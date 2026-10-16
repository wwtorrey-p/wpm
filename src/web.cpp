#include "web.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "cJSON.h"
#include "cert.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "mbedtls/base64.h"

static const char* TAG = "web";
static App* g_app;
static std::string g_cert, g_key;  // server keeps pointers to these

struct JsonDeleter { void operator()(cJSON* j) const { cJSON_Delete(j); } };
using Json = std::unique_ptr<cJSON, JsonDeleter>;

static bool authorized(httpd_req_t* req) {
    char hdr[256];
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof hdr) == ESP_OK &&
        strncmp(hdr, "Basic ", 6) == 0) {
        unsigned char dec[160];
        size_t n = 0;
        if (mbedtls_base64_decode(dec, sizeof dec - 1, &n, (const unsigned char*)hdr + 6, strlen(hdr + 6)) == 0) {
            dec[n] = 0;
            std::string s((char*)dec);
            size_t colon = s.find(':');
            if (colon != std::string::npos) {
                g_app->lock();
                bool ok = g_app->checkPassword(s.substr(0, colon), s.substr(colon + 1));
                g_app->unlock();
                if (ok) return true;
            }
        }
    }
    // No WWW-Authenticate header: the UI has its own login form, not the browser prompt.
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
    return false;
}

static esp_err_t sendJson(httpd_req_t* req, cJSON* j) {
    char* s = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!s) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t r = httpd_resp_send(req, s, HTTPD_RESP_USE_STRLEN);
    free(s);
    return r;
}

static Json readBody(httpd_req_t* req) {
    if (req->content_len == 0 || req->content_len > 512) return nullptr;
    char buf[513];
    int got = 0;
    while (got < (int)req->content_len) {
        int r = httpd_req_recv(req, buf + got, req->content_len - got);
        if (r <= 0) return nullptr;
        got += r;
    }
    buf[got] = 0;
    return Json(cJSON_Parse(buf));
}

static const char* stateName(pump::State s) {
    switch (s) {
        case pump::State::Disabled: return "disabled";
        case pump::State::Backoff: return "backoff";
        case pump::State::Settling: return "settling";
        default: return "armed";
    }
}

static const char* eventName(pump::EventType t) {
    switch (t) {
        case pump::EventType::Boot: return "boot";
        case pump::EventType::Demand: return "demand";
        case pump::EventType::RunEnd: return "run_end";
        case pump::EventType::Fault: return "fault";
        case pump::EventType::Recovery: return "recovery";
        case pump::EventType::Disabled: return "disabled";
        case pump::EventType::Enabled: return "enabled";
        default: return "reset";
    }
}

static esp_err_t getStatus(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    g_app->lock();
    pump::Status st = g_app->controller().status();
    cJSON* j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "state", stateName(st.state));
    cJSON_AddBoolToObject(j, "enabled", st.p.enabled);
    cJSON_AddBoolToObject(j, "auxClosed", st.auxClosed);
    cJSON_AddBoolToObject(j, "running", st.running);
    cJSON_AddBoolToObject(j, "alarm", st.alarm);
    cJSON_AddNumberToObject(j, "epoch", (double)g_app->epoch());
    cJSON_AddNumberToObject(j, "backoffEnd", (double)st.p.backoffEndEpoch);
    cJSON_AddNumberToObject(j, "curBackoffS", st.p.curBackoffS);
    cJSON_AddNumberToObject(j, "nextBackoffS", st.p.nextBackoffS);
    cJSON_AddNumberToObject(j, "faultCount", st.p.faultCount);
    cJSON_AddNumberToObject(j, "totalRunS", st.p.totalRunS);
    cJSON_AddNumberToObject(j, "cycleGallons", st.cycleGallons);
    cJSON_AddBoolToObject(j, "defaultPassword", g_app->defaultPassword());
    g_app->unlock();
    return sendJson(req, j);
}

static cJSON* settingsJson(const pump::Settings& s) {
    cJSON* j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "initialBackoffH", s.initialBackoffS / 3600.0);
    cJSON_AddNumberToObject(j, "maxBackoffH", s.maxBackoffS / 3600.0);
    cJSON_AddNumberToObject(j, "graceS", s.graceS);
    cJSON_AddNumberToObject(j, "gpm", s.gpm);
    cJSON_AddNumberToObject(j, "targetGalPerDay", s.targetGalPerDay);
    return j;
}

static esp_err_t getSettings(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    g_app->lock();
    cJSON* j = settingsJson(g_app->controller().settings());
    g_app->unlock();
    return sendJson(req, j);
}

static bool num(cJSON* j, const char* key, double lo, double hi, double& out) {
    cJSON* v = cJSON_GetObjectItem(j, key);
    if (!cJSON_IsNumber(v)) return true;  // optional
    if (v->valuedouble < lo || v->valuedouble > hi) return false;
    out = v->valuedouble;
    return true;
}

static esp_err_t putSettings(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    Json body = readBody(req);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
    g_app->lock();
    pump::Settings s = g_app->controller().settings();
    double init = s.initialBackoffS / 3600.0, mx = s.maxBackoffS / 3600.0, grace = s.graceS, gpm = s.gpm,
           tgt = s.targetGalPerDay;
    bool ok = num(body.get(), "initialBackoffH", 0.01, 8760, init) && num(body.get(), "maxBackoffH", 0.01, 87600, mx) &&
              num(body.get(), "graceS", 35, 600, grace) && num(body.get(), "gpm", 0.1, 500, gpm) &&
              num(body.get(), "targetGalPerDay", 0, 100000, tgt) && mx >= init;
    cJSON* resp = nullptr;
    if (ok) {
        s.initialBackoffS = (uint32_t)(init * 3600);
        s.maxBackoffS = (uint32_t)(mx * 3600);
        s.graceS = (uint32_t)grace;
        s.gpm = (float)gpm;
        s.targetGalPerDay = (float)tgt;
        g_app->applySettings(s);
        resp = settingsJson(s);
    }
    g_app->unlock();
    if (!ok) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "out of range");
    return sendJson(req, resp);
}

static esp_err_t postControl(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    Json body = readBody(req);
    cJSON* a = body ? cJSON_GetObjectItem(body.get(), "action") : nullptr;
    if (!cJSON_IsString(a)) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "action required");
    std::string act = a->valuestring;
    g_app->lock();
    auto& c = g_app->controller();
    bool ok = true;
    if (act == "enable") c.setEnabled(true);
    else if (act == "disable") c.setEnabled(false);
    else if (act == "reset") c.resetBackoff();
    else if (act == "skip") c.endBackoffNow();
    else ok = false;
    g_app->unlock();
    if (!ok) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown action");
    return getStatus(req);
}

static esp_err_t getLog(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    cJSON* arr = cJSON_CreateArray();
    g_app->lock();
    const EventLog& l = g_app->events();
    for (uint32_t i = 0; i < l.count; i++) {  // newest first
        const pump::Event& e = l.events[(l.head + kMaxEvents - 1 - i) % kMaxEvents];
        cJSON* o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "t", (double)e.epoch);
        cJSON_AddStringToObject(o, "type", eventName(e.type));
        cJSON_AddNumberToObject(o, "arg", e.arg);
        cJSON_AddItemToArray(arr, o);
    }
    g_app->unlock();
    return sendJson(req, arr);
}

static esp_err_t deleteLog(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    g_app->lock();
    g_app->clearLog();
    g_app->unlock();
    return sendJson(req, cJSON_CreateArray());
}

static esp_err_t getHistory(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    cJSON* arr = cJSON_CreateArray();
    g_app->lock();
    for (const DayBucket& b : g_app->history()) {
        if (b.day == 0) continue;
        cJSON* o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "day", b.day);
        cJSON_AddNumberToObject(o, "gallons", b.gallons);
        cJSON_AddItemToArray(arr, o);
    }
    g_app->unlock();
    return sendJson(req, arr);
}

static esp_err_t postPassword(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    Json body = readBody(req);
    cJSON* p = body ? cJSON_GetObjectItem(body.get(), "password") : nullptr;
    if (!cJSON_IsString(p)) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "password required");
    g_app->lock();
    bool ok = g_app->setPassword(p->valuestring);
    g_app->unlock();
    if (!ok) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "min 6 chars, not 'admin'");
    return httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
}

static const char* mimeFor(const std::string& path) {
    auto ends = [&](const char* s) { size_t n = strlen(s); return path.size() >= n && path.compare(path.size() - n, n, s) == 0; };
    if (ends(".html")) return "text/html";
    if (ends(".js")) return "text/javascript";
    if (ends(".css")) return "text/css";
    if (ends(".svg")) return "image/svg+xml";
    return "application/octet-stream";
}

static esp_err_t serveStatic(httpd_req_t* req) {
    // The UI shell is public so the login page can load; all data is behind /api auth.
    std::string uri = req->uri;
    uri = uri.substr(0, uri.find('?'));
    if (uri == "/" || uri.find("..") != std::string::npos) uri = "/index.html";
    std::string path = "/spiffs" + uri;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
    httpd_resp_set_type(req, mimeFor(path));
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            fclose(f);
            httpd_resp_sendstr_chunk(req, nullptr);
            return ESP_FAIL;
        }
    }
    fclose(f);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

bool startWebServer(App& app) {
    g_app = &app;
    if (!ensureCert(g_cert, g_key)) {
        ESP_LOGE(TAG, "certificate unavailable");
        return false;
    }
    httpd_ssl_config_t cfg = HTTPD_SSL_CONFIG_DEFAULT();
    cfg.servercert = (const uint8_t*)g_cert.c_str();
    cfg.servercert_len = g_cert.size() + 1;
    cfg.prvtkey_pem = (const uint8_t*)g_key.c_str();
    cfg.prvtkey_len = g_key.size() + 1;
    cfg.httpd.max_uri_handlers = 12;
    cfg.httpd.uri_match_fn = httpd_uri_match_wildcard;
    cfg.httpd.stack_size = 10240;

    httpd_handle_t server = nullptr;
    if (httpd_ssl_start(&server, &cfg) != ESP_OK) return false;

    const httpd_uri_t routes[] = {
        {"/api/status", HTTP_GET, getStatus, nullptr},
        {"/api/settings", HTTP_GET, getSettings, nullptr},
        {"/api/settings", HTTP_PUT, putSettings, nullptr},
        {"/api/control", HTTP_POST, postControl, nullptr},
        {"/api/log", HTTP_GET, getLog, nullptr},
        {"/api/log", HTTP_DELETE, deleteLog, nullptr},
        {"/api/history", HTTP_GET, getHistory, nullptr},
        {"/api/password", HTTP_POST, postPassword, nullptr},
        {"/*", HTTP_GET, serveStatic, nullptr},  // must stay last
    };
    for (const auto& r : routes) httpd_register_uri_handler(server, &r);
    return true;
}

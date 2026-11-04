#include "web.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>

#include "cJSON.h"
#include "cert.h"
#include "esp_app_format.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "flo.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "web_assets.h"

static const char* TAG = "web";
static App* g_app;
static std::string g_cert, g_key;  // server keeps pointers to these

// The UI files are compiled in from src/generated/web_assets.cpp (see tools/build_web.py).

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
        case pump::State::RunBackoff: return "run_backoff";
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
        case pump::EventType::RunLimit: return "run_limit";
        case pump::EventType::RunRecovery: return "run_recovery";
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
    cJSON_AddNumberToObject(j, "runBackoffEnd", (double)st.p.runBackoffEndEpoch);
    cJSON_AddNumberToObject(j, "curBackoffS", st.p.curBackoffS);
    cJSON_AddNumberToObject(j, "nextBackoffS", st.p.nextBackoffS);
    cJSON_AddNumberToObject(j, "faultCount", st.p.faultCount);
    cJSON_AddNumberToObject(j, "totalRunS", st.p.totalRunS);
    cJSON_AddNumberToObject(j, "cycleGallons", st.cycleGallons);
    cJSON_AddNumberToObject(j, "cycleRunS", st.cycleRunS);
    cJSON_AddNumberToObject(j, "capacityGal", st.capacityGal);
    cJSON_AddNumberToObject(j, "maxCapacityGal", pump::kMaxCapacityGal);
    cJSON_AddBoolToObject(j, "defaultPassword", g_app->defaultPassword());
    cJSON_AddNumberToObject(j, "floAnchor", (double)g_app->floAnchor());
    cJSON_AddStringToObject(j, "certIp", certIp().c_str());
    cJSON_AddStringToObject(j, "username", g_app->username().c_str());
    g_app->unlock();
    const esp_app_desc_t* desc = esp_app_get_description();
    cJSON_AddStringToObject(j, "version", desc->version);
    std::string built = std::string(desc->date) + " " + desc->time;
    cJSON_AddStringToObject(j, "built", built.c_str());
    return sendJson(req, j);
}

static cJSON* settingsJson(const pump::Settings& s) {
    cJSON* j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "initialBackoffH", s.initialBackoffS / 3600.0);
    cJSON_AddNumberToObject(j, "maxBackoffH", s.maxBackoffS / 3600.0);
    cJSON_AddNumberToObject(j, "graceS", s.graceS);
    cJSON_AddNumberToObject(j, "gpm", s.gpm);
    cJSON_AddNumberToObject(j, "targetGalPerDay", s.targetGalPerDay);
    cJSON_AddNumberToObject(j, "lowLevelGal", s.lowLevelGal);
    cJSON_AddNumberToObject(j, "highLevelGal", s.highLevelGal);
    cJSON_AddNumberToObject(j, "maxRunM", s.maxRunS / 60.0);
    cJSON_AddNumberToObject(j, "runBackoffM", s.runBackoffS / 60.0);
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
           tgt = s.targetGalPerDay, low = s.lowLevelGal, high = s.highLevelGal,
           maxRun = s.maxRunS / 60.0, runBackoff = s.runBackoffS / 60.0;
    bool ok = num(body.get(), "initialBackoffH", 0.01, 8760, init) && num(body.get(), "maxBackoffH", 0.01, 87600, mx) &&
              num(body.get(), "graceS", 35, 600, grace) && num(body.get(), "gpm", 0.1, 500, gpm) &&
              num(body.get(), "targetGalPerDay", 0, 100000, tgt) && mx >= init &&
              num(body.get(), "lowLevelGal", 0, pump::kMaxCapacityGal, low) &&
              num(body.get(), "highLevelGal", 0, pump::kMaxCapacityGal, high) && low < high &&
              num(body.get(), "maxRunM", 0, 1440, maxRun) &&
              num(body.get(), "runBackoffM", 0.01, 87600, runBackoff);
    cJSON* resp = nullptr;
    if (ok) {
        s.initialBackoffS = (uint32_t)(init * 3600);
        s.maxBackoffS = (uint32_t)(mx * 3600);
        s.graceS = (uint32_t)grace;
        s.gpm = (float)gpm;
        s.targetGalPerDay = (float)tgt;
        s.lowLevelGal = (float)low;
        s.highLevelGal = (float)high;
        s.maxRunS = (uint32_t)(maxRun * 60);
        s.runBackoffS = (uint32_t)(runBackoff * 60);
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
        cJSON_AddNumberToObject(o, "gal", std::round(e.gallons * 10.0f) / 10.0);
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

static esp_err_t getCapacityHistory(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    cJSON* arr = cJSON_CreateArray();
    g_app->lock();
    const CapHistory& h = g_app->capacityHistory();
    for (uint32_t i = 0; i < h.count; i++) {  // oldest first
        const CapSample& s = h.samples[(h.head + kCapSamples - h.count + i) % kCapSamples];
        cJSON* o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "t", s.epoch);
        cJSON_AddNumberToObject(o, "gal", std::round(s.gallons * 10.0f) / 10.0);
        cJSON_AddItemToArray(arr, o);
    }
    g_app->unlock();
    return sendJson(req, arr);
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

static esp_err_t sendText(httpd_req_t* req, const char* status, const char* msg);
static std::string g_floLocation;  // cached after the first lookup; cleared when the login changes

static esp_err_t postPassword(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    Json body = readBody(req);
    cJSON* p = body ? cJSON_GetObjectItem(body.get(), "password") : nullptr;
    cJSON* u = body ? cJSON_GetObjectItem(body.get(), "username") : nullptr;
    if ((p && !cJSON_IsString(p)) || (u && !cJSON_IsString(u)) || !(p || u)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "username or password required");
    }
    g_app->lock();
    bool ok = g_app->setCredentials(u ? u->valuestring : "", p ? p->valuestring : "");
    g_app->unlock();
    if (!ok) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "username: 1-64 printable chars, no ':'; password: 6-72 chars, not 'admin'");
    }
    g_floLocation.clear();
    return httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
}

// The TLS client needs more stack than the HTTP server task has, so the Flo call runs in its own task.
struct FloJob {
    std::string user, pass, location, err;
    time_t start, end;
    flo::Interval interval;
    flo::Usage usage;
    bool ok = false;
    SemaphoreHandle_t done;
};

static void floTask(void* arg) {
    auto* job = static_cast<FloJob*>(arg);
    job->ok = flo::getUsage(job->user, job->pass, job->location, job->start, job->end, job->interval, job->usage,
                            job->err);
    xSemaphoreGive(job->done);
    vTaskDelete(nullptr);
}

// Runs the job in its own task and waits for it. False only if the task could not be started.
static bool runFloJob(FloJob& job) {
    job.done = xSemaphoreCreateBinary();
    if (!job.done) return false;
    if (xTaskCreate(floTask, "flo", 12288, &job, 5, nullptr) != pdPASS) {
        vSemaphoreDelete(job.done);
        return false;
    }
    xSemaphoreTake(job.done, portMAX_DELAY);
    vSemaphoreDelete(job.done);
    return true;
}

// POST /api/capacity/refresh : corrects the capacity estimate with the water Flo measured since the last reset
// or the last correction. Limited to once an hour.
static esp_err_t postCapacityRefresh(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    std::unique_ptr<FloJob> job(new (std::nothrow) FloJob());
    if (!job) return httpd_resp_send_500(req);

    FloPlan plan;
    int64_t waitS = 0;
    g_app->lock();
    bool haveLogin = g_app->floLogin(job->user, job->pass);
    FloPlanResult r = haveLogin ? g_app->planFloRefresh(plan, waitS) : FloPlanResult::NothingNew;
    g_app->unlock();
    if (!haveLogin) {
        return sendText(req, "409 Conflict", "Set a new password first; the Flo login is the same as this login");
    }
    auto skipped = [&](const char* msg) {
        cJSON* j = cJSON_CreateObject();
        cJSON_AddBoolToObject(j, "applied", false);
        cJSON_AddStringToObject(j, "message", msg);
        return sendJson(req, j);
    };
    switch (r) {
        case FloPlanResult::NoClock: return sendText(req, "409 Conflict", "Clock not set yet");
        case FloPlanResult::NoAnchor:
            return skipped("Waiting for the next capacity reset before usage can be applied");
        case FloPlanResult::NothingNew: return skipped("Nothing new to apply yet");
        case FloPlanResult::TooSoon: {
            char msg[64];
            snprintf(msg, sizeof msg, "Refreshed recently; try again in %d min", (int)((waitS + 59) / 60));
            return sendText(req, "429 Too Many Requests", msg);
        }
        case FloPlanResult::Ready: break;
    }

    job->location = g_floLocation;
    // Start on the hour so the bucket that contains the reset is included.
    job->start = (time_t)(plan.start - plan.start % 3600);
    job->end = (time_t)plan.end;
    job->interval = flo::Interval::Hourly;
    if (!runFloJob(*job)) return httpd_resp_send_500(req);
    if (!job->ok) {
        ESP_LOGW(TAG, "flo refresh failed: %s", job->err.c_str());
        return sendText(req, "502 Bad Gateway", job->err.c_str());
    }
    g_floLocation = job->location;

    float used = 0;
    g_app->lock();
    bool done = g_app->commitFloRefresh(plan, job->usage.items, used);
    float capacity = g_app->controller().status().capacityGal;
    g_app->unlock();
    if (!done) return skipped("The capacity was reset during the refresh; try again later");

    cJSON* j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "applied", true);
    cJSON_AddNumberToObject(j, "usedGallons", std::round(used * 10.0f) / 10.0);
    cJSON_AddNumberToObject(j, "appliedThrough", (double)plan.end);
    cJSON_AddBoolToObject(j, "more", plan.more);
    cJSON_AddNumberToObject(j, "capacityGal", capacity);
    return sendJson(req, j);
}

// GET /api/flo/usage?days=7&interval=1d : fetches consumption from the Flo cloud with the device login.
static esp_err_t getFloUsage(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    int days = 7;
    int64_t startQ = 0, endQ = 0;  // optional explicit range (epoch seconds); overrides days
    flo::Interval interval = flo::Interval::Daily;
    char query[96], val[16];
    if (httpd_req_get_url_query_str(req, query, sizeof query) == ESP_OK) {
        if (httpd_query_key_value(query, "days", val, sizeof val) == ESP_OK) days = atoi(val);
        if (httpd_query_key_value(query, "start", val, sizeof val) == ESP_OK) startQ = atoll(val);
        if (httpd_query_key_value(query, "end", val, sizeof val) == ESP_OK) endQ = atoll(val);
        if (httpd_query_key_value(query, "interval", val, sizeof val) == ESP_OK) {
            if (!strcmp(val, "1h")) interval = flo::Interval::Hourly;
            else if (!strcmp(val, "1m")) interval = flo::Interval::Monthly;
            else if (strcmp(val, "1d")) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "interval: 1h, 1d or 1m");
        }
    }
    const int maxDays = interval == flo::Interval::Hourly ? 7 : 93;
    if (!startQ && (days < 1 || days > maxDays)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "days out of range");
    }

    std::unique_ptr<FloJob> job(new (std::nothrow) FloJob());
    if (!job) return httpd_resp_send_500(req);
    g_app->lock();
    bool haveLogin = g_app->floLogin(job->user, job->pass);
    int64_t now = g_app->epoch();
    g_app->unlock();
    if (!haveLogin) {
        return sendText(req, "409 Conflict", "Set a new password first; the Flo login is the same as this login");
    }
    if (now <= 0) return sendText(req, "409 Conflict", "Clock not set yet");

    job->location = g_floLocation;
    job->end = endQ ? (time_t)endQ : (time_t)now;
    job->start = startQ ? (time_t)startQ : job->end - (time_t)days * 86400;
    if (job->end > now) job->end = (time_t)now;
    if (job->start >= job->end || job->end - job->start > (int64_t)maxDays * 86400) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid range");
    }
    job->interval = interval;
    if (!runFloJob(*job)) return httpd_resp_send_500(req);

    if (!job->ok) {
        ESP_LOGW(TAG, "flo usage failed: %s", job->err.c_str());
        return sendText(req, "502 Bad Gateway", job->err.c_str());
    }
    g_floLocation = job->location;
    cJSON* j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "locationId", job->location.c_str());
    cJSON_AddNumberToObject(j, "totalGallons", job->usage.totalGallons);
    cJSON* arr = cJSON_AddArrayToObject(j, "items");
    for (const flo::Sample& s : job->usage.items) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "time", s.time.c_str());
        cJSON_AddNumberToObject(o, "gallons", s.gallons);
        cJSON_AddItemToArray(arr, o);
    }
    return sendJson(req, j);
}

// Public: the certificate is not secret, and it has to be fetchable before a client trusts the device.
static esp_err_t getCertFile(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/x-x509-ca-cert");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"water-pump-monitor.crt\"");
    return httpd_resp_send(req, g_cert.c_str(), HTTPD_RESP_USE_STRLEN);
}

// POST /api/cert {"ip": "192.168.1.63"} : makes a new certificate for that address and restarts to use it.
static esp_err_t postCert(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    Json body = readBody(req);
    cJSON* ip = body ? cJSON_GetObjectItem(body.get(), "ip") : nullptr;
    if (!cJSON_IsString(ip)) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ip required");
    g_app->lock();
    bool busy = g_app->controller().status().running;
    g_app->unlock();
    if (busy) return sendText(req, "409 Conflict", "Pump is running; try again when it is idle");
    if (!regenerateCert(ip->valuestring)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid IPv4 address or certificate failed");
    }
    sendText(req, "200 OK", "ok");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t serveStatic(httpd_req_t* req) {
    // The UI shell is public so the login page can load; all data is behind /api auth.
    std::string uri = req->uri;
    uri = uri.substr(0, uri.find('?'));
    for (size_t i = 0; i < kWebAssetCount; i++) {
        const WebAsset& a = kWebAssets[i];
        if (uri != a.path) continue;
        httpd_resp_set_type(req, a.mime);
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
        return httpd_resp_send(req, (const char*)a.data, a.size);
    }
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
}

static esp_err_t sendText(httpd_req_t* req, const char* status, const char* msg) {
    httpd_resp_set_status(req, status);
    return httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);
}

static bool recvFull(httpd_req_t* req, char* buf, size_t len) {
    size_t got = 0;
    int idle = 0;
    while (got < len) {
        int n = httpd_req_recv(req, buf + got, len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++idle > 10) return false;
            continue;
        }
        if (n <= 0) return false;
        idle = 0;
        got += n;
    }
    return true;
}

// Rejects images that are not an app build of this project before anything is written.
static bool isOurImage(const char* first) {
    constexpr size_t kDescOffset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
    if ((uint8_t)first[0] != ESP_IMAGE_HEADER_MAGIC) return false;
    esp_app_desc_t desc;
    memcpy(&desc, first + kDescOffset, sizeof desc);
    if (desc.magic_word != ESP_APP_DESC_MAGIC_WORD) return false;
    return strncmp(desc.project_name, esp_app_get_description()->project_name, sizeof desc.project_name) == 0;
}

static esp_err_t postFirmware(httpd_req_t* req) {
    constexpr size_t kChunk = 4096;
    constexpr size_t kMinImage = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
    if (!authorized(req)) return ESP_OK;

    const size_t total = req->content_len;
    const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
    if (!part || total < kMinImage) return sendText(req, "400 Bad Request", "Not a firmware image");
    if (total > part->size) return sendText(req, "413 Payload Too Large", "Image too large");

    g_app->lock();
    bool busy = g_app->controller().status().running;
    g_app->unlock();
    if (busy) return sendText(req, "409 Conflict", "Pump is running; try again when it is idle");

    std::unique_ptr<char[]> buf(new (std::nothrow) char[kChunk]);
    if (!buf) return httpd_resp_send_500(req);

    esp_ota_handle_t handle = 0;
    bool begun = false;
    size_t received = 0;
    while (received < total) {
        const size_t n = std::min(kChunk, total - received);
        if (!recvFull(req, buf.get(), n)) {
            if (begun) esp_ota_abort(handle);
            ESP_LOGW(TAG, "firmware upload interrupted at %u of %u", (unsigned)received, (unsigned)total);
            return ESP_FAIL;
        }
        if (!begun) {
            if (!isOurImage(buf.get())) return sendText(req, "400 Bad Request", "Not a firmware image for this device");
            if (esp_ota_begin(part, total, &handle) != ESP_OK) return httpd_resp_send_500(req);
            begun = true;
        }
        if (esp_ota_write(handle, buf.get(), n) != ESP_OK) {
            esp_ota_abort(handle);
            return httpd_resp_send_500(req);
        }
        received += n;
    }
    if (esp_ota_end(handle) != ESP_OK) return sendText(req, "400 Bad Request", "Image failed validation");
    if (esp_ota_set_boot_partition(part) != ESP_OK) return httpd_resp_send_500(req);

    ESP_LOGI(TAG, "firmware installed (%u bytes), restarting", (unsigned)total);
    sendText(req, "200 OK", "ok");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
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
    cfg.httpd.max_uri_handlers = 20;
    cfg.httpd.uri_match_fn = httpd_uri_match_wildcard;
    cfg.httpd.stack_size = 10240;
    cfg.httpd.recv_wait_timeout = 10;

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
        {"/api/capacity", HTTP_GET, getCapacityHistory, nullptr},
        {"/api/password", HTTP_POST, postPassword, nullptr},
        {"/api/flo/usage", HTTP_GET, getFloUsage, nullptr},
        {"/api/capacity/refresh", HTTP_POST, postCapacityRefresh, nullptr},
        {"/api/firmware", HTTP_POST, postFirmware, nullptr},
        {"/api/cert", HTTP_POST, postCert, nullptr},
        {"/cert.crt", HTTP_GET, getCertFile, nullptr},
        {"/*", HTTP_GET, serveStatic, nullptr},  // must stay last
    };
    for (const auto& r : routes) httpd_register_uri_handler(server, &r);
    return true;
}

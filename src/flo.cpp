#include "flo.h"

#include <cctype>
#include <cstdio>
#include <memory>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

namespace flo {
namespace {

const char* TAG = "flo";

const char* SSO_TOKEN_URL = "https://4j1gkf0vji.execute-api.us-east-2.amazonaws.com/prod/v1/oauth2/token";
const char* SSO_CLIENT_ID = "6qn9pep31dglq6ed4fvlq6rp5t";
const char* API_BASE = "https://api-gw.meetflo.com/api/v2";
constexpr size_t MAX_BODY = 64 * 1024;

struct JsonDeleter {
    void operator()(cJSON* j) const { cJSON_Delete(j); }
};
using Json = std::unique_ptr<cJSON, JsonDeleter>;

struct Response {
    std::string body;
    bool overflow = false;
};

esp_err_t onEvent(esp_http_client_event_t* e) {
    if (e->event_id == HTTP_EVENT_ON_DATA && e->user_data) {
        auto* r = static_cast<Response*>(e->user_data);
        if (r->body.size() + e->data_len > MAX_BODY) {
            r->overflow = true;
        } else {
            r->body.append(static_cast<const char*>(e->data), e->data_len);
        }
    }
    return ESP_OK;
}

// Performs one request and parses the JSON reply. token may be empty (login).
Json request(esp_http_client_method_t method, const std::string& url, const std::string& token,
             const std::string& postBody, std::string& err) {
    Response resp;
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.method = method;
    cfg.timeout_ms = 25000;
    // The default 512-byte buffers are too small for the JWT in Authorization and the gateway's response headers.
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 4096;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.event_handler = onEvent;
    cfg.user_data = &resp;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        err = "http client init failed";
        return nullptr;
    }
    // The Flo gateway is fronted for a browser client; match the headers the web app sends.
    esp_http_client_set_header(c, "User-Agent",
                               "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_2) AppleWebKit/537.36 (KHTML, like Gecko) "
                               "Chrome/79.0.3945.117 Safari/537.36");
    esp_http_client_set_header(c, "Accept", "application/json, text/plain, */*");
    esp_http_client_set_header(c, "Content-Type", "application/json;charset=UTF-8");
    esp_http_client_set_header(c, "Origin", "https://user.meetflo.com");
    esp_http_client_set_header(c, "Referer", "https://user.meetflo.com/home");
    if (!token.empty()) {
        std::string auth = "Bearer " + token;
        esp_http_client_set_header(c, "Authorization", auth.c_str());
    }
    if (method == HTTP_METHOD_POST) {
        esp_http_client_set_post_field(c, postBody.c_str(), postBody.size());
    }
    esp_err_t rc = esp_http_client_perform(c);
    if (rc == ESP_ERR_HTTP_EAGAIN && method == HTTP_METHOD_GET) {  // read timed out; GETs are safe to repeat
        ESP_LOGW(TAG, "timeout, retrying %s", url.c_str());
        resp.body.clear();
        rc = esp_http_client_perform(c);
    }
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);

    if (rc != ESP_OK) {
        err = std::string("request failed: ") + esp_err_to_name(rc);
        return nullptr;
    }
    if (status < 200 || status >= 300) {
        err = "HTTP " + std::to_string(status);
        return nullptr;
    }
    if (resp.overflow) {
        err = "response too large";
        return nullptr;
    }
    Json j(cJSON_Parse(resp.body.c_str()));
    if (!j) err = "invalid JSON response";
    return j;
}

std::string urlEncode(const std::string& s) {
    std::string out;
    char buf[4];
    for (unsigned char ch : s) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out += static_cast<char>(ch);
        } else {
            std::snprintf(buf, sizeof buf, "%%%02X", ch);
            out += buf;
        }
    }
    return out;
}

std::string isoUtc(time_t t) {
    struct tm tm;
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S+00:00", &tm);
    return buf;
}

const char* intervalName(Interval i) {
    switch (i) {
        case Interval::Hourly: return "1h";
        case Interval::Monthly: return "1m";
        default: return "1d";
    }
}

std::string str(const cJSON* obj, const char* key) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}

// Parses "2026-10-27T00:00:00-06:00" (or "...Z" / no offset, taken as UTC) to UTC seconds; 0 on failure.
int64_t parseIso(const std::string& s) {
    int y, mo, d, h, mi, sec;
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) != 6) return 0;
    // Days since 1970-01-01 for a civil date.
    y -= mo <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t t = ((int64_t)era * 146097 + doe - 719468) * 86400 + h * 3600 + mi * 60 + sec;
    const size_t tz = s.find_first_of("+-", 19);
    if (tz != std::string::npos) {
        int oh = 0, om = 0;
        if (std::sscanf(s.c_str() + tz + 1, "%d:%d", &oh, &om) >= 1) {
            const int64_t off = oh * 3600 + om * 60;
            t += s[tz] == '-' ? off : -off;
        }
    }
    return t;
}

bool containsHome(std::string s) {
    for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s.find("home") != std::string::npos;
}

bool login(const std::string& email, const std::string& password, std::string& token, std::string& err) {
    Json body(cJSON_CreateObject());
    cJSON_AddStringToObject(body.get(), "username", email.c_str());
    cJSON_AddStringToObject(body.get(), "password", password.c_str());
    cJSON_AddStringToObject(body.get(), "client_id", SSO_CLIENT_ID);
    std::unique_ptr<char, decltype(&cJSON_free)> text(cJSON_PrintUnformatted(body.get()), cJSON_free);
    if (!text) {
        err = "out of memory";
        return false;
    }
    Json r = request(HTTP_METHOD_POST, SSO_TOKEN_URL, "", text.get(), err);
    if (!r) {
        err = "login: " + err;
        return false;
    }
    // The endpoint nests the token object; tolerate a flat reply too.
    const cJSON* t = cJSON_GetObjectItemCaseSensitive(r.get(), "token");
    token = str(cJSON_IsObject(t) ? t : r.get(), "access_token");
    if (token.empty()) {
        err = "login returned no access token";
        return false;
    }
    return true;
}

bool pickLocation(const std::string& token, std::string& locationId, std::string& err) {
    Json me = request(HTTP_METHOD_GET, std::string(API_BASE) + "/users/me", token, "", err);
    if (!me) {
        err = "users/me: " + err;
        return false;
    }
    std::string userId = str(me.get(), "id");
    if (userId.empty()) {
        err = "users/me returned no id";
        return false;
    }
    Json user = request(HTTP_METHOD_GET, std::string(API_BASE) + "/users/" + userId + "?expand=locations", token,
                        "", err);
    if (!user) {
        err = "user info: " + err;
        return false;
    }
    const cJSON* locs = cJSON_GetObjectItemCaseSensitive(user.get(), "locations");
    std::string first, home;
    const cJSON* loc = nullptr;
    cJSON_ArrayForEach(loc, locs) {
        std::string id = str(loc, "id");
        if (id.empty()) continue;
        if (first.empty()) first = id;
        if (home.empty() && containsHome(str(loc, "nickname") + " " + str(loc, "address"))) home = id;
    }
    locationId = home.empty() ? first : home;
    if (locationId.empty()) {
        err = "account has no locations";
        return false;
    }
    return true;
}

}  // namespace

bool getUsage(const std::string& email, const std::string& password, std::string& locationId, time_t start,
              time_t end, Interval interval, Usage& out, std::string& err) {
    out = Usage{};
    std::string token;
    if (!login(email, password, token, err)) return false;
    if (locationId.empty() && !pickLocation(token, locationId, err)) return false;

    std::string url = std::string(API_BASE) + "/water/consumption?locationId=" + urlEncode(locationId) +
                      "&startDate=" + urlEncode(isoUtc(start)) + "&endDate=" + urlEncode(isoUtc(end)) +
                      "&interval=" + intervalName(interval);
    Json r = request(HTTP_METHOD_GET, url, token, "", err);
    if (!r) {
        err = "consumption: " + err;
        return false;
    }
    const cJSON* agg = cJSON_GetObjectItemCaseSensitive(r.get(), "aggregations");
    const cJSON* total = cJSON_GetObjectItemCaseSensitive(agg, "sumTotalGallonsConsumed");
    if (cJSON_IsNumber(total)) out.totalGallons = total->valuedouble;

    const cJSON* items = cJSON_GetObjectItemCaseSensitive(r.get(), "items");
    if (!cJSON_IsArray(items)) {
        err = "consumption reply has no items";
        return false;
    }
    const cJSON* it = nullptr;
    cJSON_ArrayForEach(it, items) {
        const cJSON* g = cJSON_GetObjectItemCaseSensitive(it, "gallonsConsumed");
        std::string when = str(it, "time");
        const int64_t epoch = parseIso(when);
        out.items.push_back({std::move(when), epoch, cJSON_IsNumber(g) ? g->valuedouble : 0.0});
    }
    ESP_LOGI(TAG, "usage: %zu samples, %.1f gal", out.items.size(), out.totalGallons);
    return true;
}

}  // namespace flo

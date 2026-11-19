#include "flo_auto.h"

#include <string>

#include "app.h"
#include "esp_log.h"
#include "flo_schedule.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi.h"

namespace {
constexpr uint32_t kPollMs = 60000;
const char* TAG = "flo_auto";

void run(void* arg) {
    auto& app = *static_cast<App*>(arg);
    std::string location;
    std::string locationUser;
    int64_t retryAt = 0;
    bool catchingUp = false;
    for (;;) {
        std::string user, pass, status;
        FloPlan plan;
        app.lock();
        const int64_t now = app.epoch();
        const int64_t last = app.floLastRefresh();
        const bool login = app.floLogin(user, pass);
        const FloPlanResult result = app.planFloRefresh(plan);
        if (!login) status = "Flo login not configured";
        else if (result == FloPlanResult::NoClock) status = "Waiting for device clock";
        else if (result == FloPlanResult::NoAnchor) status = "Waiting for a known cistern level";
        else if (!wifi::connected()) status = "Flo update waiting for Wi-Fi";
        else if (result == FloPlanResult::NothingNew) status = "Waiting for new Flo data";
        else if (now < retryAt) status = app.floStatus();
        else if (!flo_schedule::due(now, last, app.floAnchor(), kFloMaxSpanS, retryAt, catchingUp))
            status = "Flo update scheduled daily";
        if (!status.empty()) app.setFloStatus(status);
        app.unlock();

        if (status.empty()) {
            if (user != locationUser) {
                location.clear();
                locationUser = user;
            }
            flo::Usage usage;
            std::string err;
            const time_t start = static_cast<time_t>(plan.start - plan.start % 3600);
            const bool ok = flo::getUsage(user, pass, location, start, static_cast<time_t>(plan.end),
                                          flo::Interval::Hourly, usage, err);
            app.lock();
            const int64_t finished = app.epoch();
            std::string currentUser, currentPass;
            if (!app.floLogin(currentUser, currentPass) || currentUser != user || currentPass != pass) {
                status = "Flo credentials changed; update will retry";
                location.clear();
            } else if (!ok) {
                status = "Flo update failed: " + err;
            } else {
                float used = 0;
                switch (app.commitFloRefresh(plan, usage.items, used)) {
                    case FloCommitResult::Applied:
                        catchingUp = plan.more;
                        status = catchingUp ? "Flo update catching up" : "Flo update scheduled daily";
                        break;
                    case FloCommitResult::AnchorChanged:
                        status = "Cistern level reset during Flo update; retrying";
                        break;
                    case FloCommitResult::StorageError:
                        status = "Flo update failed: could not save correction";
                        break;
                }
            }
            const bool failed = status != "Flo update scheduled daily" && status != "Flo update catching up";
            retryAt = failed ? finished + flo_schedule::kRetryInterval : 0;
            if (failed) ESP_LOGW(TAG, "%s", status.c_str());
            app.setFloStatus(status);
            app.unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(kPollMs));
    }
}
}  // namespace

bool startFloAuto(App& app) {
    if (xTaskCreate(run, "flo_auto", 16384, &app, 5, nullptr) == pdPASS) return true;
    ESP_LOGE(TAG, "could not start automatic Flo task");
    app.lock();
    app.setFloStatus("Flo update failed: could not start background task");
    app.unlock();
    return false;
}

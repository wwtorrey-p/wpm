#include "app.h"

#include <ctime>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "storage.h"

static const char* TAG = "app";

static int64_t wallClock() {
    time_t t = time(nullptr);
    return t > 1'700'000'000 ? t : 0;
}

static void hashPassword(const uint8_t salt[16], const std::string& pass, uint8_t out[32]) {
    mbedtls_sha256_context c;
    mbedtls_sha256_init(&c);
    mbedtls_sha256_starts(&c, 0);
    mbedtls_sha256_update(&c, salt, 16);
    mbedtls_sha256_update(&c, (const unsigned char*)pass.data(), pass.size());
    mbedtls_sha256_finish(&c, out);
    mbedtls_sha256_free(&c);
}

static void makeCredentials(Credentials& c, const std::string& pass, bool isDefault) {
    esp_fill_random(c.salt, sizeof c.salt);
    hashPassword(c.salt, pass, c.hash);
    c.isDefault = isDefault;
}

void App::init() {
    pump::Settings s;
    pump::Persisted p;
    storage::load("settings", s);
    storage::load("persist", p);
    storage::load("events", log_);
    storage::load("days", days_);
    if (!storage::load("cred", cred_)) {
        makeCredentials(cred_, "admin", true);
        storage::save("cred", cred_);
    }
    epoch_ = wallClock();
    ctl_.begin(s, p, esp_timer_get_time() / 1000, epoch_);
    savePersisted();
}

void App::onEvent(const pump::Event& e) {
    log_.events[log_.head] = e;
    log_.head = (log_.head + 1) % kMaxEvents;
    if (log_.count < kMaxEvents) log_.count++;
    storage::save("events", log_);
}

void App::savePersisted() {
    storage::save("persist", ctl_.status().p);
    ctl_.takeDirty();
}

void App::addRunTime(uint32_t seconds) {
    if (epoch_ <= 0) return;
    uint32_t day = epoch_ / 86400;
    DayBucket& b = days_[day % kHistoryDays];
    if (b.day != day) b = DayBucket{day, 0};
    b.gallons += seconds * ctl_.settings().gpm / 60.0f;
    historyDirty_ = true;
}

void App::tick(bool run, bool alarm) {
    lock();
    epoch_ = wallClock();
    ctl_.tick(pump::Inputs{(uint32_t)(esp_timer_get_time() / 1000), epoch_, run, alarm});
    tickCount_++;
    if (tickCount_ % 10 == 0) {
        if (uint32_t s = ctl_.takeRunSeconds()) addRunTime(s);
    }
    // Structural changes persist at once; run-time counters at most once a minute.
    if (ctl_.takeDirty()) savePersisted();
    if (tickCount_ % 600 == 0 && historyDirty_) {
        savePersisted();
        storage::save("days", days_);
        historyDirty_ = false;
    }
    unlock();
}

void App::applySettings(const pump::Settings& s) {
    ctl_.setSettings(s);
    storage::save("settings", s);
    savePersisted();
}

bool App::checkPassword(const std::string& user, const std::string& pass) const {
    uint8_t h[32];
    hashPassword(cred_.salt, pass, h);
    uint8_t diff = user == "admin" ? 0 : 1;
    for (int i = 0; i < 32; i++) diff |= h[i] ^ cred_.hash[i];
    return diff == 0;
}

bool App::setPassword(const std::string& pass) {
    if (pass.size() < 6 || pass == "admin") return false;
    makeCredentials(cred_, pass, false);
    ESP_LOGI(TAG, "password changed");
    return storage::save("cred", cred_);
}

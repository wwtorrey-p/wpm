#include "app.h"

#include <algorithm>
#include <cstring>
#include <ctime>

#include "capcorrect.h"
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
    storage::loadPrefix("settings", s);
    storage::load("persist", p);
    float capacity = -1.0f;
    if (storage::load("capacity", capacity)) ctl_.restoreCapacity(capacity);
    // The event record gained a field, so the log moved to a new key; drop the old one to free its flash.
    storage::erase("events");
    storage::load("events2", log_);
    storage::loadPrefix("caphist", cap_);
    storage::load("days", days_);
    storage::load("flo", flo_);
    if (!storage::loadPrefix("cred", cred_)) {
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
    storage::save("events2", log_);
}

void App::pushCapacity(int64_t epoch, float gallons) {
    if (epoch <= 0) return;  // no wall-clock time yet
    cap_.samples[cap_.head] = CapSample{(uint32_t)epoch, gallons};
    cap_.head = (cap_.head + 1) % kCapSamples;
    if (cap_.count < kCapSamples) cap_.count++;
    lastCapEpoch_ = epoch;
    capDirty_ = true;
}

void App::savePersisted() {
    const pump::Status st = ctl_.status();
    storage::save("persist", st.p);
    storage::save("capacity", st.capacityGal);
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
    capBefore_ = ctl_.status().capacityGal;
    ctl_.tick(pump::Inputs{(uint32_t)(esp_timer_get_time() / 1000), epoch_, run, alarm});
    tickCount_++;
    // A correction is a step: record the level just before and just after it at the same time.
    if (ctl_.takeCapacityStep()) {
        const float after = ctl_.status().capacityGal;
        if (after != capBefore_) pushCapacity(epoch_, capBefore_);
        pushCapacity(epoch_, after);
        // A reset makes the level known again, so everything before it no longer needs correcting.
        flo_.anchor = epoch_;
        storage::save("flo", flo_);
    }
    // While the pump runs the estimate climbs steadily, so sample it every 10 minutes.
    if (ctl_.status().running && epoch_ > 0 && epoch_ - lastCapEpoch_ >= 600) {
        pushCapacity(epoch_, ctl_.status().capacityGal);
    }
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
    if (tickCount_ % 600 == 0 && capDirty_) {
        storage::save("caphist", cap_);
        capDirty_ = false;
    }
    unlock();
}

void App::applySettings(const pump::Settings& s) {
    ctl_.setSettings(s);
    storage::save("settings", s);
    savePersisted();
}

float App::addWater() {
    const float before = ctl_.status().capacityGal;
    ctl_.adjustCapacity(ctl_.settings().waterAddGal);
    const float after = ctl_.status().capacityGal;
    if (after != before) {
        pushCapacity(epoch_, before);
        pushCapacity(epoch_, after);
        if (capDirty_) {
            storage::save("caphist", cap_);
            capDirty_ = false;
        }
    }
    savePersisted();
    return after - before;
}

void App::clearLog() {
    log_ = EventLog{};
    storage::save("events2", log_);
}

bool App::checkPassword(const std::string& user, const std::string& pass) const {
    uint8_t h[32];
    hashPassword(cred_.salt, pass, h);
    const std::string expected = username();
    uint8_t diff = 0;
    for (size_t i = 0; i < std::max(user.size(), expected.size()); i++) {
        diff |= (i < user.size() ? user[i] : 0) ^ (i < expected.size() ? expected[i] : 0);
    }
    for (int i = 0; i < 32; i++) diff |= h[i] ^ cred_.hash[i];
    return diff == 0;
}

std::string App::username() const { return cred_.user[0] ? cred_.user : "admin"; }

static bool validUser(const std::string& u) {
    if (u.empty() || u.size() > kMaxUserLen) return false;
    for (unsigned char c : u) {
        if (c < 0x21 || c > 0x7e || c == ':') return false;  // ':' would break Basic auth parsing
    }
    return true;
}

bool App::setCredentials(const std::string& user, const std::string& pass) {
    if (user.empty() && pass.empty()) return false;
    if (!user.empty() && !validUser(user)) return false;
    if (!pass.empty() && (pass.size() < 6 || pass.size() > kMaxPassLen || pass == "admin")) return false;
    Credentials next = cred_;
    if (!pass.empty()) {
        makeCredentials(next, pass, false);
        memset(next.pass, 0, sizeof next.pass);
        memcpy(next.pass, pass.data(), pass.size());
    }
    if (!user.empty()) {
        memset(next.user, 0, sizeof next.user);
        memcpy(next.user, user.data(), user.size());
    }
    if (!storage::save("cred", next)) return false;
    cred_ = next;
    ESP_LOGI(TAG, "credentials changed");
    return true;
}

bool App::floLogin(std::string& user, std::string& pass) const {
    if (cred_.isDefault || !cred_.pass[0]) return false;
    user = username();
    pass = cred_.pass;
    return true;
}

static int64_t floorHour(int64_t t) { return t - ((t % 3600) + 3600) % 3600; }

FloPlanResult App::planFloRefresh(FloPlan& plan, int64_t& waitS) {
    const int64_t now = epoch_;
    if (now <= 0) return FloPlanResult::NoClock;
    if (flo_.anchor <= 0) return FloPlanResult::NoAnchor;
    plan = FloPlan{};
    plan.start = flo_.anchor;
    plan.end = floorHour(now - kFloLagS);
    if (plan.end <= plan.start) return FloPlanResult::NothingNew;
    if (plan.end - plan.start > kFloMaxSpanS) {
        plan.end = floorHour(plan.start + kFloMaxSpanS);
        plan.more = true;
    }
    const int64_t next = std::max(flo_.lastRefresh + kFloMinIntervalS, floRetryAt_);
    if (now < next) {
        waitS = next - now;
        return FloPlanResult::TooSoon;
    }
    floRetryAt_ = now + kFloRetryAfterFailureS;
    return FloPlanResult::Ready;
}

bool App::commitFloRefresh(const FloPlan& plan, const std::vector<flo::Sample>& samples, float& usedGal) {
    if (flo_.anchor != plan.start) return false;

    std::vector<capcorrect::Bucket> buckets;
    for (const flo::Sample& s : samples) {
        if (s.epoch > 0 && s.epoch < plan.end && s.epoch + 3600 > plan.start) {
            buckets.push_back({s.epoch, 3600, static_cast<float>(s.gallons)});
        }
    }

    std::vector<capcorrect::Point> pts;
    pts.reserve(cap_.count + kCapSamples);
    for (uint32_t i = 0; i < cap_.count; i++) {
        const CapSample& s = cap_.samples[(cap_.head + kCapSamples - cap_.count + i) % kCapSamples];
        pts.push_back({s.epoch, s.gallons});
    }
    usedGal = capcorrect::apply(pts, buckets, plan.start, plan.end);

    const size_t keep = std::min<size_t>(pts.size(), kCapSamples);
    cap_ = CapHistory{};
    for (size_t i = 0; i < keep; i++) {
        const capcorrect::Point& p = pts[pts.size() - keep + i];
        cap_.samples[i] = CapSample{(uint32_t)p.t, p.gal};
    }
    cap_.count = keep;
    cap_.head = keep % kCapSamples;
    storage::save("caphist", cap_);
    capDirty_ = false;

    ctl_.adjustCapacity(-usedGal);
    flo_.anchor = plan.end;
    flo_.lastRefresh = epoch_;
    storage::save("flo", flo_);
    savePersisted();
    ESP_LOGI(TAG, "flo correction applied: %.1f gal used through %lld", usedGal, (long long)plan.end);
    return true;
}

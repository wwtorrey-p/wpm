#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "controller.h"
#include "flo.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

constexpr int kMaxEvents = 100;
constexpr int kHistoryDays = 90;
constexpr int kCapSamples = 256;

struct DayBucket {
    uint32_t day;  // days since epoch (UTC); 0 = unused
    float gallons;
};

struct CapSample {
    uint32_t epoch;
    float gallons;
};

struct CapHistory {
    uint32_t head;  // next write slot
    uint32_t count;
    CapSample samples[kCapSamples];
};

constexpr size_t kMaxUserLen = 64;
constexpr size_t kMaxPassLen = 72;

struct Credentials {
    uint8_t salt[16];
    uint8_t hash[32];
    uint8_t isDefault;
    // Added later; a blob saved by an older build leaves these zeroed (user then means "admin").
    // The plain-text password is kept so the same login can be used for the Moen Flo cloud.
    char user[kMaxUserLen + 1];
    char pass[kMaxPassLen + 1];
};

// Flo usage correction state. anchor is the time up to which the capacity estimate is settled: the last capacity
// reset (low/high snap) or the end of the last Flo correction, whichever is later. 0 = no reset seen yet.
struct FloState {
    int64_t anchor;
    int64_t lastRefresh;
};

struct LevelState {
    float capacity;
    CapHistory history;
    FloState flo;
};

// Flo's hourly data lags a little, so only hours that ended at least this long ago are applied.
constexpr int64_t kFloLagS = 1800;
// Longest span fetched in one refresh (hourly data); a longer gap is caught up over several refreshes.
constexpr int64_t kFloMaxSpanS = 7 * 86400;

struct FloPlan {
    int64_t start = 0, end = 0;
    bool more = false;  // the gap was longer than one refresh covers
};

enum class FloPlanResult { Ready, NoClock, NoAnchor, NothingNew };
enum class FloCommitResult { Applied, AnchorChanged, StorageError };

struct EventLog {
    uint32_t head;  // next write slot
    uint32_t count;
    pump::Event events[kMaxEvents];
};

class App {
public:
    void init();
    // Called every 100 ms from the control task.
    void tick(bool run, bool alarm);

    void lock() { xSemaphoreTake(mutex_, portMAX_DELAY); }
    void unlock() { xSemaphoreGive(mutex_); }

    // Callers must hold the lock.
    pump::Controller& controller() { return ctl_; }
    const EventLog& events() const { return log_; }
    const CapHistory& capacityHistory() const { return cap_; }
    const std::array<DayBucket, kHistoryDays>& history() const { return days_; }
    int64_t epoch() const { return epoch_; }
    bool defaultPassword() const { return cred_.isDefault; }
    void applySettings(const pump::Settings& s);
    float addWater();
    void clearLog();
    bool checkPassword(const std::string& user, const std::string& pass) const;
    // Empty user or pass keeps the current value. Returns false if the new values are not acceptable.
    bool setCredentials(const std::string& user, const std::string& pass);
    std::string username() const;
    // Fills user/pass with the login for the Flo cloud; false if the password is not known (still the default,
    // or set by an older build before it was kept).
    bool floLogin(std::string& user, std::string& pass) const;
    int64_t floAnchor() const { return flo_.anchor; }
    int64_t floLastRefresh() const { return flo_.lastRefresh; }
    const std::string& floStatus() const { return floStatus_; }
    void setFloStatus(const std::string& status) { floStatus_ = status; }
    FloPlanResult planFloRefresh(FloPlan& plan) const;
    FloCommitResult commitFloRefresh(const FloPlan& plan, const std::vector<flo::Sample>& samples, float& usedGal);

private:
    void onEvent(const pump::Event& e);
    void addRunTime(uint32_t seconds);
    void savePersisted();
    bool saveLevel();
    void pushCapacity(int64_t epoch, float gallons);

    SemaphoreHandle_t mutex_ = xSemaphoreCreateMutex();
    pump::Controller ctl_{[this](const pump::Event& e) { onEvent(e); }};
    EventLog log_{};
    CapHistory cap_{};
    float capBefore_ = 0;       // capacity just before the current tick, for step-shaped corrections
    int64_t lastCapEpoch_ = 0;  // time of the newest sample
    bool capDirty_ = false;
    std::array<DayBucket, kHistoryDays> days_{};
    Credentials cred_{};
    FloState flo_{};
    std::string floStatus_ = "Waiting for automatic Flo update";
    int64_t epoch_ = 0;
    uint32_t tickCount_ = 0;
    bool historyDirty_ = false;
};

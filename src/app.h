#pragma once
#include <array>
#include <cstdint>
#include <string>

#include "controller.h"
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

struct Credentials {
    uint8_t salt[16];
    uint8_t hash[32];
    uint8_t isDefault;
};

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
    void clearLog();
    bool checkPassword(const std::string& user, const std::string& pass) const;
    bool setPassword(const std::string& pass);

private:
    void onEvent(const pump::Event& e);
    void addRunTime(uint32_t seconds);
    void savePersisted();
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
    int64_t epoch_ = 0;
    uint32_t tickCount_ = 0;
    bool historyDirty_ = false;
};

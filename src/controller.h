#pragma once
#include <cstdint>
#include <functional>

namespace pump {

enum class State : uint8_t { Disabled, Backoff, Settling, Armed };
enum class EventType : uint8_t { Boot, Demand, RunEnd, Fault, Recovery, Disabled, Enabled, Reset };

struct Event {
    int64_t epoch;
    EventType type;
    uint32_t arg;  // RunEnd: run seconds this cycle; Fault/Recovery: back-off seconds
};

struct Settings {
    uint32_t initialBackoffS = 48 * 3600;
    uint32_t maxBackoffS = 30 * 86400;
    uint32_t graceS = 60;
    float gpm = 5.0f;
    float targetGalPerDay = 50.0f;
};

struct Persisted {
    uint32_t nextBackoffS = 48 * 3600;  // duration applied to the next fault
    uint32_t curBackoffS = 0;           // duration of the back-off in progress
    int64_t backoffEndEpoch = 0;        // 0 while active means "time not known yet"
    uint32_t faultCount = 0;
    uint32_t totalRunS = 0;
    uint8_t backoffActive = 0;
    uint8_t enabled = 1;
};

struct Inputs {
    uint32_t nowMs;
    int64_t epoch;  // 0 when wall-clock time is unknown
    bool run;       // drive Run relay closed
    bool alarm;     // drive Alarm relay active
};

struct Status {
    State state;
    bool auxClosed;
    bool running;
    bool alarm;
    float cycleGallons;
    Persisted p;
};

class Controller {
public:
    using EventFn = std::function<void(const Event&)>;

    explicit Controller(EventFn onEvent = nullptr) : onEvent_(std::move(onEvent)) {}

    void begin(const Settings& s, const Persisted& p, uint32_t nowMs, int64_t epoch);
    void tick(const Inputs& in);

    void setEnabled(bool enabled);
    void resetBackoff();    // back to the initial duration and resume
    void endBackoffNow();   // resume, keeping the escalated duration
    void setSettings(const Settings& s);

    // Whole seconds of pump run time since the last call.
    uint32_t takeRunSeconds();
    bool takeDirty() { bool d = dirty_; dirty_ = false; return d; }

    bool auxClosed() const { return auxClosed_; }
    Status status() const;
    const Settings& settings() const { return s_; }

private:
    struct Debounce {
        bool stable = false, raw = false;
        uint32_t sinceMs = 0;
        bool update(bool in, uint32_t now, uint32_t holdMs);
    };

    void emit(EventType t, uint32_t arg);
    void enterSettling();
    void fault();
    void cleanStop();
    void resumeAfterBackoff();

    static constexpr uint32_t kRunDebounceMs = 500;
    static constexpr uint32_t kAlarmDebounceMs = 2000;
    // Drive may drop Run slightly before raising Alarm.
    static constexpr uint32_t kStopConfirmMs = 3000;

    EventFn onEvent_;
    Settings s_;
    Persisted p_;
    State state_ = State::Disabled;
    bool auxClosed_ = false;
    bool dirty_ = false;

    Debounce run_, alarm_;
    bool runOn_ = false;
    bool stopPending_ = false;
    uint32_t stopAtMs_ = 0;
    uint32_t settleStartMs_ = 0;
    uint32_t lastMs_ = 0;
    uint32_t cycleMs_ = 0;
    uint32_t unreportedMs_ = 0;
    int64_t epoch_ = 0;
};

}  // namespace pump

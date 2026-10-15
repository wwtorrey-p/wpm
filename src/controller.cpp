#include "controller.h"

#include <algorithm>

namespace pump {

bool Controller::Debounce::update(bool in, uint32_t now, uint32_t holdMs) {
    if (in != raw) {
        raw = in;
        sinceMs = now;
    }
    if (raw != stable && (now - sinceMs) >= holdMs) stable = raw;
    return stable;
}

void Controller::emit(EventType t, uint32_t arg) {
    dirty_ = true;
    if (onEvent_) onEvent_(Event{epoch_, t, arg});
}

void Controller::begin(const Settings& s, const Persisted& p, uint32_t nowMs, int64_t epoch) {
    s_ = s;
    p_ = p;
    lastMs_ = nowMs;
    epoch_ = epoch;
    if (p_.nextBackoffS < s_.initialBackoffS) p_.nextBackoffS = s_.initialBackoffS;
    emit(EventType::Boot, 0);
    if (!p_.enabled) {
        state_ = State::Disabled;
        auxClosed_ = false;
    } else if (p_.backoffActive) {
        state_ = State::Backoff;
        auxClosed_ = false;
    } else {
        enterSettling();
    }
}

void Controller::enterSettling() {
    state_ = State::Settling;
    auxClosed_ = true;
    settleStartMs_ = lastMs_;
    stopPending_ = false;
}

void Controller::fault() {
    uint32_t dur = std::clamp(p_.nextBackoffS, s_.initialBackoffS, std::max(s_.maxBackoffS, s_.initialBackoffS));
    p_.curBackoffS = dur;
    uint64_t doubled = static_cast<uint64_t>(dur) * 2;
    p_.nextBackoffS = static_cast<uint32_t>(std::min<uint64_t>(doubled, std::max(s_.maxBackoffS, s_.initialBackoffS)));
    p_.backoffEndEpoch = epoch_ > 0 ? epoch_ + dur : 0;
    p_.backoffActive = 1;
    p_.faultCount++;
    state_ = State::Backoff;
    auxClosed_ = false;
    stopPending_ = false;
    emit(EventType::Fault, dur);
}

void Controller::cleanStop() {
    stopPending_ = false;
    emit(EventType::RunEnd, cycleMs_ / 1000);
    cycleMs_ = 0;
    p_.nextBackoffS = s_.initialBackoffS;
}

void Controller::resumeAfterBackoff() {
    p_.backoffActive = 0;
    p_.backoffEndEpoch = 0;
    p_.curBackoffS = 0;
    dirty_ = true;
    if (p_.enabled) enterSettling();
}

void Controller::tick(const Inputs& in) {
    const uint32_t now = in.nowMs;
    const uint32_t dt = now - lastMs_;
    lastMs_ = now;
    epoch_ = in.epoch;

    const bool run = run_.update(in.run, now, kRunDebounceMs);
    const bool alarm = alarm_.update(in.alarm, now, kAlarmDebounceMs);

    if (runOn_) {
        cycleMs_ += dt;
        unreportedMs_ += dt;
    }
    if (run && !runOn_) {
        runOn_ = true;
        stopPending_ = false;
        emit(EventType::Demand, 0);
    } else if (!run && runOn_) {
        runOn_ = false;
        stopPending_ = true;
        stopAtMs_ = now;
    }

    switch (state_) {
        case State::Disabled:
            break;
        case State::Backoff:
            if (p_.backoffEndEpoch == 0 && epoch_ > 0) {
                p_.backoffEndEpoch = epoch_ + p_.curBackoffS;
                dirty_ = true;
            }
            if (p_.backoffEndEpoch != 0 && epoch_ >= p_.backoffEndEpoch) {
                emit(EventType::Recovery, p_.curBackoffS);
                resumeAfterBackoff();
            }
            break;
        case State::Settling:
            // Alarm stays asserted (F26) while Aux is open, so ignore it until the drive restarts.
            if ((now - settleStartMs_) >= s_.graceS * 1000u) state_ = State::Armed;
            break;
        case State::Armed:
            break;
    }

    if (state_ == State::Armed) {
        if (alarm) {
            fault();
        } else if (stopPending_ && (now - stopAtMs_) >= kStopConfirmMs) {
            cleanStop();
        }
    }
}

void Controller::setEnabled(bool enabled) {
    if (enabled == (p_.enabled != 0)) return;
    p_.enabled = enabled;
    if (!enabled) {
        state_ = State::Disabled;
        auxClosed_ = false;
        emit(EventType::Disabled, 0);
    } else {
        emit(EventType::Enabled, 0);
        if (p_.backoffActive) {
            state_ = State::Backoff;
        } else {
            enterSettling();
        }
    }
}

void Controller::resetBackoff() {
    p_.nextBackoffS = s_.initialBackoffS;
    emit(EventType::Reset, 0);
    if (p_.backoffActive) resumeAfterBackoff();
}

void Controller::endBackoffNow() {
    if (!p_.backoffActive) return;
    emit(EventType::Recovery, 0);
    resumeAfterBackoff();
}

void Controller::setSettings(const Settings& s) {
    s_ = s;
    if (!p_.backoffActive) p_.nextBackoffS = s_.initialBackoffS;
    dirty_ = true;
}

uint32_t Controller::takeRunSeconds() {
    uint32_t s = unreportedMs_ / 1000;
    unreportedMs_ -= s * 1000;
    p_.totalRunS += s;
    return s;
}

Status Controller::status() const {
    return Status{state_, auxClosed_, runOn_, alarm_.stable,
                  static_cast<float>(cycleMs_) / 60000.0f * s_.gpm, p_};
}

}  // namespace pump

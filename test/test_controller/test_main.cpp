#include <unity.h>

#include <vector>

#include "controller.h"

using namespace pump;

namespace {
constexpr int64_t kT0 = 1'800'000'000;

struct Rig {
    std::vector<Event> events;
    Controller c{[this](const Event& e) { events.push_back(e); }};
    uint32_t ms = 0;
    int64_t epoch = kT0;
    bool run = false, alarm = false;

    explicit Rig(float capacity = -1.0f) {
        if (capacity >= 0) c.restoreCapacity(capacity);
        c.begin(Settings{}, Persisted{}, ms, epoch);
    }

    void advance(uint32_t seconds) {
        for (uint32_t i = 0; i < seconds * 10; i++) {
            ms += 100;
            epoch = kT0 + ms / 1000;
            c.tick(Inputs{ms, epoch, run, alarm});
        }
    }
    int count(EventType t) const {
        int n = 0;
        for (auto& e : events) n += e.type == t;
        return n;
    }
};
}  // namespace

void test_fault_opens_aux_and_doubles() {
    Rig r;
    r.advance(60);
    TEST_ASSERT_TRUE(r.c.auxClosed());
    r.alarm = true;
    r.advance(5);
    TEST_ASSERT_FALSE(r.c.auxClosed());
    TEST_ASSERT_EQUAL_UINT32(48 * 3600, r.c.status().p.curBackoffS);
    TEST_ASSERT_EQUAL_UINT32(96 * 3600, r.c.status().p.nextBackoffS);

    r.alarm = false;
    r.advance(48 * 3600 + 5);
    TEST_ASSERT_TRUE(r.c.auxClosed());
    TEST_ASSERT_EQUAL_INT(1, r.count(EventType::Recovery));

    // Alarm still present after grace: second fault doubles.
    r.alarm = true;
    r.advance(65);
    TEST_ASSERT_FALSE(r.c.auxClosed());
    TEST_ASSERT_EQUAL_UINT32(96 * 3600, r.c.status().p.curBackoffS);
}

void test_alarm_ignored_during_grace() {
    Rig r;
    r.alarm = true;
    r.advance(30);
    TEST_ASSERT_TRUE(r.c.auxClosed());
    TEST_ASSERT_EQUAL_INT(0, r.count(EventType::Fault));
}

void test_clean_stop_resets_backoff() {
    Rig r;
    r.advance(60);
    r.alarm = true;
    r.advance(5);
    r.alarm = false;
    r.advance(48 * 3600 + 5);
    r.advance(65);
    r.run = true;
    r.advance(120);
    r.run = false;
    r.advance(10);
    TEST_ASSERT_EQUAL_INT(1, r.count(EventType::RunEnd));
    TEST_ASSERT_EQUAL_UINT32(48 * 3600, r.c.status().p.nextBackoffS);
}

void test_stop_with_alarm_is_fault() {
    Rig r;
    r.advance(60);
    r.run = true;
    r.advance(30);
    r.run = false;
    r.advance(1);
    r.alarm = true;
    r.advance(10);
    TEST_ASSERT_EQUAL_INT(1, r.count(EventType::Fault));
    TEST_ASSERT_EQUAL_INT(1, r.count(EventType::RunEnd));  // every stop is logged, faulted or not
}

void test_cap() {
    Rig r;
    Settings s;
    s.maxBackoffS = 100 * 3600;
    r.c.setSettings(s);
    for (int i = 0; i < 3; i++) {
        r.advance(65);
        r.alarm = true;
        r.advance(5);
        r.alarm = false;
        r.c.endBackoffNow();
    }
    TEST_ASSERT_EQUAL_UINT32(100 * 3600, r.c.status().p.nextBackoffS);
}

void test_manual_disable_and_reset() {
    Rig r;
    r.advance(60);
    r.alarm = true;
    r.advance(5);
    r.alarm = false;
    r.c.setEnabled(false);
    TEST_ASSERT_FALSE(r.c.auxClosed());
    r.c.setEnabled(true);
    TEST_ASSERT_FALSE(r.c.auxClosed());  // back-off still active
    r.c.resetBackoff();
    TEST_ASSERT_TRUE(r.c.auxClosed());
    TEST_ASSERT_EQUAL_UINT32(48 * 3600, r.c.status().p.nextBackoffS);
}

void test_backoff_waits_for_time() {
    Controller c;
    Persisted p;
    p.backoffActive = 1;
    p.curBackoffS = 3600;
    c.begin(Settings{}, p, 0, 0);
    c.tick(Inputs{10000, 0, false, false});
    TEST_ASSERT_FALSE(c.auxClosed());
    c.tick(Inputs{20000, kT0, false, false});
    TEST_ASSERT_FALSE(c.auxClosed());
    TEST_ASSERT_EQUAL_INT64(kT0 + 3600, c.status().p.backoffEndEpoch);
}

void test_demand_resets_cycle_gallons() {
    Rig r;
    r.advance(65);
    r.run = true;
    r.advance(60);
    r.alarm = true;  // fault ends the call without a clean stop
    r.advance(3);
    r.run = false;  // the drive stops once Aux opens
    r.advance(2);
    r.alarm = false;
    r.c.endBackoffNow();
    r.advance(65);
    TEST_ASSERT_TRUE(r.c.status().cycleGallons > 4.0f);
    r.run = true;
    r.advance(3);
    TEST_ASSERT_TRUE(r.c.status().cycleGallons < 1.0f);
}

void test_fault_while_running_is_immediate() {
    Rig r;
    r.advance(5);  // still inside the settle delay
    r.run = true;
    r.advance(5);
    r.alarm = true;
    r.advance(2);
    TEST_ASSERT_EQUAL_INT(1, r.count(EventType::Fault));
    TEST_ASSERT_FALSE(r.c.auxClosed());
}

void test_stale_alarm_after_restart_is_ignored() {
    Rig r;
    r.alarm = true;  // F26 still releasing when the pump starts
    r.advance(10);
    r.run = true;
    r.advance(1);
    r.alarm = false;
    r.advance(5);
    TEST_ASSERT_EQUAL_INT(0, r.count(EventType::Fault));
}

void test_capacity_low_on_start_and_high_on_clean_stop() {
    Rig r(1555);
    r.advance(65);
    r.run = true;
    r.advance(300);  // 5 min at 5 gpm adds about 25 gal to the low level
    float c = r.c.status().capacityGal;
    TEST_ASSERT_TRUE(c > 1420.0f && c < 1430.0f);
    r.run = false;
    r.advance(5);
    TEST_ASSERT_EQUAL_FLOAT(1700.0f, r.c.status().capacityGal);
}

void test_capacity_not_reset_when_starting_while_settling() {
    Rig r(1555);
    r.advance(5);  // still settling
    r.run = true;
    r.advance(5);
    float c = r.c.status().capacityGal;
    TEST_ASSERT_TRUE(c >= 1555.0f && c < 1556.0f);
}

void test_capacity_not_reset_by_fault() {
    Rig r(1555);
    r.advance(65);
    r.alarm = true;
    r.advance(3);
    TEST_ASSERT_EQUAL_FLOAT(1555.0f, r.c.status().capacityGal);
}

void test_capacity_is_capped() {
    Rig r;
    Settings s;
    s.lowLevelGal = 1995.0f;
    s.highLevelGal = 1999.0f;
    r.c.setSettings(s);
    r.advance(65);
    r.run = true;
    r.advance(300);
    TEST_ASSERT_EQUAL_FLOAT(kMaxCapacityGal, r.c.status().capacityGal);
}

void test_pump_off_event_records_runtime_and_gallons() {
    Rig r;
    r.advance(65);
    r.run = true;
    r.advance(120);
    r.run = false;
    r.advance(5);
    const Event* off = nullptr;
    for (const Event& e : r.events) {
        if (e.type == EventType::RunEnd) off = &e;
    }
    TEST_ASSERT_NOT_NULL(off);
    TEST_ASSERT_TRUE(off->arg >= 118 && off->arg <= 121);
    TEST_ASSERT_TRUE(off->gallons > 9.5f && off->gallons < 10.5f);
}

void test_last_run_numbers_stay_until_next_start() {
    Rig r;
    r.advance(65);
    r.run = true;
    r.advance(120);
    r.run = false;
    r.advance(30);  // well past the clean-stop confirmation
    Status st = r.c.status();
    TEST_ASSERT_TRUE(st.cycleRunS >= 118 && st.cycleRunS <= 121);
    TEST_ASSERT_TRUE(st.cycleGallons > 9.5f);
    r.run = true;
    r.advance(3);
    TEST_ASSERT_TRUE(r.c.status().cycleRunS < 5);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_fault_opens_aux_and_doubles);
    RUN_TEST(test_alarm_ignored_during_grace);
    RUN_TEST(test_clean_stop_resets_backoff);
    RUN_TEST(test_stop_with_alarm_is_fault);
    RUN_TEST(test_cap);
    RUN_TEST(test_manual_disable_and_reset);
    RUN_TEST(test_backoff_waits_for_time);
    RUN_TEST(test_demand_resets_cycle_gallons);
    RUN_TEST(test_fault_while_running_is_immediate);
    RUN_TEST(test_stale_alarm_after_restart_is_ignored);
    RUN_TEST(test_capacity_low_on_start_and_high_on_clean_stop);
    RUN_TEST(test_capacity_not_reset_when_starting_while_settling);
    RUN_TEST(test_capacity_not_reset_by_fault);
    RUN_TEST(test_capacity_is_capped);
    RUN_TEST(test_pump_off_event_records_runtime_and_gallons);
    RUN_TEST(test_last_run_numbers_stay_until_next_start);
    return UNITY_END();
}

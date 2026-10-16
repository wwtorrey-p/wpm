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

    Rig() { c.begin(Settings{}, Persisted{}, ms, epoch); }

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
    TEST_ASSERT_EQUAL_INT(0, r.count(EventType::RunEnd));
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
    return UNITY_END();
}

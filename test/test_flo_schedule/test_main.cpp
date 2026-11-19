#include <unity.h>

#include "flo_schedule.h"

void setUp() {}
void tearDown() {}

void test_daily_schedule_and_retry() {
    constexpr int64_t now = 1'800'000'000;
    constexpr int64_t maxSpan = 7 * 86400;
    TEST_ASSERT_FALSE(flo_schedule::due(0, 0, 0, maxSpan, 0, false));
    TEST_ASSERT_TRUE(flo_schedule::due(now, 0, now - 3600, maxSpan, 0, false));
    TEST_ASSERT_FALSE(flo_schedule::due(now, now - 86399, now - 3600, maxSpan, 0, false));
    TEST_ASSERT_TRUE(flo_schedule::due(now, now - 86400, now - 3600, maxSpan, 0, false));
    TEST_ASSERT_FALSE(flo_schedule::due(now, 0, now - 3600, maxSpan, now + 1, true));
    TEST_ASSERT_TRUE(flo_schedule::due(now, now - 1, now - 3600, maxSpan, now, true));
    TEST_ASSERT_TRUE(flo_schedule::due(now, now - 1, now - maxSpan - 1, maxSpan, 0, false));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_daily_schedule_and_retry);
    return UNITY_END();
}

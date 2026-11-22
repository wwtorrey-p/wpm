#include <unity.h>

#include "report_format.h"

void test_readable_fields_and_values() {
    TEST_ASSERT_EQUAL_STRING("Last Flo update", report::label("floLastRefresh").c_str());
    TEST_ASSERT_EQUAL_STRING("11/21/2026, 05:59:14 PM UTC", report::number("floLastRefresh", 1795283954).c_str());
    TEST_ASSERT_EQUAL_STRING("Unavailable", report::number("floLastRefresh", 0).c_str());
    TEST_ASSERT_EQUAL_STRING("Wi-Fi network", report::label("ssid").c_str());
    TEST_ASSERT_EQUAL_STRING("Pump rate", report::label("gpm").c_str());
    TEST_ASSERT_EQUAL_STRING("5 gal/min", report::number("gpm", 5).c_str());
    TEST_ASSERT_EQUAL_STRING("1d 02h 03m 04s", report::number("totalRunS", 93784).c_str());
    TEST_ASSERT_EQUAL_STRING("42.5 \xc2\xb0" "C", report::number("tempC", 42.5).c_str());
    TEST_ASSERT_EQUAL_STRING("Fault back-off", report::text("state", "backoff").c_str());
    TEST_ASSERT_EQUAL_STRING("Open", report::boolean("auxClosed", false).c_str());
}

void test_event_descriptions() {
    pump::Event run{0, pump::EventType::RunEnd, 120, 10.5f};
    TEST_ASSERT_EQUAL_STRING("Pump off - ran 0d 00h 02m 00s, 10.5 gal", report::event(run).c_str());
    pump::Event water{0, pump::EventType::WaterAdded, 14000, 1610};
    TEST_ASSERT_EQUAL_STRING("Water added - from 1400 gal to 1610 gal", report::event(water).c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_readable_fields_and_values);
    RUN_TEST(test_event_descriptions);
    return UNITY_END();
}

#include <unity.h>

#include <cmath>
#include <vector>

#include "capcorrect.h"

using namespace capcorrect;

namespace {
constexpr int64_t kH = 3600;
constexpr int64_t kT0 = 1'800'000'000 - (1'800'000'000 % 86400);  // a UTC midnight

bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

bool hasPoint(const std::vector<Point>& p, int64_t t, float gal) {
    for (const Point& q : p) {
        if (q.t == t && near(q.gal, gal)) return true;
    }
    return false;
}
}  // namespace

void setUp() {}
void tearDown() {}

void test_usage_prorates_partial_buckets() {
    std::vector<Bucket> b{{kT0, kH, 10}, {kT0 + kH, kH, 20}};
    TEST_ASSERT_TRUE(near(usageBetween(b, kT0, kT0 + 2 * kH), 30));
    TEST_ASSERT_TRUE(near(usageBetween(b, kT0 + kH / 2, kT0 + kH), 5));
    TEST_ASSERT_TRUE(near(usageBetween(b, kT0 + kH / 2, kT0 + kH + kH / 2), 15));
    TEST_ASSERT_TRUE(near(usageBetween(b, kT0 + 5 * kH, kT0 + 6 * kH), 0));
}

void test_value_at_interpolates_and_prefers_later_point_of_a_step() {
    std::vector<Point> p{{100, 1700}, {100, 1400}, {200, 1500}};
    TEST_ASSERT_TRUE(near(valueAt(p, 100), 1400));
    TEST_ASSERT_TRUE(near(valueAt(p, 150), 1450));
    TEST_ASSERT_TRUE(near(valueAt(p, 50), 1700));
    TEST_ASSERT_TRUE(near(valueAt(p, 900), 1500));
}

void test_apply_leaves_points_at_or_before_anchor() {
    std::vector<Point> p{{kT0, 1700}, {kT0, 1400}, {kT0 + 2 * kH, 1500}};
    std::vector<Bucket> b{{kT0, kH, 10}, {kT0 + kH, kH, 20}};
    float used = apply(p, b, kT0, kT0 + 2 * kH);
    TEST_ASSERT_TRUE(near(used, 30));
    TEST_ASSERT_TRUE(hasPoint(p, kT0, 1700));
    TEST_ASSERT_TRUE(hasPoint(p, kT0, 1400));
}

void test_apply_subtracts_cumulative_use_and_adds_end_point() {
    std::vector<Point> p{{kT0, 1400}, {kT0 + 2 * kH, 1500}, {kT0 + 5 * kH, 1530}};
    std::vector<Bucket> b{{kT0, kH, 10}, {kT0 + kH, kH, 20}};
    apply(p, b, kT0, kT0 + 3 * kH);
    TEST_ASSERT_TRUE(hasPoint(p, kT0 + 2 * kH, 1470));
    TEST_ASSERT_TRUE(hasPoint(p, kT0 + 5 * kH, 1500));
    TEST_ASSERT_TRUE(hasPoint(p, kT0 + 3 * kH, 1480));  // 1500 + 10 interpolated - 30
    for (size_t i = 1; i < p.size(); i++) TEST_ASSERT_TRUE(p[i - 1].t <= p[i].t);
}

void test_second_apply_from_new_anchor_does_not_recorrect_earlier_points() {
    std::vector<Point> p{{kT0, 1400}, {kT0 + 4 * kH, 1400}};
    std::vector<Bucket> b1{{kT0, kH, 10}, {kT0 + kH, kH, 10}};
    apply(p, b1, kT0, kT0 + 2 * kH);
    TEST_ASSERT_TRUE(hasPoint(p, kT0, 1400));
    TEST_ASSERT_TRUE(hasPoint(p, kT0 + 2 * kH, 1380));

    std::vector<Bucket> b2{{kT0 + 2 * kH, kH, 5}, {kT0 + 3 * kH, kH, 5}};
    apply(p, b2, kT0 + 2 * kH, kT0 + 4 * kH);
    TEST_ASSERT_TRUE(hasPoint(p, kT0, 1400));           // before the new anchor: untouched
    TEST_ASSERT_TRUE(hasPoint(p, kT0 + 2 * kH, 1380));  // at the new anchor: untouched
    TEST_ASSERT_TRUE(near(p.back().gal, 1370));         // carries both corrections (20, then 10)
}

void test_apply_adds_a_point_at_each_utc_midnight() {
    std::vector<Point> p{{kT0 + 20 * kH, 1400}, {kT0 + 80 * kH, 1400}};
    std::vector<Bucket> b{{kT0 + 24 * kH, kH, 40}};
    apply(p, b, kT0 + 20 * kH, kT0 + 75 * kH);
    int midnights = 0;
    for (const Point& q : p) midnights += (q.t % 86400 == 0 && q.t > kT0 + 20 * kH);
    TEST_ASSERT_EQUAL(3, midnights);  // +24 h, +48 h, +72 h
}

void test_apply_never_goes_below_zero() {
    std::vector<Point> p{{kT0, 10}, {kT0 + 2 * kH, 10}};
    std::vector<Bucket> b{{kT0, kH, 500}};
    apply(p, b, kT0, kT0 + kH);
    for (const Point& q : p) TEST_ASSERT_TRUE(q.gal >= 0);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_usage_prorates_partial_buckets);
    RUN_TEST(test_value_at_interpolates_and_prefers_later_point_of_a_step);
    RUN_TEST(test_apply_leaves_points_at_or_before_anchor);
    RUN_TEST(test_apply_subtracts_cumulative_use_and_adds_end_point);
    RUN_TEST(test_second_apply_from_new_anchor_does_not_recorrect_earlier_points);
    RUN_TEST(test_apply_adds_a_point_at_each_utc_midnight);
    RUN_TEST(test_apply_never_goes_below_zero);
    return UNITY_END();
}

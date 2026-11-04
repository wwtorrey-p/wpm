#pragma once
#include <cstdint>
#include <vector>

// Pure helpers for correcting the capacity history with measured water use (no ESP-IDF dependencies).
namespace capcorrect {

struct Point {
    int64_t t;
    float gal;
};

// One hourly bucket of measured use: [start, start + len).
struct Bucket {
    int64_t start;
    int64_t len;
    float gallons;
};

// Gallons used in [from, to), prorating buckets that only partly overlap the range.
float usageBetween(const std::vector<Bucket>& b, int64_t from, int64_t to);

// Capacity at time t, interpolated between points (flat before the first and after the last).
float valueAt(const std::vector<Point>& pts, int64_t t);

// Subtracts the use in (anchor, end] from every point after anchor and adds points at each UTC midnight in between
// and at end, so the drop shows on the chart. Points at or before anchor are left alone, which is what stops a point
// being corrected twice. Returns the total gallons removed.
float apply(std::vector<Point>& pts, const std::vector<Bucket>& b, int64_t anchor, int64_t end);

}  // namespace capcorrect

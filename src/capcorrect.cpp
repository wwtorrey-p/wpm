#include "capcorrect.h"

#include <algorithm>

namespace capcorrect {

float usageBetween(const std::vector<Bucket>& b, int64_t from, int64_t to) {
    float sum = 0;
    for (const Bucket& k : b) {
        const int64_t lo = std::max(from, k.start), hi = std::min(to, k.start + k.len);
        if (hi > lo && k.len > 0) sum += k.gallons * static_cast<float>(hi - lo) / static_cast<float>(k.len);
    }
    return sum;
}

float valueAt(const std::vector<Point>& pts, int64_t t) {
    if (pts.empty()) return 0;
    size_t j = 0;
    while (j < pts.size() && pts[j].t <= t) j++;
    if (j == 0) return pts.front().gal;
    if (j == pts.size()) return pts.back().gal;
    const Point &a = pts[j - 1], &c = pts[j];
    return a.gal + (c.gal - a.gal) * static_cast<float>(t - a.t) / static_cast<float>(c.t - a.t);
}

float apply(std::vector<Point>& pts, const std::vector<Bucket>& b, int64_t anchor, int64_t end) {
    if (end <= anchor) return 0;
    const float total = usageBetween(b, anchor, end);

    std::vector<Point> added;
    if (!pts.empty()) {
        constexpr int64_t kDay = 86400;
        for (int64_t t = (anchor / kDay + 1) * kDay; t < end; t += kDay) {
            added.push_back({t, std::max(0.0f, valueAt(pts, t) - usageBetween(b, anchor, t))});
        }
        added.push_back({end, std::max(0.0f, valueAt(pts, end) - total)});
    }
    for (Point& p : pts) {
        if (p.t > anchor) p.gal = std::max(0.0f, p.gal - usageBetween(b, anchor, std::min(p.t, end)));
    }
    pts.insert(pts.end(), added.begin(), added.end());
    // Stable, so the before/after pair of a step at one instant keeps its order.
    std::stable_sort(pts.begin(), pts.end(), [](const Point& x, const Point& y) { return x.t < y.t; });
    return total;
}

}  // namespace capcorrect

#pragma once

#include <ctime>
#include <string>
#include <vector>

namespace flo {

enum class Interval { Hourly, Daily, Monthly };

struct Sample {
    std::string time;  // ISO 8601 bucket start, as returned by Flo
    int64_t epoch = 0;  // the same instant as UTC seconds; 0 if it could not be parsed
    double gallons = 0;
};

struct Usage {
    double totalGallons = 0;
    std::vector<Sample> items;
};

// Fetches water consumption for the [start, end] UTC range from the Moen Flo cloud.
// Blocking (several HTTPS round trips): call from a task with a >= 8 KB stack, with Wi-Fi up and the clock set.
// If locationId is empty, the location whose nickname/address contains "home" (else the first) is used,
// and the chosen id is written back to locationId so later calls can skip the lookup.
// On failure returns false and sets err.
bool getUsage(const std::string& email, const std::string& password, std::string& locationId,
              time_t start, time_t end, Interval interval, Usage& out, std::string& err);

}  // namespace flo

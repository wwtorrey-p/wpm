#include "report_format.h"

#include <cstdio>
#include <cstring>
#include <ctime>

namespace report {
namespace {
enum class Format { Plain, Time, Duration, Gallons, Hours, Minutes, Seconds, Gpm, GalPerDay, Bytes, MHz, Celsius, Dbm };
struct Field { const char* key; const char* label; Format format; };

constexpr Field fields[] = {
    {"state", "Control state", Format::Plain}, {"enabled", "Pump control", Format::Plain},
    {"auxClosed", "Float gate", Format::Plain}, {"running", "Pump", Format::Plain},
    {"alarm", "Drive alarm relay", Format::Plain}, {"epoch", "Device time", Format::Time},
    {"backoffEnd", "Fault back-off ends", Format::Time}, {"runBackoffEnd", "Run-limit back-off ends", Format::Time},
    {"curBackoffS", "Current fault delay", Format::Duration}, {"nextBackoffS", "Next fault delay", Format::Duration},
    {"faultCount", "Fault count", Format::Plain}, {"totalRunS", "Total run time", Format::Duration},
    {"cycleGallons", "Pumped this run", Format::Gallons}, {"cycleRunS", "Run time", Format::Duration},
    {"capacityGal", "Estimated capacity", Format::Gallons}, {"maxCapacityGal", "Maximum capacity", Format::Gallons},
    {"defaultPassword", "Default password in use", Format::Plain}, {"floAnchor", "Flo correction anchor", Format::Time},
    {"floLastRefresh", "Last Flo update", Format::Time}, {"floStatus", "Flo update status", Format::Plain},
    {"certIp", "Certificate IP address", Format::Plain}, {"username", "Username", Format::Plain},
    {"version", "Firmware version", Format::Plain}, {"built", "Firmware build time", Format::Plain},
    {"initialBackoffH", "Initial fault delay", Format::Hours}, {"maxBackoffH", "Maximum fault delay", Format::Hours},
    {"graceS", "Restart check delay", Format::Seconds}, {"gpm", "Pump rate", Format::Gpm},
    {"targetGalPerDay", "Production target", Format::GalPerDay}, {"lowLevelGal", "Low capacity level", Format::Gallons},
    {"highLevelGal", "High capacity level", Format::Gallons}, {"maxRunM", "Maximum run time", Format::Minutes},
    {"runBackoffM", "Run-limit delay", Format::Minutes}, {"waterAddGal", "Water added per action", Format::Gallons},
    {"connected", "Connection", Format::Plain}, {"ip", "IP address", Format::Plain},
    {"netmask", "Subnet mask", Format::Plain}, {"gateway", "Gateway", Format::Plain},
    {"dns", "DNS", Format::Plain}, {"hostname", "Hostname", Format::Plain}, {"mac", "MAC address", Format::Plain},
    {"ssid", "Wi-Fi network", Format::Plain}, {"rssi", "Signal strength", Format::Dbm},
    {"channel", "Wi-Fi channel", Format::Plain}, {"uptimeS", "Uptime", Format::Duration},
    {"resetReason", "Last reset reason", Format::Plain}, {"chip", "Chip model", Format::Plain},
    {"chipRevision", "Chip revision", Format::Plain}, {"cores", "CPU cores", Format::Plain},
    {"cpuMHz", "CPU speed", Format::MHz}, {"heapFree", "Free RAM", Format::Bytes},
    {"heapTotal", "Total RAM", Format::Bytes}, {"heapMinFree", "Lowest free RAM since boot", Format::Bytes},
    {"heapLargestBlock", "Largest free RAM block", Format::Bytes}, {"psramTotal", "Total PSRAM", Format::Bytes},
    {"psramFree", "Free PSRAM", Format::Bytes}, {"httpStackFree", "Web server stack low-water mark", Format::Bytes},
    {"flashSize", "Flash size", Format::Bytes}, {"otaSlot", "Running firmware slot", Format::Plain},
    {"otaSlotSize", "Firmware slot size", Format::Bytes}, {"idfVersion", "ESP-IDF version", Format::Plain},
    {"nvsUsed", "Settings storage entries used", Format::Plain}, {"nvsTotal", "Settings storage entries total", Format::Plain},
    {"tempC", "Chip temperature", Format::Celsius}, {"wifiDisconnects", "Wi-Fi disconnects since boot", Format::Plain},
    {"wifiLastReason", "Last Wi-Fi disconnect reason", Format::Plain},
};

const Field* find(const char* key) {
    for (const Field& field : fields) if (strcmp(field.key, key) == 0) return &field;
    return nullptr;
}

std::string value(double n, const char* suffix) {
    char buf[64];
    snprintf(buf, sizeof buf, "%.10g%s", n, suffix);
    return buf;
}

std::string measurement(double n, const char* suffix) {
    char buf[64];
    snprintf(buf, sizeof buf, "%.6g%s", n, suffix);
    return buf;
}

std::string duration(uint64_t seconds) {
    char buf[80];
    snprintf(buf, sizeof buf, "%llud %02lluh %02llum %02llus",
             static_cast<unsigned long long>(seconds / 86400),
             static_cast<unsigned long long>(seconds / 3600 % 24),
             static_cast<unsigned long long>(seconds / 60 % 60),
             static_cast<unsigned long long>(seconds % 60));
    return buf;
}
}

std::string label(const char* key) {
    if (const Field* field = find(key)) return field->label;
    std::string out;
    for (const char* p = key; *p; ++p) {
        if (*p >= 'A' && *p <= 'Z') out += ' ';
        out += p == key && *p >= 'a' && *p <= 'z' ? static_cast<char>(*p - 'a' + 'A') : *p;
    }
    return out;
}

std::string time(int64_t epoch) {
    if (epoch <= 0) return "Unavailable";
    time_t t = static_cast<time_t>(epoch);
    struct tm utc = {};
    char buf[40];
#ifdef _WIN32
    if (gmtime_s(&utc, &t) != 0) return "Unavailable";
#else
    if (!gmtime_r(&t, &utc)) return "Unavailable";
#endif
    if (!strftime(buf, sizeof buf, "%m/%d/%Y, %I:%M:%S %p UTC", &utc)) return "Unavailable";
    return buf;
}

std::string number(const char* key, double n) {
    const Field* field = find(key);
    switch (field ? field->format : Format::Plain) {
        case Format::Time: return time(static_cast<int64_t>(n));
        case Format::Duration: return duration(static_cast<uint64_t>(n));
        case Format::Gallons: return measurement(n, " gal");
        case Format::Hours: return value(n, " h");
        case Format::Minutes: return value(n, " min");
        case Format::Seconds: return value(n, " sec");
        case Format::Gpm: return measurement(n, " gal/min");
        case Format::GalPerDay: return measurement(n, " gal/day");
        case Format::Bytes: return value(n, " bytes");
        case Format::MHz: return value(n, " MHz");
        case Format::Celsius: return measurement(n, " \xc2\xb0" "C");
        case Format::Dbm: return value(n, " dBm");
        default: return value(n, "");
    }
}

std::string boolean(const char* key, bool b) {
    if (strcmp(key, "enabled") == 0) return b ? "Enabled" : "Disabled";
    if (strcmp(key, "auxClosed") == 0) return b ? "Closed" : "Open";
    if (strcmp(key, "running") == 0) return b ? "Running" : "Off";
    if (strcmp(key, "alarm") == 0) return b ? "Active" : "Inactive";
    if (strcmp(key, "connected") == 0) return b ? "Connected" : "Not connected";
    return b ? "Yes" : "No";
}

std::string text(const char* key, const std::string& text) {
    if (strcmp(key, "state") == 0) {
        if (text == "armed") return "Armed";
        if (text == "settling") return "Settling";
        if (text == "backoff") return "Fault back-off";
        if (text == "run_backoff") return "Run-limit back-off";
        if (text == "disabled") return "Disabled";
    }
    return text.empty() ? "Unavailable" : text;
}

std::string event(const pump::Event& e) {
    switch (e.type) {
        case pump::EventType::Boot: return "Controller started";
        case pump::EventType::Demand: return "Pump on";
        case pump::EventType::RunEnd: return "Pump off - ran " + duration(e.arg) + ", " + measurement(e.gallons, " gal");
        case pump::EventType::Fault: return "Fault - back-off " + duration(e.arg);
        case pump::EventType::Recovery: return "Fault back-off ended";
        case pump::EventType::RunLimit: return "Maximum run time reached - back-off " + duration(e.arg);
        case pump::EventType::RunRecovery: return "Run-limit back-off ended";
        case pump::EventType::Disabled: return "Pump control disabled";
        case pump::EventType::Enabled: return "Pump control enabled";
        case pump::EventType::Reset: return "Back-off reset";
        case pump::EventType::CapacityReset:
            return "Capacity reset - from " + value(e.arg / 10.0, " gal to ") + measurement(e.gallons, " gal");
        case pump::EventType::WaterAdded:
            return "Water added - from " + value(e.arg / 10.0, " gal to ") + measurement(e.gallons, " gal");
    }
    return "Unknown event";
}

}

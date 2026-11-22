#pragma once

#include <cstdint>
#include <string>

#include "controller.h"

namespace report {

std::string label(const char* key);
std::string number(const char* key, double value);
std::string boolean(const char* key, bool value);
std::string text(const char* key, const std::string& value);
std::string time(int64_t epoch);
std::string event(const pump::Event& event);

}

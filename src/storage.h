#pragma once
#include <cstddef>
#include <cstring>

#include "nvs.h"

namespace storage {

inline bool loadBlob(const char* key, void* out, size_t size) {
    nvs_handle_t h;
    if (nvs_open("pump", NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = size;
    bool ok = nvs_get_blob(h, key, out, &len) == ESP_OK && len == size;
    nvs_close(h);
    return ok;
}

inline bool saveBlob(const char* key, const void* data, size_t size) {
    nvs_handle_t h;
    if (nvs_open("pump", NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_blob(h, key, data, size) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

template <typename T>
bool load(const char* key, T& v) { return loadBlob(key, &v, sizeof(T)); }

template <typename T>
bool save(const char* key, const T& v) { return saveBlob(key, &v, sizeof(T)); }

}  // namespace storage

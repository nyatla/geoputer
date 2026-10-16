#pragma once

#include <cstddef>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// Host NVS adapter: persistent namespace/key bytes and explicit backend faults.
class Preferences {
public:
    inline static std::map<std::string, std::vector<unsigned char>> records;
    inline static bool fail_open = false, fail_write = false, fail_read = false;

    bool begin(const char* name, bool) { name_ = std::string(name) + "/"; return !fail_open; }
    bool isKey(const char* key) { return records.count(name_ + key) != 0; }
    size_t getBytesLength(const char* key) { return isKey(key) ? records[name_ + key].size() : 0; }
    size_t getBytes(const char* key, void* destination, size_t size) {
        if (fail_read || !isKey(key) || getBytesLength(key) > size) return 0;
        const auto& source = records[name_ + key];
        std::memcpy(destination, source.data(), source.size());
        return source.size();
    }
    size_t putBytes(const char* key, const void* source, size_t size) {
        if (fail_write) return 0;
        const auto* bytes = static_cast<const unsigned char*>(source);
        records[name_ + key] = {bytes, bytes + size};
        return size;
    }
private:
    std::string name_;
};

#pragma once

#include <cstddef>
#include <cstring>
#include <string_view>

namespace geoputer {

// An SD directory name, not a display title. Empty means no selection.
struct DatasetId {
    static constexpr size_t Capacity = 16;
    char name[Capacity];

    static bool valid(std::string_view value, bool allow_empty = false) {
        if (value.empty()) return allow_empty;
        if (value.size() >= Capacity || value.front() < 'a' || value.front() > 'z') return false;
        bool hyphen = false;
        for (const char ch : value) {
            if (ch == '-') { if (hyphen) return false; hyphen = true; }
            else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) hyphen = false;
            else return false;
        }
        return !hyphen;
    }
    bool assign(std::string_view value) {
        if (!valid(value, true)) return false;
        char staged[Capacity]{};
        if(!value.empty())std::memcpy(staged, value.data(), value.size());
        std::memset(name, 0, sizeof(name));
        std::memcpy(name, staged, sizeof(name));
        return true;
    }
    bool valid(bool allow_empty = false) const {
        const size_t size = strnlen(name, Capacity);
        if (size >= Capacity || !valid(std::string_view(name, size), allow_empty)) return false;
        for (size_t i = size + 1; i < Capacity; ++i) if (name[i]) return false;
        return true;
    }
    bool empty() const { return !name[0]; }
    explicit operator bool() const { return !empty(); }
    const char* c_str() const { return name; }
    std::string_view view() const { return name; }
};
inline bool operator==(const DatasetId& a, const DatasetId& b) {
    return std::memcmp(a.name, b.name, DatasetId::Capacity) == 0;
}
inline bool operator!=(const DatasetId& a, const DatasetId& b) { return !(a == b); }

} // namespace geoputer

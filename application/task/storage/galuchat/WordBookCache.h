#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "../../../Config.h"

namespace geoputer {

struct WordBookCacheStats {
    uint32_t hits = 0, misses = 0;
    uint8_t entries = 0;
};

// Storage-owned, dataset-local cache of validated/formatted UTF-8 names. Requests
// and coordinates never enter the cache. order_[0] is most recently used.
class WordBookCache {
public:
    static constexpr size_t Capacity = config::WordBookCacheEntries;

    bool find(uint32_t code, char (&lines)[3][64]) {
        if (!code) return false;
        for (unsigned position = 0; position < count_; ++position) {
            const auto slot = order_[position];
            if (entries_[slot].code != code) continue;
            std::memcpy(lines, entries_[slot].lines, sizeof(lines));
            promote(position);
            ++hits_;
            return true;
        }
        ++misses_;
        return false;
    }

    void remember(uint32_t code, const char (&lines)[3][64]) {
        if (!code) return;
        unsigned position = 0;
        for (; position < count_; ++position)
            if (entries_[order_[position]].code == code) break;
        if (position == count_) {
            if (count_ < Capacity) {
                order_[count_] = count_;
                ++count_;
            } else position = count_ - 1;
        }
        auto& entry = entries_[order_[position]];
        entry.code = code;
        std::memcpy(entry.lines, lines, sizeof(entry.lines));
        promote(position);
    }

    void clear() { count_ = 0; hits_ = misses_ = 0; }
    WordBookCacheStats stats() const { return {hits_, misses_, count_}; }

private:
    void promote(unsigned position) {
        const auto slot = order_[position];
        for (; position; --position) order_[position] = order_[position - 1];
        order_[0] = slot;
    }

    struct Entry { uint32_t code; char lines[3][64]; } entries_[Capacity]{};
    uint8_t order_[Capacity]{};
    uint8_t count_ = 0;
    uint32_t hits_ = 0, misses_ = 0;
};

}  // namespace geoputer

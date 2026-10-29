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

// Storage-owned, dataset-local cache of the final two UTF-8 display rows. Requests
// and coordinates never enter the cache. order_[0] is most recently used.
class WordBookCache {
public:
    static constexpr size_t Capacity = config::WordBookCacheEntries;

    template<size_t N>
    bool find(uint32_t code, char (&lines)[2][N]) {
        static_assert(N>=64,"Cache copies the clipped display prefix");
        if (!code) return false;
        for (unsigned position = 0; position < count_; ++position) {
            const auto slot = order_[position];
            if (entries_[slot].code != code) continue;
            for(unsigned row=0;row<2;++row) {
                std::memset(lines[row],0,N);std::memcpy(lines[row],entries_[slot].lines[row],64);
            }
            promote(position);
            ++hits_;
            return true;
        }
        ++misses_;
        return false;
    }

    template<size_t N>
    void remember(uint32_t code, const char (&lines)[2][N]) {
        static_assert(N>=64,"Cache stores the clipped display prefix");
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
        for(unsigned row=0;row<2;++row) {
            std::memcpy(entry.lines[row],lines[row],64);entry.lines[row][63]='\0';
        }
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

    struct Entry { uint32_t code; char lines[2][64]; } entries_[Capacity]{};
    uint8_t order_[Capacity]{};
    uint8_t count_ = 0;
    uint32_t hits_ = 0, misses_ = 0;
};

}  // namespace geoputer

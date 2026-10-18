#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include "task/storage/galuchat/WordBookCache.h"

using namespace geoputer;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

void names(uint32_t code, char (&lines)[2][64]) {
    std::memset(lines, 0, sizeof(lines));
    std::snprintf(lines[0], sizeof(lines[0]), "県%lu", static_cast<unsigned long>(code));
    std::snprintf(lines[1], sizeof(lines[1]), "市%lu", static_cast<unsigned long>(code));
}
}  // namespace

void testWordBookCache() {
    WordBookCache cache;
    char input[2][64], output[2][64]{};
    check(!cache.find(1, output) && cache.stats().entries == 0, "empty name cache misses");
    names(1, input);
    cache.remember(0, input);
    check(!cache.find(0, output) && !cache.stats().entries, "region zero never enters the name cache");
    cache.remember(1, input);
    input[0][0] = 'X';
    check(cache.find(1, output) && std::strcmp(output[0], "県1") == 0, "cache owns its UTF-8 name copy");
    output[0][0] = 'Y';
    check(cache.find(1, output) && std::strcmp(output[0], "県1") == 0, "returned names cannot mutate the cached entry");

    cache.clear();
    for (uint32_t code = 1; code <= 16; ++code) {
        names(code, input);
        cache.remember(code, input);
    }
    check(cache.stats().entries == 16 && cache.find(1, output), "all sixteen entries fit and an old entry can be reused");
    names(17, input); cache.remember(17, input);
    check(!cache.find(2, output) && cache.find(1, output), "seventeenth entry evicts least recently used, not oldest inserted");
    names(99, input); cache.remember(1, input);
    check(cache.stats().entries == 16 && cache.find(1, output) && std::strcmp(output[0], "県99") == 0,
          "replacing an existing code updates its names without allocating another slot");
    check(cache.find(3, output), "updating an existing entry does not evict a different code");
    names(UINT32_MAX, input); cache.remember(UINT32_MAX, input);
    check(cache.find(UINT32_MAX, output) && std::strcmp(output[0], input[0]) == 0, "full unsigned 32-bit region code survives");

    cache.clear();
    check(!cache.stats().entries && !cache.stats().hits && !cache.stats().misses && !cache.find(1, output),
          "dataset reset drops cached names and statistics");

    // Compare long mixed access sequences with a separate list-based LRU model.
    std::vector<uint32_t> recent;
    uint32_t random = 0x93a7812u;
    for (unsigned iteration = 0; iteration < 10000; ++iteration) {
        random = random * 1664525u + 1013904223u;
        const uint32_t code = random % 29u + 1;
        const auto found = std::find(recent.begin(), recent.end(), code);
        const bool expected = found != recent.end();
        check(cache.find(code, output) == expected, "name-cache eviction agrees with reference LRU");
        names(code, input);
        if (expected) {
            check(std::memcmp(input, output, sizeof(input)) == 0, "mixed hits keep the correct complete two-row display");
            recent.erase(found);
        } else {
            cache.remember(code, input);
            if (recent.size() == 16) recent.pop_back();
        }
        recent.insert(recent.begin(), code);
        check(cache.stats().entries == recent.size(), "cache capacity remains bounded during long operation");
    }
}

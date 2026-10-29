#pragma once
#include "../../../Config.h"
#include <array>
#include <cstdint>

namespace geoputer {
// One query owns this typed UI loan. Reuse token arrays for each component.
// Small path and per-component position arrays are operation-local stack data.
struct WordBookWorkspace {
    std::array<uint16_t,config::WordBookLookupTokens> tokens,order;
};
static_assert(sizeof(WordBookWorkspace)==config::WordBookLookupTokens*2*sizeof(uint16_t),"Only two token arrays occupy the UI loan");
static_assert(config::PlaceResultLineBytes<UINT16_MAX,"Output positions reserve UINT16_MAX as absent");
static_assert(config::WordBookLookupTokens<=UINT16_MAX,"Token permutations must fit uint16");
} // namespace geoputer

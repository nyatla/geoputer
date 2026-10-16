#pragma once

#include <cstddef>
#include "GpsTypes.h"

namespace geoputer {

enum class NmeaResult { Incomplete, Rejected, OtherSentence, Position };

// GGA/RMC position, quality, movement and UTC date decoder. Fixed storage, checksum required, all talkers accepted.
class NmeaParser {
public:
    NmeaResult feed(char byte, GpsFix& position);

private:
    NmeaResult decode(GpsFix& position);
    char line_[192]{};
    size_t length_ = 0;
    bool collecting_ = false;
};

}  // namespace geoputer

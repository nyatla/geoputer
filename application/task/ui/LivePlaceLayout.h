#pragma once

namespace geoputer {
namespace LivePlaceLayout {
constexpr int AddressX = 4, AddressY = 20, AddressStep = 20;
constexpr int AddressBottom = 60, DividerX = 120, SpeedBottom = 83;
constexpr int InfoBottom = 106, FooterTop = 120;
constexpr int DetailLabelX = 122, DetailValueX = 148, DetailY = 65, DetailStep = 10;
constexpr int LogY = 110, ErrorY = 107, ErrorHeight = 13;
constexpr int SpeedRight = 84, SpeedY = 66, SpeedUnitX = 84, SpeedUnitY = 73;
}

inline int speedTextWidth(const char* text) {
    int width = 0;
    for (; *text; ++text) width += *text == '.' ? 8 : 16;
    return width;
}

}  // namespace geoputer

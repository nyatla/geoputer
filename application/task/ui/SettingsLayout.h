#pragma once

#include <cstdint>

namespace geoputer {

struct SettingsLayout {
    static constexpr int Cell = 16;
    static constexpr int LabelX = 20;
    static constexpr int ValueRight = 236;
    static constexpr int HeaderBottom = 22;
    static constexpr int FirstRowY = 27;
    static constexpr int RowStep = 20;
    static constexpr unsigned VisibleRows = 4;
    static constexpr unsigned LicenseRows = 9;
    static constexpr unsigned LicenseColumns = 29;
    static constexpr int LicenseRowStep = 10;
    static constexpr unsigned TextColumns = 14;

    static unsigned firstVisible(unsigned selected, unsigned count) {
        if (count <= VisibleRows || selected < VisibleRows) return 0;
        const unsigned first = selected - VisibleRows + 1;
        return first < count - VisibleRows ? first : count - VisibleRows;
    }
};

static_assert(SettingsLayout::FirstRowY + (SettingsLayout::VisibleRows - 1) * SettingsLayout::RowStep +
                  SettingsLayout::Cell <= 108, "Settings rows must leave room for messages and key guides");
static_assert(4 + SettingsLayout::TextColumns * SettingsLayout::Cell <= 236,
              "Large text must fit the Cardputer display with side margins");
static_assert(4 + SettingsLayout::LicenseColumns * 8 <= 236 &&
              SettingsLayout::FirstRowY + (SettingsLayout::LicenseRows - 1) * SettingsLayout::LicenseRowStep + 8 <= 118,
              "Small license text must fit between the header and footer");

}  // namespace geoputer

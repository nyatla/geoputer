#pragma once

#include "UiTypes.h"

namespace geoputer {

// Input is the unshifted value of a physical Cardputer key, independent of Fn.
// Del and Enter are supplied separately by the keyboard driver's state flags.
inline UiKey mapPhysicalKey(char key, bool fn) {
    switch (key) {
        case '2': return UiKey::Menu;
        case ';': return UiKey::Up;
        case '.': return UiKey::Down;
        case ',': return UiKey::Left;
        case '/': return UiKey::Right;
        case '1': return fn ? UiKey::F1 : UiKey::Other;
        case '`': return fn ? UiKey::Escape : UiKey::Other;
        default: return UiKey::Other;
    }
}

}  // namespace geoputer

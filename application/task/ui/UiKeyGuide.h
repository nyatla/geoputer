#pragma once
#include "GpsUiTypes.h"

namespace geoputer {
struct UiKeyGuide {
    bool accepting = false, root = false, back = false, system = false, remove = false;
    bool vertical = false, horizontal = false;
    bool up = false, down = false, left = false, right = false;
    bool zoom_in = false, zoom_out = false;
    bool allowsKey(UiKey key) const {
        switch (key) {
            case UiKey::ZoomIn: return zoom_in;
            case UiKey::ZoomOut: return zoom_out;
            case UiKey::Up: return up;
            case UiKey::Down: return down;
            case UiKey::Left: return left;
            case UiKey::Right: return right;
            default: return true;
        }
    }
};
} // namespace geoputer

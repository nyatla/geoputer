#pragma once
#include "ScreenNavigation.h"
#include "EscapeIcon.h"
#include "EnterIcon.h"
#include <array>
#include <cassert>
namespace geoputer {
enum class ShortcutTone : uint8_t { Disabled, Normal, Accent };
struct ShortcutHint { UiKey key; ShortcutTone tone; };
inline constexpr unsigned shortcutWidth(UiKey key) {return key==UiKey::Escape?escape_icon::Width:key==UiKey::Enter?enter_icon::Width:8;}
// A render-time value, never retained per screen and never allocated on the heap.
// Other is an empty 8-pixel cell separating nonempty groups.
class UiShortcutGuide {
public:
    static constexpr unsigned Capacity=29;
    void group() { separate_=count_!=0; }
    void key(UiKey key,bool enabled=true,bool accent=false) {
        assert(count_+1u+unsigned(separate_)<=Capacity);
        if(separate_) {items_[count_++]={UiKey::Other,ShortcutTone::Normal};separate_=false;}
        items_[count_++]={key,!enabled?ShortcutTone::Disabled:accent?ShortcutTone::Accent:ShortcutTone::Normal};
    }
    unsigned size()const {return count_;}
    unsigned pixelWidth()const {unsigned width=0;for(unsigned i=0;i<count_;++i)width+=shortcutWidth(items_[i].key);return width;}
    const ShortcutHint& operator[](unsigned i)const {assert(i<count_);return items_[i];}
private:
    std::array<ShortcutHint,Capacity> items_{};
    uint8_t count_=0;
    bool separate_=false;
};
static_assert(sizeof(UiShortcutGuide)<=64,"Footer must remain a small stack value");
inline char shortcutCharacter(UiKey key) {
    for(const auto& item:MenuScreens)if(item.key==key)return item.shortcut;
    switch(key) {
        case UiKey::Other:return ' ';
        case UiKey::ScreenOff:return 'S';case UiKey::GpsControl:return 'G';
        case UiKey::DeleteLog:return 'D';
        case UiKey::FirstPoint:return 'B';case UiKey::LastPoint:return 'E';
        case UiKey::Recalculate:return 'R';
        case UiKey::ZoomIn:return '+';case UiKey::ZoomOut:return '-';case UiKey::Playback:return 'P';
        default:return 0; // Escape, Enter and directions are ROM bitmaps, not font characters.
    }
}
} // namespace geoputer

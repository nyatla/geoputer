#pragma once

#include "UiTypes.h"

namespace geoputer {

enum class DialogKind : uint8_t { None, PauseLog, StopLog, DeleteLog, FormatSd, ResetSettings, EnterUsbMode, ExitUsbMode, RestartLogger };
enum class DialogInput : uint8_t { Ignored, Changed, Cancelled, Accepted };

// UI-owned two-choice modal. Opening always selects Cancel; only an explicit
// enabled action followed by Enter can create an application request.
class ChoiceDialog {
public:
    void open(DialogKind kind, bool enabled = true) {
        kind_ = kind; selected_ = false; setActionEnabled(enabled);
    }
    void close() { kind_ = DialogKind::None; selected_ = false; }
    bool active() const { return kind_ != DialogKind::None; }
    DialogKind kind() const { return kind_; }
    bool selected() const { return selected_; }
    bool actionEnabled() const { return enabled_; }
    void setActionEnabled(bool enabled) {
        enabled_ = enabled;
        if (!enabled) selected_ = false;
    }
    DialogInput keyPressed(UiKey key) {
        if (!active()) return DialogInput::Ignored;
        if (key == UiKey::Escape) { close(); return DialogInput::Cancelled; }
        if ((key == UiKey::Left || key == UiKey::Right) && enabled_) {
            const bool next = key == UiKey::Right;
            if (next == selected_) return DialogInput::Ignored;
            selected_ = next; return DialogInput::Changed;
        }
        if (key == UiKey::Enter) {
            const bool accepted = selected_ && enabled_;
            close(); return accepted ? DialogInput::Accepted : DialogInput::Cancelled;
        }
        return DialogInput::Ignored;
    }

private:
    DialogKind kind_ = DialogKind::None;
    bool selected_ = false;
    bool enabled_ = true;
};

}  // namespace geoputer

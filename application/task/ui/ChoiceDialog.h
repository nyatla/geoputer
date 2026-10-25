#pragma once

#include "UiTypes.h"

namespace geoputer {

enum class DialogKind : uint8_t { None, DeleteLog, FormatSd, ResetSettings, EnterUsbMode, ExitUsbMode, RestartLogger, RebuildIndex, VerifyIndex, ClearDataset, GpsControl };
enum class DialogInput : uint8_t { Ignored, Changed, Cancelled, Accepted };

// Confirmation dialogs select Cancel initially. Action menus omit that item and
// select a caller-specified action; Escape cancels either kind.
class ChoiceDialog {
public:
    void open(DialogKind kind, bool enabled = true) {
        kind_ = kind; selected_ = 0; choices_ = 2; cancel_choice_ = true; setActionEnabled(enabled);
    }
    void openActions(DialogKind kind, unsigned choices, unsigned initial) {
        kind_ = kind; choices_ = choices >= 1 && choices <= 3 ? choices : 1;
        cancel_choice_ = false; selected_ = initial < choices_ ? initial : 0;
        setActionEnabled(true);
    }
    void close() { kind_ = DialogKind::None; selected_ = false; }
    bool active() const { return kind_ != DialogKind::None; }
    DialogKind kind() const { return kind_; }
    bool selected() const { return selected_ != 0; }
    unsigned choice() const { return selected_; }
    unsigned choices() const { return choices_; }
    bool actionEnabled() const { return enabled_mask_ != 0; }
    bool choiceEnabled(unsigned choice) const { return choice < choices_ && ((cancel_choice_ && choice == 0) || (enabled_mask_ & (1u << choice))); }
    void setChoiceEnabled(unsigned choice, bool enabled) {
        if ((cancel_choice_ && choice == 0) || choice >= choices_) return;
        if (enabled) enabled_mask_ |= 1u << choice;
        else enabled_mask_ &= ~(1u << choice);
        // Keep an action menu's focus when it is temporarily disabled; never
        // silently move Pause/Resume to Stop while status updates arrive.
        if (cancel_choice_) selectEnabled();
    }
    void setActionEnabled(bool enabled) {
        enabled_mask_ = enabled ? ((1u << choices_) - (cancel_choice_ ? 2 : 1)) : 0;
        selectEnabled();
    }
    DialogInput keyPressed(UiKey key) {
        if (!active()) return DialogInput::Ignored;
        if (key == UiKey::Escape) { close(); return DialogInput::Cancelled; }
        if (key == UiKey::Left || key == UiKey::Right) {
            int next = static_cast<int>(selected_) + (key == UiKey::Right ? 1 : -1);
            while (next >= 0 && next < static_cast<int>(choices_)) {
                if (choiceEnabled(next)) { selected_ = next; return DialogInput::Changed; }
                next += key == UiKey::Right ? 1 : -1;
            }
            return DialogInput::Ignored;
        }
        if (key == UiKey::Enter) {
            if (!choiceEnabled(selected_)) return DialogInput::Ignored;
            const bool accepted = !cancel_choice_ || selected_ != 0;
            close(); return accepted ? DialogInput::Accepted : DialogInput::Cancelled;
        }
        return DialogInput::Ignored;
    }

private:
    void selectEnabled() {
        if (choiceEnabled(selected_)) return;
        selected_ = 0;
        for (unsigned i = 0; i < choices_; ++i)
            if (choiceEnabled(i)) { selected_ = i; break; }
    }
    bool cancel_choice_ = true;
    DialogKind kind_ = DialogKind::None;
    unsigned selected_ = 0, choices_ = 2;
    uint8_t enabled_mask_ = 2;
};

}  // namespace geoputer

#include <stdexcept>
#include "task/ui/UiModel.h"

using namespace geoputer;
static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void testDialogs() {
    for (auto kind : {DialogKind::PauseLog, DialogKind::StopLog, DialogKind::DeleteLog,
                      DialogKind::FormatSd, DialogKind::ResetSettings, DialogKind::EnterUsbMode, DialogKind::ExitUsbMode, DialogKind::RestartLogger}) {
        ChoiceDialog dialog;
        dialog.open(kind);
        check(dialog.active() && !dialog.selected(), "all dialogs default to Cancel");
        for (auto key : {UiKey::Up, UiKey::Down, UiKey::OpenGpsLogger, UiKey::OpenSettings,
                         UiKey::OpenLogs, UiKey::F1,UiKey::PauseLog, UiKey::DeleteLog})
            check(dialog.keyPressed(key) == DialogInput::Ignored && !dialog.selected(),
                  "a horizontal modal ignores vertical navigation and unrelated keys");
        check(dialog.keyPressed(UiKey::Enter) == DialogInput::Cancelled && !dialog.active(),
              "Enter on default Cancel never accepts");
        dialog.open(kind);
        dialog.keyPressed(UiKey::Right);
        check(dialog.selected(), "Right selects the action");
        check(dialog.keyPressed(UiKey::Right) == DialogInput::Ignored && dialog.selected(),
              "Right at the rightmost choice does not wrap to Cancel");
        check(dialog.keyPressed(UiKey::Escape) == DialogInput::Cancelled && !dialog.active(),
              "Escape cancels even after selecting the action");
        dialog.open(kind);
        check(dialog.keyPressed(UiKey::Left) == DialogInput::Ignored && !dialog.selected(),
              "Left at Cancel stays on Cancel");
        dialog.keyPressed(UiKey::Right);
        dialog.keyPressed(UiKey::Left);
        check(!dialog.selected(), "Left returns from the action to Cancel");
        dialog.keyPressed(UiKey::Right);
        check(dialog.keyPressed(UiKey::Enter) == DialogInput::Accepted && !dialog.active(),
              "explicit selection and Enter accept exactly once");
        check(dialog.keyPressed(UiKey::Enter) == DialogInput::Ignored, "closed dialog cannot repeat an action");
        dialog.open(kind);
        dialog.keyPressed(UiKey::Right);
        dialog.setActionEnabled(false);
        check(!dialog.selected() && dialog.keyPressed(UiKey::Right) == DialogInput::Ignored,
              "new protection moves selection back to Cancel and disables the action");
        check(dialog.keyPressed(UiKey::Enter) == DialogInput::Cancelled, "disabled action cannot be accepted");
    }
    UiData data{}; data.settings = defaultSettings();
    data.browse.revision = 1; data.browse.count = 1;
    UiModel model;
    model.setScreen({ScreenId::Logs, DisplayMode::View, 1, data, 0, ActionResult::None});
    model.keyPressed(UiKey::DeleteLog, 10); model.keyPressed(UiKey::Right, 11);
    data.browse.active_mask = 1; ++data.revision; // Same list, newly active file.
    model.updateContent({DisplayMode::View, 1, data, 0, ActionResult::None});
    check(model.confirming() && !model.confirmChoice() && !model.dialog().actionEnabled(),
          "content updates revoke Delete without a stale selected action");
    model.keyPressed(UiKey::Enter, 12);
    check(model.snapshot().action.kind == ActionKind::None, "newly active file cannot produce a delete request");

    data.recording.state = RecordingState::Recording;
    model.setScreen({ScreenId::GpsLogger, DisplayMode::View, 2, data, 0, ActionResult::None});
    model.keyPressed(UiKey::Enter, 20); model.keyPressed(UiKey::Left, 21); model.keyPressed(UiKey::Enter, 22);
    const auto action = model.snapshot().action;
    check(action.kind == ActionKind::Logging && action.log_action == LogAction::Stop && action.confirmed,
          "shared Stop dialog preserves App-owned confirmed action");
}

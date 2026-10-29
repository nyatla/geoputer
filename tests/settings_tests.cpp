#include <stdexcept>
#include <cstring>
#include "Settings.h"
#include "task/ui/UiModel.h"
#include "task/ui/LicenseText.h"
#include "task/storage/SettingsStore.h"
#include "task/ui/SettingsLayout.h"

using namespace geoputer;

namespace {
void check(bool value, const char* detail) { if (!value) throw std::runtime_error(detail); }

void codec() {
    Settings value = defaultSettings();
    check(!value.dataset && !value.wordbook_language[0] && validSettings(value), "new configurations start without a dataset");
    uint8_t record[SettingsRecordSize];
    for (unsigned log = 0; log < 3; ++log) for (unsigned flush = 0; flush < 5; ++flush)
    for (unsigned timeout = 0; timeout < settingChoices(static_cast<unsigned>(SettingId::ScreenTimeout)); ++timeout) for (unsigned light = 0; light < 5; ++light)
    for (unsigned dataset = 0; dataset <= 3; ++dataset) for (unsigned gps = 0; gps < 2; ++gps)
    for (unsigned language = 0; language < 2; ++language) {
        value = {static_cast<uint8_t>(log), static_cast<uint8_t>(flush), static_cast<uint8_t>(timeout),
                 static_cast<uint8_t>(light), static_cast<uint8_t>(dataset), static_cast<uint8_t>(gps), static_cast<UiLanguage>(language)};
        check(validSettings(value), "valid settings recognized");
        encodeSettings(value, record);
        Settings decoded{};
        check(decodeSettings(record, sizeof(record), decoded) && sameSettings(decoded, value),
              "all settings combinations survive the portable NVS record encoding");
    }
    value = defaultSettings();
    encodeSettings(value, record);
    for (unsigned i = 0; i < sizeof(record); ++i) {
        record[i] ^= 0x80;
        Settings unchanged = value;
        check(!decodeSettings(record, sizeof(record), unchanged) && sameSettings(unchanged, value),
              "damaged NVS records cannot mutate active settings");
        record[i] ^= 0x80;
    }
    check(!decodeSettings(nullptr, sizeof(record), value), "missing record rejected");
    check(!decodeSettings(record, sizeof(record) - 1, value), "truncated NVS record rejected");
    check(!setSettingValue(value, 3, 5) && !setSettingValue(value, SettingCount, 0), "invalid choice and setting index rejected");
}

void persistenceAndBackendErrors() {
    Preferences::records.clear();
    Preferences::records["other/component"] = {1, 2, 3};
    SettingsStore first;
    Settings value{};
    Preferences::fail_open = true;
    check(!first.begin() && first.load(value) == SettingsState::Unavailable, "NVS open errors are distinct");
    Preferences::fail_open = false;
    check(first.begin() && first.load(value) == SettingsState::Defaults && sameSettings(value, defaultSettings()),
          "missing config uses defaults independently of any SD API");
    value.brightness = 4;
    value.language = UiLanguage::English;
    value.screen_timeout = 4;
    check(first.save(value), "save commits one NVS settings record");
    SettingsStore rebooted;
    Settings restored{};
    check(rebooted.begin() && rebooted.load(restored) == SettingsState::Loaded && sameSettings(restored, value),
          "a new SettingsStore restores the persisted record");
    Preferences::fail_write = true;
    value.brightness = 0;
    check(!rebooted.save(value), "backend write failure cannot report persistence success");
    Preferences::fail_write = false;
    check(rebooted.load(restored) == SettingsState::Loaded && restored.brightness == 4,
          "failed write preserves the previous committed record");
    Preferences::fail_read = true;
    check(rebooted.load(restored) == SettingsState::Invalid, "read failure is not confused with missing settings");
    Preferences::fail_read = false;
    Preferences::records["geoputer/config"][4] ^= 0x80;
    check(rebooted.load(restored) == SettingsState::Invalid && sameSettings(restored, defaultSettings()),
          "corrupted persistent record falls back to defaults");
    check(rebooted.save(defaultSettings()) && rebooted.load(restored) == SettingsState::Loaded &&
          sameSettings(restored, defaultSettings()), "reset stores defaults through the same save operation");
    check(Preferences::records.at("other/component") == std::vector<unsigned char>({1, 2, 3}),
          "reset does not erase other NVS namespaces");
}

SetUiScreen screen() {
    SetUiScreen command{};
    command.screen = ScreenId::Settings;
    command.mode = DisplayMode::View;
    command.generation = 1;
    command.data.revision = 1;
    command.data.settings = defaultSettings();
    command.data.storage.settings_state = SettingsState::Defaults;
    return command;
}

void navigationAndEditing() {
    for (unsigned selected = 0; selected < SettingCount; ++selected) {
        const unsigned first = SettingsLayout::firstVisible(selected, SettingCount);
        check(first <= selected && selected < first + SettingsLayout::VisibleRows && first + SettingsLayout::VisibleRows <= SettingCount,
              "all seven operation settings remain selectable inside the four-row large-font window");
    }
    UiModel ui;
    auto command = screen();
    ui.setScreen(command);
    for (unsigned i = 0; i < 10; ++i) ui.keyPressed(UiKey::Down, 10 + i);
    check(ui.snapshot().selected_setting == SettingsRootCount-1, "Settings root has five entries");
    ui.keyPressed(UiKey::Enter, 30);
    check(ui.snapshot().page == SettingsPage::Licenses, "last root entry opens licenses");
    ui.keyPressed(UiKey::Down, 40);
    check(ui.licenseOffset() == 1, "license body scrolls locally");
    ui.keyPressed(UiKey::Escape, 50);
    check(ui.snapshot().page == SettingsPage::Root && ui.snapshot().action.kind == ActionKind::None,
          "child Escape returns to Settings root without a top-level transition");
    ui.keyPressed(UiKey::Enter, 60);
    check(ui.snapshot().page == SettingsPage::Operation, "first entry opens operation settings");
    for (unsigned i = 0; i < 4; ++i) ui.keyPressed(UiKey::Down, 70 + i);
    ui.keyPressed(UiKey::Right, 80);
    check(ui.snapshot().selected_setting == 4 && !ui.editing(), "left/right cannot navigate a menu");
    ui.keyPressed(UiKey::Enter, 90);
    ui.keyPressed(UiKey::Right, 100);
    check(ui.editing() && ui.editValue() == 2, "brightness choice changes only in editing state");
    UpdateUiContent gps_update{};
    gps_update.mode = DisplayMode::View; gps_update.generation = 1; gps_update.data = command.data;
    gps_update.data.revision = 2;
    ui.updateContent(gps_update);
    check(ui.editing() && ui.editValue() == 2, "App/GPS updates preserve unfinished editing");
    ui.keyPressed(UiKey::Escape, 110);
    check(!ui.editing() && ui.data().settings.brightness == 1, "edit cancellation preserves committed setting");
    ui.keyPressed(UiKey::Enter, 120);
    ui.keyPressed(UiKey::Right, 130);
    ui.keyPressed(UiKey::Enter, 140);
    const auto saved = ui.snapshot().action;
    check(saved.kind == ActionKind::SaveSettings && saved.settings.brightness == 2,
          "confirmed edit contains an immutable settings copy for App");
    ui.keyPressed(UiKey::Escape, 150);
    check(ui.snapshot().action.action_id == saved.action_id, "pending save cannot be overwritten by navigation");
    gps_update.processed_action_id = saved.action_id; gps_update.action_result = ActionResult::Applied;
    ui.updateContent(gps_update);
    ui.keyPressed(UiKey::Escape, 160);
    check(ui.snapshot().page == SettingsPage::Root, "Escape returns from operation page to root");
    ui.keyPressed(UiKey::Escape, 170);
    auto menu = ui.snapshot().action;
    check(menu.kind == ActionKind::ChangeScreen && menu.target==ScreenId::LivePlace, "Escape on Settings root returns Live");
    ui.setScreen({menu.target,DisplayMode::View,ui.snapshot().screen_generation+1,ui.data(),menu.action_id,ActionResult::Applied});
    ui.keyPressed(UiKey::OpenSettings, 171);
    check(ui.snapshot().action.kind == ActionKind::ChangeScreen && ui.snapshot().action.target == ScreenId::Settings,
          "0 at Live requests Settings through App");
}

void destructiveConfirmationAndSleep() {
    UiModel ui;
    auto command = screen();
    ui.setScreen(command);
    for(unsigned n=0;n<settingsRootIndex(SettingsRootItem::Storage);++n)ui.keyPressed(UiKey::Down,10+n);
    ui.keyPressed(UiKey::Enter, 20);
    ui.keyPressed(UiKey::Enter, 21);
    check(ui.cardInfo() && ui.snapshot().action.kind == ActionKind::None,
          "card information opens a local details view without issuing an SD operation");
    ui.keyPressed(UiKey::Escape, 22);
    check(!ui.cardInfo() && ui.snapshot().page == SettingsPage::Storage && ui.snapshot().selected_setting == 0,
          "Escape from card details returns to the storage list");
    for (unsigned i = 0; i < 3; ++i) ui.keyPressed(UiKey::Down, 30 + i);
    ui.keyPressed(UiKey::Enter, 40);
    check(ui.confirming() && !ui.confirmChoice() && ui.snapshot().action.kind == ActionKind::None,
          "format requires separate confirmation and defaults to cancel");
    ui.keyPressed(UiKey::Enter, 50);
    check(!ui.confirming() && ui.snapshot().action.kind == ActionKind::None, "default Enter cancels format");
    ui.keyPressed(UiKey::Enter, 60); ui.keyPressed(UiKey::Right, 70); ui.keyPressed(UiKey::Enter, 80);
    check(ui.snapshot().action.kind == ActionKind::Storage &&
          ui.snapshot().action.storage_operation == StorageOperation::Format && ui.snapshot().action.confirmed,
          "only explicit OK creates a confirmed format action");

    UiModel reset;
    reset.setScreen(command);
    for(unsigned n=0;n<SettingsSystemRow;++n)reset.keyPressed(UiKey::Down,10+n);
    reset.keyPressed(UiKey::Enter, 20);
    for(unsigned n=0;n<SettingsSystemResetRow;++n)reset.keyPressed(UiKey::Down,21+n);
    reset.keyPressed(UiKey::Enter, 30);
    check(reset.snapshot().page == SettingsPage::Reset && !reset.confirming() && reset.snapshot().action.kind == ActionKind::None,
          "System Reset opens the existing initialization page without executing it");
    reset.keyPressed(UiKey::Enter, 40); reset.keyPressed(UiKey::Escape, 50);
    check(!reset.confirming() && reset.snapshot().action.kind == ActionKind::None, "Escape cancels reset");
    reset.keyPressed(UiKey::Escape, 51);
    check(reset.snapshot().page == SettingsPage::System && reset.snapshot().selected_setting == SettingsSystemResetRow,
          "Reset Back returns to its System parent with selection preserved");
    reset.keyPressed(UiKey::Enter, 52);
    reset.keyPressed(UiKey::Enter, 60); reset.keyPressed(UiKey::Right, 70); reset.keyPressed(UiKey::Enter, 80);
    check(reset.snapshot().action.kind == ActionKind::ResetSettings && reset.snapshot().action.confirmed &&
          sameSettings(reset.snapshot().action.settings, defaultSettings()), "reset contains defaults only");

    UiModel idle;
    idle.setScreen(command);
    idle.tick(100); idle.tick(15100);
    check(idle.snapshot().action.kind == ActionKind::Sleep, "configured timeout requests background mode");
    UpdateUiContent update{};
    update.mode = DisplayMode::Background; update.generation = 1; update.data = command.data;
    update.processed_action_id = idle.snapshot().action.action_id; update.action_result = ActionResult::Applied;
    idle.updateContent(update);
    idle.keyPressed(UiKey::Enter, 15200);
    check(idle.snapshot().action.kind == ActionKind::Wake && !idle.confirming() && !idle.editing(),
          "wake key cannot execute the screen's Enter action");

    UiModel always_on;
    command.data.settings.screen_timeout = 4;
    always_on.setScreen(command);
    always_on.tick(100);
    for (const uint32_t now : {60100u, 3600100u, 0xffffff00u, 1000u}) {
        always_on.tick(now);
        check(always_on.snapshot().action.kind == ActionKind::None,
              "Never keeps display active during long inactivity and tick wrap");
    }
    command.data.settings.screen_timeout = 3;
    UiModel sixty_seconds;
    sixty_seconds.setScreen(command);
    sixty_seconds.tick(100); sixty_seconds.tick(60099);
    check(sixty_seconds.snapshot().action.kind == ActionKind::None, "60 seconds remains active before its deadline");
    sixty_seconds.tick(60100);
    check(sixty_seconds.snapshot().action.kind == ActionKind::Sleep, "60 seconds retains its original timeout");
    check(licenseLineCount() > 100 && std::strlen(licenseLine(0)) > 0 && !*licenseLine(licenseLineCount()),
          "offline license document exists and bounds are checked");
}
}  // namespace

void testSettings() { codec(); persistenceAndBackendErrors(); navigationAndEditing(); destructiveConfirmationAndSleep(); }

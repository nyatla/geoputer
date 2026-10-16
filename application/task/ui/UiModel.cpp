#include "UiModel.h"
#include "LicenseText.h"
#include "SettingsLayout.h"

namespace geoputer {

OperationStatus UiModel::setScreen(const SetUiScreen& command) {
    if (command.screen != ScreenId::LivePlace && command.screen != ScreenId::Settings && command.screen != ScreenId::Boot &&
        command.screen != ScreenId::Menu && command.screen != ScreenId::GpsLogger && !isLogBrowser(command.screen)) {
        return OperationStatus::InvalidRequest;
    }
    if (command.generation <= generation_) return OperationStatus::StaleContent;
    // Switching screens cannot silently erase an unprocessed confirmed action.
    if (action_.kind != ActionKind::None &&
        (command.processed_action_id != action_.action_id || command.action_result == ActionResult::None)) {
        return OperationStatus::InvalidRequest;
    }
    acknowledge(command.processed_action_id, command.action_result);
    if (screen_ == ScreenId::Boot || command.screen == ScreenId::Boot) last_input_ms_ = 0;
    screen_ = command.screen;
    mode_ = command.mode;
    generation_ = command.generation;
    copyData(command.data);
    selection_ = command.screen==ScreenId::Logs ? command.data.browse.selected : 0;
    page_ = SettingsPage::Root;
    editing_ = confirming_ = confirm_yes_ = false;
    license_offset_ = 0;
    card_info_ = false;
    redraw_ = true;
    return OperationStatus::Ok;
}

OperationStatus UiModel::updateContent(const UpdateUiContent& command) {
    if (command.generation != generation_ || command.data.revision < data_.revision) {
        return OperationStatus::StaleContent;
    }
    copyData(command.data);
    mode_ = command.mode;
    acknowledge(command.processed_action_id, command.action_result);
    redraw_ = true;
    return OperationStatus::Ok;
}

void UiModel::copyData(const UiData& data) {
    if (!datasetUnavailable(data.dataset.state)) dataset_warning_ = false;
    else if (data.dataset.state != data_.dataset.state || data.dataset.chunks != data_.dataset.chunks ||
             data.dataset.limit != data_.dataset.limit) dataset_warning_ = true;
    if (data.gps.fix.valid) clock_.setInitialUtc(data.gps.fix.utc_ms, last_tick_ms_);
    const auto previous_sd = data_.storage.sd;
    const auto previous_rg = data_.storage.rg;
    if (newerRevision(data.storage.sd.revision, previous_sd.revision) &&
        (!sd_seen_ || data.storage.sd.access_id != previous_sd.access_id)) {
        access_started_ms_ = last_tick_ms_; sd_seen_ = true;
    }
    const auto old_browse=data_.browse.revision;
    data_ = data;
    // Activity is global; an older job snapshot cannot roll it back.
    if (newerRevision(previous_sd.revision, data_.storage.sd.revision)) data_.storage.sd = previous_sd;
    if (newerRevision(previous_rg.revision, data_.storage.rg.revision)) data_.storage.rg = previous_rg;
    for (auto& line : data_.place) line[sizeof(line) - 1] = '\0';
    for(auto& name:data_.browse.names) name[LogNameBytes-1]='\0';
    data_.browse.name[LogNameBytes-1]='\0';
    if(screen_==ScreenId::Logs && !data_.browse.loading && data_.browse.revision!=old_browse) {
        selection_=data_.browse.selected;
    }
    data_.message[sizeof(data_.message) - 1] = '\0';
}

void UiModel::acknowledge(uint32_t action_id, ActionResult result) {
    if (action_.kind != ActionKind::None && action_id == action_.action_id &&
        result != ActionResult::None) {
        action_ = {};
    }
}

void UiModel::changed() {
    ++revision_;
    notification_pending_ = true;
    redraw_ = true;
}

void UiModel::keyPressed(UiKey key, uint32_t now_ms) {
    last_input_ms_ = now_ms;
    if (action_.kind != ActionKind::None || generation_ == 0) return;
    if (screen_ == ScreenId::Boot) {
        if (data_.boot.complete && key == UiKey::Enter) action(ActionKind::ChangeScreen, ScreenId::LivePlace);
        return;
    }
    if (mode_ == DisplayMode::Background) { action(ActionKind::Wake); return; }
    if (datasetWarning()) {
        if (key == UiKey::Escape || key == UiKey::Enter) { dataset_warning_ = false; changed(); }
        return;
    }
    if (screen_ == ScreenId::Menu) {
        if (key == UiKey::Escape) action(ActionKind::ChangeScreen);
        else if (key == UiKey::Up && selection_) { --selection_; changed(); }
        else if (key == UiKey::Down && selection_ < 2) { ++selection_; changed(); }
        else if (key == UiKey::Enter) action(ActionKind::ChangeScreen, selection_==2?ScreenId::Logs:selection_?ScreenId::Settings:ScreenId::GpsLogger);
        return;
    }
    if ((screen_ == ScreenId::LivePlace || screen_ == ScreenId::GpsLogger) && confirming_) {
        if (key == UiKey::Escape) { confirming_ = false; changed(); }
        else if (key == UiKey::Up || key == UiKey::Down) { confirm_yes_ = !confirm_yes_; changed(); }
        else if (key == UiKey::Enter) {
            confirming_ = false;
            if (confirm_yes_) { action(ActionKind::Logging); action_.log_action = confirm_log_action_; action_.confirmed = true; }
            else changed();
        }
        return;
    }
    if (screen_ == ScreenId::LivePlace) {
        if (key == UiKey::Menu) action(ActionKind::ChangeScreen, ScreenId::Menu);
        else if (key == UiKey::F1 && data_.recording.state == RecordingState::Recording) {
            confirm_log_action_ = LogAction::Pause;
            confirming_ = true; confirm_yes_ = false; changed();
        }
        return;
    }
    if (screen_ == ScreenId::GpsLogger) {
        const auto state = data_.recording.state;
        const unsigned count = state == RecordingState::Paused ? 2 : 1;
        if (selection_ >= count) selection_ = 0;
        if (key == UiKey::Escape) action(ActionKind::ChangeScreen);
        else if (key == UiKey::Up && selection_) { --selection_; changed(); }
        else if (key == UiKey::Down && selection_ + 1u < count) { ++selection_; changed(); }
        else if (key == UiKey::Enter && !data_.storage.log.closing) {
            if (state == RecordingState::Stopped || (state == RecordingState::Paused && !selection_)) {
                action(ActionKind::Logging);
                action_.log_action = state == RecordingState::Stopped ? LogAction::Start : LogAction::Resume;
            } else {
                confirm_log_action_ = LogAction::Stop;
                confirming_ = true; confirm_yes_ = false; changed();
            }
        }
        return;
    }
    if(isLogBrowser(screen_)) {
        const auto& view=data_.browse;
        if(key==UiKey::Escape) {
            action(ActionKind::ChangeScreen,screen_==ScreenId::Logs?ScreenId::Menu:screen_==ScreenId::LogDetail?ScreenId::Logs:ScreenId::LogDetail);
            return;
        }
        if(screen_==ScreenId::LogPoint) {
            if(key==UiKey::Up && view.target) browseAction(BrowseAction::Point,view.target-1);
            if(key==UiKey::Down && view.target+1<view.summary.records) browseAction(BrowseAction::Point,view.target+1);
            return;
        }
        if(view.loading) return;
        if(screen_==ScreenId::Logs) {
            if(key==UiKey::Up) {
                if(selection_) { --selection_; changed(); }
                else if(view.has_newer) browseAction(BrowseAction::Newer);
            } else if(key==UiKey::Down) {
                if(selection_+1<view.count) { ++selection_; changed(); }
                else if(view.has_older) browseAction(BrowseAction::Older);
            } else if(key==UiKey::Enter) {
                if(view.count) browseAction(BrowseAction::OpenFile,selection_);
                else browseAction(BrowseAction::Reload);
            }
        } else {
            if(key==UiKey::Up && selection_) { --selection_; changed(); }
            if(key==UiKey::Down && selection_<1) { ++selection_; changed(); }
            if(key==UiKey::Enter) {
                if(selection_ || !view.summary.session_id) browseAction(BrowseAction::Reload);
                else if(view.summary.records) browseAction(BrowseAction::Points);
            }
        }
        return;
    }
    if (card_info_) {
        if (key == UiKey::Escape || key == UiKey::Enter) { card_info_ = false; changed(); }
        return;
    }
    if (confirming_) {
        if (key == UiKey::Escape) { confirming_ = false; changed(); }
        else if (key == UiKey::Up || key == UiKey::Down) { confirm_yes_ = !confirm_yes_; changed(); }
        else if (key == UiKey::Enter) {
            confirming_ = false;
            if (!confirm_yes_) { changed(); return; }
            if (page_ == SettingsPage::Reset) {
                action(ActionKind::ResetSettings);
                action_.settings = defaultSettings();
            } else {
                action(ActionKind::Storage);
                action_.storage_operation = StorageOperation::Format;
            }
            action_.confirmed = true;
        }
        return;
    }
    if (editing_) {
        if (key == UiKey::Escape) { editing_ = false; changed(); }
        else if (key == UiKey::Left || key == UiKey::Right) {
            const unsigned count = settingChoices(selection_);
            edit_value_ = (edit_value_ + (key == UiKey::Right ? 1 : count - 1)) % count;
            changed();
        } else if (key == UiKey::Enter) {
            Settings value = data_.settings;
            if (setSettingValue(value, selection_, edit_value_)) {
                action(ActionKind::SaveSettings);
                action_.settings = value;
                editing_ = false;
            }
        }
        return;
    }
    if (key == UiKey::Escape) {
        if (page_ == SettingsPage::Root) action(ActionKind::ChangeScreen, ScreenId::LivePlace);
        else { page_ = SettingsPage::Root; selection_ = 0; changed(); }
        return;
    }
    if (page_ == SettingsPage::Licenses) {
        if (key == UiKey::Up && license_offset_ > 0) { --license_offset_; changed(); }
        if (key == UiKey::Down && license_offset_ + SettingsLayout::LicenseRows < licenseLineCount()) { ++license_offset_; changed(); }
        return;
    }
    const unsigned count = page_ == SettingsPage::Operation ? SettingCount : page_ == SettingsPage::Reset ? 1 : 4;
    if (key == UiKey::Up && selection_ > 0) { --selection_; changed(); }
    if (key == UiKey::Down && static_cast<unsigned>(selection_) + 1 < count) { ++selection_; changed(); }
    if (key != UiKey::Enter) return;
    if (page_ == SettingsPage::Root) {
        page_ = static_cast<SettingsPage>(selection_ + 1);
        selection_ = 0; license_offset_ = 0; changed();
    } else if (page_ == SettingsPage::Operation && settingEditable(selection_) && !data_.storage.busy) {
        edit_value_ = settingValue(data_.settings, selection_);
        editing_ = true; changed();
    } else if (page_ == SettingsPage::Reset && !data_.storage.busy) {
        confirming_ = true; confirm_yes_ = false; changed();
    } else if (page_ == SettingsPage::Storage && !data_.storage.busy) {
        if (selection_ == 0) { card_info_ = true; changed(); }
        else if (selection_ == 3) { confirming_ = true; confirm_yes_ = false; changed(); }
        else if (selection_ == 1 || selection_ == 2) {
            action(ActionKind::Storage);
            action_.storage_operation = selection_ == 1 ? StorageOperation::Remount : StorageOperation::Unmount;
        }
    }
}

void UiModel::browseAction(BrowseAction kind,uint32_t index) {
    action(ActionKind::Browse);
    action_.browse_action=kind; action_.browse_index=index; action_.browse_revision=data_.browse.revision;
}

void UiModel::action(ActionKind kind, ScreenId target) {
    if (++next_action_id_ == 0) ++next_action_id_;
    action_ = {};
    action_.kind = kind; action_.action_id = next_action_id_;
    action_.screen_generation = generation_; action_.target = target;
    changed();
}

void UiModel::tick(uint32_t now_ms) {
    const auto previous_second = clock_.secondsJst();
    clock_.tick(now_ms);
    if (screen_ == ScreenId::LivePlace && mode_ == DisplayMode::View &&
        clock_.secondsJst() != previous_second) redraw_ = true;
    const bool was_lit = sdAccessLit();
    last_tick_ms_ = now_ms;
    if (sdAccessLit() != was_lit) redraw_ = true;
    if (screen_ == ScreenId::Boot || datasetWarning()) return;  // Keep warnings visible until acknowledged.
    if (data_.message[0] && now_ms - data_.message_at_ms >= 3000u) {
        data_.message[0] = '\0';
        redraw_ = true;  // Local toast expiry does not notify App.
    }
    if (!last_input_ms_) last_input_ms_ = now_ms;
    if (mode_ != DisplayMode::View || generation_ == 0 || action_.kind != ActionKind::None ||
        !validSettings(data_.settings)) return;
    const auto timeout = ScreenTimeouts[data_.settings.screen_timeout];
    if (timeout && now_ms - last_input_ms_ >= timeout * 1000u) {
        editing_ = confirming_ = false;
        action(ActionKind::Sleep);
    }
}

UiSnapshot UiModel::snapshot() const {
    return {screen_, generation_, revision_, last_input_ms_, selection_, action_, page_};
}

bool UiModel::takeRedraw() {
    const bool value = redraw_;
    redraw_ = false;
    return value;
}

}  // namespace geoputer

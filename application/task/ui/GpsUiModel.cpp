#include "GpsUiModel.h"
#include "../../LogReplay.h"
#include "AlarmText.h"
#include "PlacePresentation.h"
#include "LicenseText.h"
#include "SettingsLayout.h"
#include "ScreenNavigation.h"
#include "GpsControls.h"
#include "DatasetPresentation.h"
#include "SettingsAvailability.h"
#include "../storage/dataset/DatasetReadMemory.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace geoputer {

static bool switchingMode(const UiData& data) {
    return data.storage.busy && (data.storage.operation == StorageOperation::EnterUsbMode ||
                                 data.storage.operation == StorageOperation::EnterWifiMode);
}

OperationStatus GpsUiModel::setScreen(const SetUiScreen& command) {
    if (command.screen != ScreenId::LivePlace && command.screen != ScreenId::Settings && command.screen != ScreenId::Boot && command.screen != ScreenId::Menu && command.screen != ScreenId::Map && command.screen != ScreenId::GpsLogger && !isLogBrowser(command.screen)) {
        return OperationStatus::InvalidRequest;
    }
    if (command.generation <= generation_) return OperationStatus::StaleContent;
    // Switching screens cannot silently erase an unprocessed confirmed action.
    if (action_.kind != ActionKind::None && (command.processed_action_id != action_.action_id || command.action_result == ActionResult::None)) {
        return OperationStatus::InvalidRequest;
    }
    acknowledge(command.processed_action_id, command.action_result);
    if (screen_ == ScreenId::Boot || command.screen == ScreenId::Boot) last_input_ms_ = 0;
    dataset_owned_=dataset_publishable_=false;
    screen_resources_.enter(screenGroup(command.screen));
    screen_ = command.screen;
    mode_ = command.mode;
    generation_ = command.generation;
    copyData(command.data);
    selection_ = command.screen==ScreenId::Logs ? command.data.browse().selected : 0;
    dialog_.close();
    redraw_ = true;
    if (early_maintenance_key_) {
        early_maintenance_key_ = false;
        if (screen_ == ScreenId::Boot) action(ActionKind::Maintenance, ScreenId::Settings);
    }
    updateScreenName();
    return OperationStatus::Ok;
}

OperationStatus GpsUiModel::updateContent(const UpdateUiContent& command) {
    if (command.generation != generation_ || command.data.revision < data_.revision) {
        return OperationStatus::StaleContent;
    }
    copyData(command.data);
    mode_ = command.mode;
    acknowledge(command.processed_action_id, command.action_result);
    redraw_ = true;
    updateScreenName();
    return OperationStatus::Ok;
}

void GpsUiModel::copyData(const UiData& data) {
    if (data.gps.fix.valid) clock_.setInitialUtc(data.gps.fix.utc_ms, last_tick_ms_);
    const auto previous_sd = data_.storage.sd;
    const auto previous_rg = data_.storage.rg;
    if (newerRevision(data.storage.sd.revision, previous_sd.revision) && (!sd_seen_ || data.storage.sd.access_id != previous_sd.access_id)) {
        access_started_ms_ = last_tick_ms_; sd_seen_ = true;
    }
    if(data.storage.dataset_failure.revision!=data_.storage.dataset_failure.revision)
        dataset_notice_started_ms_=last_tick_ms_;
    const auto old_browse=data_.browse().revision;
    const auto old_dataset_revision=data_.datasets().revision;

    // V switches modes inside LogPoint without SetUiScreen. The old images
    // are no longer visible; reject late commits but retain any outstanding loan.
    if(data.dataset.generation!=data_.dataset.generation || (showingMap(screen_,data_) && !showingMap(screen_,data)))screen_resources_.map.clear();
    data_.copyFrom(data,dataset_owned_ && screen_==ScreenId::Settings);updateMapCursor();
    // Activity is global; an older job snapshot cannot roll it back.
    if (newerRevision(previous_sd.revision, data_.storage.sd.revision)) data_.storage.sd = previous_sd;
    if (newerRevision(previous_rg.revision, data_.storage.rg.revision)) data_.storage.rg = previous_rg;
    for (auto& line : data_.place) line[sizeof(line) - 1] = '\0';
    if (data_.hasBrowse()) {
        auto& browse=data_.editBrowse();
        for(auto& name:browse.names) name[LogNameBytes-1]='\0';
        browse.name[LogNameBytes-1]='\0';
    }

    if(screen_==ScreenId::Logs && !data_.browse().loading && data_.browse().revision!=old_browse) {
        selection_=data_.browse().selected;
        dialog_.close(); // A refreshed list invalidates the previously shown target.
    }
    if(old_dataset_revision!=data_.datasets().revision && !data_.datasets().loading)datasetChanged();
    if(screen_==ScreenId::Boot) {
        const auto prompt=data_.boot.setup_prompt;
        const auto expected=prompt==BootSetupPrompt::Format?DialogKind::BootFormat:
                            prompt==BootSetupPrompt::Directories?DialogKind::BootDirectories:DialogKind::None;
        if(expected==DialogKind::None && (dialog_.kind()==DialogKind::BootFormat || dialog_.kind()==DialogKind::BootDirectories))dialog_.close();
        else if(expected!=DialogKind::None && dialog_.kind()!=expected)dialog_.open(expected);
    }
    updateDialogState();
    data_.message[sizeof(data_.message) - 1] = '\0';
}

void GpsUiModel::updateDialogState() {
    if (dialog_.kind() == DialogKind::DeleteLog)
        dialog_.setActionEnabled(canDeleteLog());
    if (dialog_.kind() == DialogKind::EnterUsbMode || dialog_.kind()==DialogKind::EnterWifiMode) dialog_.setActionEnabled(screen_resources_.settings.usb_mode && usbEntryAllowed(data_));
    if (dialog_.kind() == DialogKind::ClearDataset || dialog_.kind() == DialogKind::RebuildIndex || dialog_.kind() == DialogKind::VerifyIndex)
        dialog_.setActionEnabled(data_.datasets().purpose==screen_resources_.settings.dataset.purpose && selection_<data_.datasets().count &&
            data_.datasets().rows[selection_].id==screen_resources_.settings.dataset.dialog_id && datasetRowBlock(data_,selection_)==DatasetError::None);
    if (dialog_.kind() == DialogKind::GpsControl) {
        if (gps_dialog_state_ != data_.recording.state) dialog_.close();
        else for (unsigned i = 0; i < dialog_.choices(); ++i)
            dialog_.setChoiceEnabled(i, gpsControlAllowed(data_, gpsControlAction(gps_dialog_state_, i)));
    }
}

void GpsUiModel::acknowledge(uint32_t action_id, ActionResult result) {
    if (action_.kind != ActionKind::None && action_id == action_.action_id && result != ActionResult::None) {
        action_ = {};
    }
}

void GpsUiModel::changed() {
    updateScreenName();
    ++revision_;
    notification_pending_ = true;
    redraw_ = true;
}

UiKeyGuide GpsUiModel::keyGuide() const {
    UiKeyGuide guide;
    guide.accepting=action_.kind==ActionKind::None && !switchingMode(data_) &&
        !(isLogBrowser(screen_) && data_.browse().deleting);

    guide.root=!data_.boot.maintenance && isScreenRoot(screen_,screen_resources_.settings.page) && !screen_resources_.settings.editing && !screen_resources_.settings.card_info;
    guide.system=!data_.boot.maintenance;
    const bool dataset_page=screen_==ScreenId::Settings && isDatasetSettingsPage(screen_resources_.settings.page);
    const bool dataset_wait=dataset_page && data_.datasets().applying;
    guide.back=guide.accepting && !dataset_wait &&
        !(screen_==ScreenId::Settings && screen_resources_.settings.page==SettingsPage::DatasetIndex && data_.storage.busy);
    auto list=[&](unsigned count) {
        guide.vertical=true;guide.up=selection_>0;guide.down=selection_+1u<count;
    };
    if (screen_==ScreenId::Logs) {
        const auto& view=data_.browse();
        guide.vertical=true;guide.up=!view.loading && (selection_>0 || view.has_newer);
        guide.down=!view.loading && (selection_+1u<view.count || view.has_older);
        guide.remove=!view.loading && canDeleteLog();
    } else if (screen_==ScreenId::LogPoint) {
        const auto& view=data_.browse();guide.vertical=guide.horizontal=true;
        if(view.map_mode && view.playing) {
            guide.up=replay::speed(view)+1<replay::SpeedCount;guide.down=replay::speed(view)>0;
            guide.left=!view.replay_reverse;guide.right=view.replay_reverse;
        } else {
            guide.up=guide.left=view.summary.records && view.target>0;
            guide.down=guide.right=uint64_t(view.target)+1<view.summary.records;
        }
    } else if (screen_==ScreenId::Menu) list(MenuCount);
    else if (screen_==ScreenId::Settings && !screen_resources_.settings.card_info) {
        if (screen_resources_.settings.page==SettingsPage::Licenses) {
            guide.vertical=guide.horizontal=true;
            guide.up=licenseTarget(UiKey::Up)!=screen_resources_.settings.license_offset;guide.down=licenseTarget(UiKey::Down)!=screen_resources_.settings.license_offset;
            guide.left=licenseTarget(UiKey::Left)!=screen_resources_.settings.license_offset;guide.right=licenseTarget(UiKey::Right)!=screen_resources_.settings.license_offset;
        } else if (screen_resources_.settings.page==SettingsPage::DatasetDetail) {
            const bool ready=datasetViewReady(data_);
            const auto count=ready?selectionCount():0;
            const auto last=count>DatasetDetailVisibleRows?count-DatasetDetailVisibleRows:0;
            guide.vertical=guide.horizontal=true;
            guide.up=guide.left=ready && selection_>0;
            guide.down=guide.right=ready && selection_<last;
        } else if (screen_resources_.settings.page==SettingsPage::DatasetList) {
            const auto& view=data_.datasets();guide.vertical=true;
            const bool ready=datasetViewReady(data_);
            guide.up=ready && (selection_>0 || view.has_older);guide.down=ready && (selection_+1u<view.count || view.has_newer);
        } else if (screen_resources_.settings.page!=SettingsPage::About && screen_resources_.settings.page!=SettingsPage::Reset && screen_resources_.settings.page!=SettingsPage::DatasetIndex) {
            const bool ready=screen_resources_.settings.page==SettingsPage::DatasetChoice?
                datasetChoiceReady(data_):!dataset_wait;
            list(ready?selectionCount():0);
            if (!ready) guide.up=guide.down=false;
            if(ready && screen_resources_.settings.page==SettingsPage::DatasetChoice){
                if(selection_==0){
                    guide.left=screen_resources_.settings.dataset.title_offset>0;
                    guide.right=uiTextCanScroll(data_.datasets().info.title,data_.settings.language==UiLanguage::Japanese,
                        screen_resources_.settings.dataset.title_offset,200);
                    guide.horizontal=guide.left||guide.right;
                } else if(selection_==1 && data_.datasets().info.language_count>1){
                    guide.horizontal=guide.left=guide.right=true;
                }
            }
        }
    }
    if(screen_==ScreenId::Settings && !screen_resources_.settings.card_info && screen_resources_.settings.page!=SettingsPage::Licenses) {
        if(!isDatasetBrowserPage(screen_resources_.settings.page)){
            guide.horizontal=editableSettingChoices()>1;
            guide.left=guide.right=guide.horizontal;
        }
        if(editDirty() && data_.storage.busy && screen_resources_.settings.page!=SettingsPage::System)
            guide.up=guide.down=false;
    }
    if(showingMap(screen_,data_)) {
        guide.zoom_in=map::supportedScale(data_.map.scale) && data_.map.scale>1;
        guide.zoom_out=map::supportedScale(data_.map.scale) && data_.map.scale<4;
    }
    if (!guide.accepting) guide.up=guide.down=guide.left=guide.right=guide.remove=guide.zoom_in=guide.zoom_out=false;
    return guide;
}

// Use the same numeric destinations as direct presses; skip the current view
// and unavailable destinations. Other means that cycling has no destination.
UiKey GpsUiModel::nextNumericKey() const {
    const auto guide=keyGuide();
    if(mode_!=DisplayMode::View || dialog_.active() || !guide.accepting || !guide.system)return UiKey::Other;
    if(guide.root) {
        for(unsigned i=0;i<MenuCount;++i)if(MenuScreens[i].screen==screen_)return MenuScreens[(i+1)%MenuCount].key;
        return MenuScreens[0].key; // MENU itself is not a numeric destination.
    }
    if(isLogViewScreen(screen_)) {
        constexpr unsigned count=sizeof(LogViewShortcuts)/sizeof(*LogViewShortcuts);
        unsigned start=0;
        for(unsigned i=0;i<count;++i)if(selectedLogView(screen_,data_,LogViewShortcuts[i].action))start=(i+1)%count;
        for(unsigned n=0;n<count;++n) {
            const auto& item=LogViewShortcuts[(start+n)%count];
            if(!selectedLogView(screen_,data_,item.action) && logViewAvailable(screen_,data_,item.action))return item.key;
        }
    }
    return UiKey::Other;
}

// Only advertise Enter where it selects, confirms, retries or returns.
// Keep disabled choices visible in the same way as other local operations.
void GpsUiModel::appendEnterShortcut(UiShortcutGuide& row,bool accepting) const {
    bool enabled=true;
    if(dialog_.active()) enabled=dialog_.choiceEnabled(dialog_.choice());
    else if(screen_==ScreenId::Boot) {
        if(data_.boot.maintenance || !data_.boot.complete)return;
    } else if(screen_==ScreenId::Logs) enabled=!data_.browse().loading;
    else if(screen_==ScreenId::LogProperties) {
        const auto& view=data_.browse();
        enabled=!view.loading && view.error==LogReadError::None && view.summary.records;
    } else if(screen_==ScreenId::Settings) {
        const auto& settings=screen_resources_.settings;
        if(!settings.card_info) switch(settings.page) {
            case SettingsPage::Licenses:return;
            case SettingsPage::Alarm:if(selection_<AlarmReloadRow && !editDirty())return;enabled=alarmSettingEnabled(data_,selection_);break;
            case SettingsPage::DatasetMenu:enabled=!data_.datasets().applying && datasetMenuBlock(data_,selection_)==DatasetError::None;break;
            case SettingsPage::DatasetList: {
                const auto& view=data_.datasets();
                enabled=!view.loading && !view.applying && (!datasetViewReady(data_) ||
                    (selection_<view.count && datasetRowBlock(data_,selection_)==DatasetError::None));break;
            }
            case SettingsPage::DatasetChoice: {
                if(data_.datasets().loading || data_.datasets().applying || selection_<2)return;
                enabled=selection_==2?data_.datasets().error==DatasetError::None:datasetChoiceCanApply(data_);
                break;
            }
            case SettingsPage::DatasetDetail:return;
            case SettingsPage::DatasetIndex:enabled=!data_.datasets().applying && !data_.storage.busy;break;
            case SettingsPage::System:
                if(selection_==0 && !editDirty())return;
                if(selection_!=0 && selection_!=2 && selection_!=SettingsSystemResetRow)return;
                enabled=systemSettingEnabled(data_,settings.usb_mode,selection_);
                break;
            case SettingsPage::Operation:
                if(!editDirty())return;
                enabled=operationSettingEnabled(data_,selection_);
                break;
            case SettingsPage::Storage:case SettingsPage::Reset:enabled=!data_.storage.busy;break;
            default:break;
        }
    } else if(screen_!=ScreenId::GpsLogger && screen_!=ScreenId::Menu)return;
    row.key(UiKey::Enter,accepting && enabled);
}

UiShortcutGuide GpsUiModel::shortcutGuide() const {
    UiShortcutGuide row;
    if(mode_==DisplayMode::Background)return row;
    const auto guide=keyGuide();
    const auto active=guide.accepting;
    if(audio_active_ && !audio_preview_) {row.key(UiKey::Escape,active);return row;}
    if(dialog_.active()) {
        row.key(UiKey::Escape,active);row.group();appendEnterShortcut(row,active);
        bool left=false,right=false;
        for(unsigned i=0;i<dialog_.choices();++i)if(dialog_.choiceEnabled(i)) {
            left|=i<dialog_.choice();right|=i>dialog_.choice();
        }
        row.key(UiKey::Left,active && left);row.key(UiKey::Right,active && right);return row;
    }
    if(screen_==ScreenId::Boot) {row.key(UiKey::Escape,active);row.group();appendEnterShortcut(row,active);return row;}


    // Global shortcuts always precede numeric destinations and local operations.
    row.key(UiKey::Escape,guide.back);
    if(guide.system) {row.key(UiKey::ScreenOff,active);row.key(UiKey::GpsControl,active);}
    if(guide.root) {
        row.group();for(const auto& item:MenuScreens)row.key(item.key,active,item.screen==screen_);
    } else if(isLogViewScreen(screen_)) {
        row.group();for(const auto& item:LogViewShortcuts)
            row.key(item.key,active && logViewAvailable(screen_,data_,item.action),selectedLogView(screen_,data_,item.action));
    }
    row.group();
    appendEnterShortcut(row,active);
    const auto& view=data_.browse();
    if(screen_==ScreenId::LogPoint) {
        const bool points=view.summary.records!=0;
        row.key(UiKey::FirstPoint,active && points && view.target>0);
        row.key(UiKey::LastPoint,active && uint64_t(view.target)+1<view.summary.records);
        if(view.map_mode) {
            row.key(UiKey::ZoomIn,guide.zoom_in);row.key(UiKey::ZoomOut,guide.zoom_out);
            row.key(UiKey::Playback,active && points && view.error==LogReadError::None);
        }
    } else if(screen_==ScreenId::LogStatistics) {
        row.key(UiKey::Recalculate,active && !view.loading);
    } else if(screen_==ScreenId::Logs)row.key(UiKey::DeleteLog,guide.remove);
    else if(screen_==ScreenId::Map) {row.key(UiKey::ZoomIn,guide.zoom_in);row.key(UiKey::ZoomOut,guide.zoom_out);}
    if(guide.horizontal) {row.key(UiKey::Left,guide.left);row.key(UiKey::Right,guide.right);}
    if(guide.vertical) {row.key(UiKey::Up,guide.up);row.key(UiKey::Down,guide.down);}
    return row;
}

void GpsUiModel::keyPressed(UiKey key, uint32_t now_ms) {
    last_input_ms_ = now_ms;
    // Physical Del remains Back/Cancel, but cannot select maintenance.
    const bool maintenance_key = key == UiKey::Escape;
    if (key == UiKey::Back) key = UiKey::Escape;
    if (generation_ == 0) {
        if (maintenance_key) early_maintenance_key_ = true;
        return;
    }
    if (action_.kind != ActionKind::None || switchingMode(data_)) return;
    if (screen_ == ScreenId::Boot) {
        if (dialog_.kind()==DialogKind::BootFormat || dialog_.kind()==DialogKind::BootDirectories) {
            const auto result=dialog_.keyPressed(key);
            if(result==DialogInput::Accepted || result==DialogInput::Cancelled) {
                action(ActionKind::BootSetup);action_.confirmed=result==DialogInput::Accepted;
            } else if(result==DialogInput::Changed)changed();
            return;
        }
        if (maintenance_key) action(ActionKind::Maintenance, ScreenId::Settings);
        else if (!data_.boot.maintenance && data_.boot.complete && key == UiKey::Enter) action(ActionKind::ChangeScreen, ScreenId::LivePlace);
        return;
    }
    if(audio_active_ && !audio_preview_) {action(ActionKind::SilenceAlarm);return;}
    if (mode_ == DisplayMode::Background) { action(ActionKind::Wake); return; }
    if (dialog_.active()) {
        updateDialogState();
        if (!dialog_.active()) { changed(); return; }
        const auto kind = dialog_.kind();
        const auto choice = dialog_.choice();
        const auto result = dialog_.keyPressed(key);
        if (kind == DialogKind::GpsControl && result == DialogInput::Accepted) {
            const auto operation = gpsControlAction(gps_dialog_state_, choice);
            if (gps_dialog_state_ == data_.recording.state && gpsControlAllowed(data_, operation)) {
                action(ActionKind::Logging); action_.log_action = operation; action_.confirmed = true;
            } else changed();
            return;
        }
        if (result == DialogInput::Accepted) acceptDialog(kind);
        else if (result != DialogInput::Ignored) changed();
        return; // A modal owns all keys, including screen shortcuts.
    }


    if (isLogBrowser(screen_) && data_.browse().deleting) return;
    if (!data_.boot.maintenance) {
        if (key == UiKey::ScreenOff) { action(ActionKind::Sleep); return; }
        if (key == UiKey::GpsControl) {
            if(viewingLogPoint(screen_) && data_.browse().playing) {
                data_.editBrowse().playing=false;browseAction(BrowseAction::PausePlayback);
            }
            openGpsControls(); return; }
    }
    if(key==UiKey::Tab) {key=nextNumericKey();if(key==UiKey::Other)return;}
    if (!keyGuide().allowsKey(key)) return;
    if (!data_.boot.maintenance && isScreenRoot(screen_, screen_resources_.settings.page) && !screen_resources_.settings.editing && !screen_resources_.settings.card_info) {
        if (screen_ == ScreenId::Menu) {
            if (key == UiKey::Up) { --selection_; changed(); return; }
            if (key == UiKey::Down) { ++selection_; changed(); return; }
            if (key == UiKey::Enter) { action(ActionKind::ChangeScreen, MenuScreens[selection_].screen); return; }
        }
        if(key==UiKey::Escape) {
            // Only LIVE opens the selector. Del has no Back destination on LIVE.
            if(screen_!=ScreenId::LivePlace || maintenance_key)
                action(ActionKind::ChangeScreen,screen_==ScreenId::LivePlace?ScreenId::Menu:ScreenId::LivePlace);
            return;
        }
        ScreenId target;
        if(screenShortcut(key,target)) {
            if(target!=screen_)action(ActionKind::ChangeScreen,target);
            return;
        }
    }
    if (screen_ == ScreenId::Menu) return;
    if(showingMap(screen_,data_) && (key==UiKey::ZoomIn || key==UiKey::ZoomOut)) {
        action(ActionKind::MapScale);
        action_.map_scale=key==UiKey::ZoomIn?data_.map.scale/2:data_.map.scale*2;
        return;
    }
    if (screen_ == ScreenId::Map) {
        if(key==UiKey::Escape)action(ActionKind::ChangeScreen);
        return;
    }
    if (screen_ == ScreenId::LivePlace) return;
    if (screen_ == ScreenId::GpsLogger) {
        gpsLoggerKey(key);
        return;
    }
    if(isLogBrowser(screen_)) {
        logBrowserKey(key);
        return;
    }
    settingsKey(key);
}

void GpsUiModel::acceptDialog(DialogKind kind) {
    switch (kind) {
    case DialogKind::RegenerateWifi: case DialogKind::ExitUsbMode: case DialogKind::ExitWifiMode: break;
        case DialogKind::RestartLogger:
            action(ActionKind::RestartLogger); action_.confirmed = true; break;
        case DialogKind::DeleteLog:
            browseAction(BrowseAction::Delete, selection_);
            break;
        case DialogKind::FormatSd:
            action(ActionKind::Storage); action_.storage_operation = StorageOperation::Format;
            break;
        case DialogKind::ResetSettings:
            action(ActionKind::ResetSettings); action_.settings = defaultSettings();
            break;
        case DialogKind::EnterWifiMode:
            action(ActionKind::Storage);
            action_.storage_operation=StorageOperation::EnterWifiMode;
            break;
        case DialogKind::EnterUsbMode:
            action(ActionKind::Storage);
            action_.storage_operation = StorageOperation::EnterUsbMode;
            break;
        case DialogKind::ClearDataset:
            datasetAction(DatasetAction::Clear);break;
        case DialogKind::RebuildIndex:case DialogKind::VerifyIndex:
            datasetAction(kind==DialogKind::RebuildIndex?DatasetAction::RebuildIndex:DatasetAction::VerifyIndex,selection_);
            screen_resources_.settings.page=SettingsPage::DatasetIndex;changed();break;
        case DialogKind::GpsControl: case DialogKind::BootFormat: case DialogKind::BootDirectories:
        case DialogKind::None: return;
    }
    action_.confirmed = true;
}

void GpsUiModel::action(ActionKind kind, ScreenId target) {
    if (++next_action_id_ == 0) ++next_action_id_;
    action_ = {};
    action_.kind = kind; action_.action_id = next_action_id_;
    action_.screen_generation = generation_; action_.target = target;
    changed();
}

void GpsUiModel::updateScreenName() {
    if(!generation_)return;
    const uint16_t key=uint16_t(screen_);
    if(key==screen_name_key_)return;
    screen_name_key_=key;screen_name_started_ms_=last_tick_ms_;
    screen_name_active_=hasScreenName(screen_) && mode_==DisplayMode::View;
    redraw_=true;
}

void GpsUiModel::tick(uint32_t now_ms) {
    updateScreenName();
    const bool was_screen_name=screenNameVisible();
    const auto previous_second = clock_.secondsJst();
    clock_.tick(now_ms);
    if (hasRuntimeHeader(screen_) && mode_ == DisplayMode::View && clock_.secondsJst() != previous_second) redraw_ = true;
    const bool was_lit = sdAccessLit();
    const bool was_dataset_notice=datasetFailureVisible();
    last_tick_ms_ = now_ms;
    if(screen_name_active_ && now_ms-screen_name_started_ms_>=ScreenNameMs)screen_name_active_=false;
    if(was_screen_name!=screenNameVisible())redraw_=true;
    // Start the visible interval when Live Place can actually show the notice.
    if(screen_!=ScreenId::LivePlace || mode_==DisplayMode::Background) dataset_notice_started_ms_=now_ms;
    if(was_dataset_notice!=datasetFailureVisible())redraw_=true;
    if (sdAccessLit() != was_lit) redraw_ = true;
    // Keep a requested scan visible; the inactivity interval starts at completion.
    // Explicit S still enters Background through the normal key handler.
    if(screen_==ScreenId::LogStatistics && data_.browse().loading) {last_input_ms_=now_ms;return;}
    if (screen_ == ScreenId::Boot || switchingMode(data_) || data_.datasets().applying || (screen_==ScreenId::Logs && data_.browse().deleting)) return;
    if (data_.message[0] && now_ms - data_.message_at_ms >= 3000u) {
        data_.message[0] = '\0';
        redraw_ = true;  // Local toast expiry does not notify App.
    }
    if (!last_input_ms_) last_input_ms_ = now_ms;
    if (data_.boot.maintenance || mode_ != DisplayMode::View || generation_ == 0 || action_.kind != ActionKind::None || !validSettings(data_.settings)) return;
    const auto timeout = ScreenTimeouts[data_.settings.screen_timeout];
    if (timeout && now_ms - last_input_ms_ >= timeout * 1000u) {
        screen_resources_.settings.editing = false; dialog_.close();
        action(ActionKind::Sleep);
    }
}

UiSnapshot GpsUiModel::snapshot() const {
    return {screen_, generation_, revision_, last_input_ms_, selection_, action_, screen_resources_.settings.page};
}

bool GpsUiModel::takeRedraw() {
    updateScreenName();
    const bool value = redraw_;
    redraw_ = false;
    return value;
}

}  // namespace geoputer

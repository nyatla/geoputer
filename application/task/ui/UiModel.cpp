#include "UiModel.h"
#include "PlacePresentation.h"
#include "LicenseText.h"
#include "SettingsLayout.h"
#include "ScreenNavigation.h"
#include "DatasetPresentation.h"
#include <cstring>

namespace geoputer {

static bool switchingMode(const UiData& data) {
    return data.storage.busy && (data.storage.operation == StorageOperation::EnterUsbMode ||
                                 data.storage.operation == StorageOperation::ExitUsbMode);
}

OperationStatus UiModel::setScreen(const SetUiScreen& command) {
    if (command.screen != ScreenId::LivePlace && command.screen != ScreenId::Settings && command.screen != ScreenId::Boot &&
        command.screen != ScreenId::GpsLogger && command.screen != ScreenId::UsbStorage && !isLogBrowser(command.screen)) {
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
    usb_page_=usb_return_page_=UsbPage::Menu;usb_file_=0;
    copyData(command.data);
    selection_ = command.screen==ScreenId::Logs ? command.data.browse.selected : 0;
    page_ = SettingsPage::Root;
    dataset_purpose_=DatasetPurpose::Select;dataset_list_start_=0;
    editing_ = false; dialog_.close();
    license_offset_ = 0;
    card_info_ = false;
    redraw_ = true;
    if (early_maintenance_key_) {
        early_maintenance_key_ = false;
        if (screen_ == ScreenId::Boot) action(ActionKind::Maintenance, ScreenId::Settings);
    }
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
    if (data.gps.fix.valid) clock_.setInitialUtc(data.gps.fix.utc_ms, last_tick_ms_);
    const auto previous_sd = data_.storage.sd;
    const auto previous_rg = data_.storage.rg;
    if (newerRevision(data.storage.sd.revision, previous_sd.revision) &&
        (!sd_seen_ || data.storage.sd.access_id != previous_sd.access_id)) {
        access_started_ms_ = last_tick_ms_; sd_seen_ = true;
    }
    if(data.storage.dataset_failure.revision!=data_.storage.dataset_failure.revision)
        dataset_notice_started_ms_=last_tick_ms_;
    const auto old_browse=data_.browse.revision;
    const auto old_dataset_revision=data_.datasets.revision;
    const auto old_export=data_.storage.export_job.phase;
    data_ = data;
    // Activity is global; an older job snapshot cannot roll it back.
    if (newerRevision(previous_sd.revision, data_.storage.sd.revision)) data_.storage.sd = previous_sd;
    if (newerRevision(previous_rg.revision, data_.storage.rg.revision)) data_.storage.rg = previous_rg;
    for (auto& line : data_.place) line[sizeof(line) - 1] = '\0';
    for(auto& name:data_.browse.names) name[LogNameBytes-1]='\0';
    data_.browse.name[LogNameBytes-1]='\0';
    data_.storage.export_job.name[LogNameBytes-1]='\0';
    if(screen_==ScreenId::UsbStorage) {
        const auto phase=data_.storage.export_job.phase;
        if(phase!=old_export) {
            dialog_.close();
            switch(phase) {
                case ExportPhase::Idle:usb_page_=UsbPage::Menu;selection_=0;break;
                case ExportPhase::WaitEject:usb_page_=UsbPage::WaitEject;break;
                case ExportPhase::Selecting:usb_page_=UsbPage::Files;selection_=data_.browse.selected;break;
                case ExportPhase::Running:usb_page_=UsbPage::Progress;break;
                case ExportPhase::Complete:usb_page_=UsbPage::Complete;break;
                case ExportPhase::Failed:usb_page_=UsbPage::Error;break;
                default:usb_page_=UsbPage::Busy;break;
            }
        }
        if(phase==ExportPhase::Selecting && !data_.browse.loading && old_browse!=data_.browse.revision) {
            usb_page_=UsbPage::Files;selection_=data_.browse.selected;
        }
    }
    if(screen_==ScreenId::Logs && !data_.browse.loading && data_.browse.revision!=old_browse) {
        selection_=data_.browse.selected;
        dialog_.close(); // A refreshed list invalidates the previously shown target.
    }
    if (screen_ == ScreenId::Settings &&
        (page_ == SettingsPage::DatasetList || page_ == SettingsPage::DatasetDetail) &&
        old_dataset_revision != data_.datasets.revision && !data_.datasets.loading) {
        if (data_.datasets.detail) {
            if (page_ != SettingsPage::DatasetDetail) { selection_ = 0; editing_ = false; }
            page_ = SettingsPage::DatasetDetail;
            const auto count = datasetDetailRows(data_.datasets, data_.settings.language == UiLanguage::Japanese);
            if (selection_ >= count) selection_ = count-1;
        } else {
            page_ = SettingsPage::DatasetList; selection_ = data_.datasets.selected; editing_ = false;
            if (data_.datasets.count) dataset_list_start_ = data_.datasets.rows[0].number;
        }
    }
    if (dialog_.kind() == DialogKind::DeleteLog)
        dialog_.setActionEnabled(selection_ < data_.browse.count && !(data_.browse.active_mask & (1u << selection_)));
    if (dialog_.kind() == DialogKind::EnterUsbMode) dialog_.setActionEnabled(system_usb_mode_ && usbEntryAllowed(data_));
    if (dialog_.kind() == DialogKind::ClearDataset || dialog_.kind() == DialogKind::RebuildIndex || dialog_.kind() == DialogKind::VerifyIndex)
        dialog_.setActionEnabled(data_.datasets.purpose==dataset_purpose_ && selection_<data_.datasets.count &&
            data_.datasets.rows[selection_].number==dataset_dialog_number_ && datasetRowBlock(data_,selection_)==DatasetError::None);
    data_.message[sizeof(data_.message) - 1] = '\0';
}

void UiModel::acknowledge(uint32_t action_id, ActionResult result) {
    if (action_.kind != ActionKind::None && action_id == action_.action_id &&
        result != ActionResult::None) {
        if(result==ActionResult::Rejected && action_.kind==ActionKind::UsbExport)usb_page_=usb_return_page_;
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
    // Physical Del remains Back/Cancel, but cannot select maintenance.
    const bool maintenance_key = key == UiKey::Escape;
    if (key == UiKey::Back) key = UiKey::Escape;
    if (generation_ == 0) {
        if (maintenance_key) early_maintenance_key_ = true;
        return;
    }
    if (action_.kind != ActionKind::None || switchingMode(data_)) return;
    if (screen_ == ScreenId::Boot) {
        if (maintenance_key) action(ActionKind::Maintenance, ScreenId::Settings);
        else if (!data_.boot.maintenance && data_.boot.complete && key == UiKey::Enter) action(ActionKind::ChangeScreen, ScreenId::LivePlace);
        return;
    }
    if (mode_ == DisplayMode::Background) { action(ActionKind::Wake); return; }
    if (dialog_.active()) {
        const auto kind = dialog_.kind();
        if (kind == DialogKind::DeleteLog)
            dialog_.setActionEnabled(selection_ < data_.browse.count && !(data_.browse.active_mask & (1u << selection_)));
        if (kind == DialogKind::EnterUsbMode) dialog_.setActionEnabled(system_usb_mode_ && usbEntryAllowed(data_));
        if (kind == DialogKind::ClearDataset || kind == DialogKind::RebuildIndex || kind == DialogKind::VerifyIndex)
            dialog_.setActionEnabled(data_.datasets.purpose==dataset_purpose_ && selection_<data_.datasets.count &&
            data_.datasets.rows[selection_].number==dataset_dialog_number_ && datasetRowBlock(data_,selection_)==DatasetError::None);
        const auto result = dialog_.keyPressed(key);
        if (result == DialogInput::Accepted) acceptDialog(kind);
        else if (result != DialogInput::Ignored) changed();
        return; // A modal owns all keys, including screen shortcuts.
    }
    if (screen_ == ScreenId::LivePlace && maintenance_key && placeIssue(data_) != PlaceState::Ready &&
        loggerStopped(data_.recording, data_.storage.log)) {
        action(ActionKind::Maintenance, ScreenId::Settings);
        return;
    }
    if (screen_ == ScreenId::UsbStorage) {
        usbKey(key);
        return;
    }
    if (isLogBrowser(screen_) && data_.browse.deleting) return;
    if (!data_.boot.maintenance && isScreenRoot(screen_, page_) && !editing_ && !card_info_) {
        ScreenId target;
        // Del keeps Back semantics; only the physical Esc is a Settings shortcut.
        if (!(key == UiKey::Escape && !maintenance_key) && screenShortcut(key, target)) {
            if (target != screen_) action(ActionKind::ChangeScreen, target);
            return;
        }
    }
    if (screen_ == ScreenId::LivePlace) {
        if (key == UiKey::PauseLog && data_.recording.state == RecordingState::Recording) {
            dialog_.open(DialogKind::PauseLog); changed();
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
                dialog_.open(DialogKind::StopLog); changed();
            }
        }
        return;
    }
    if(isLogBrowser(screen_)) {
        const auto& view=data_.browse;
        if(view.deleting) return; // Accepted deletion must finish before leaving/sleeping.
        if(key==UiKey::Escape) {
            action(ActionKind::ChangeScreen,screen_==ScreenId::Logs?ScreenId::LivePlace:screen_==ScreenId::LogDetail?ScreenId::Logs:ScreenId::LogDetail);
            return;
        }
        if(screen_==ScreenId::LogPoint) {
            if(key==UiKey::Up && view.target) browseAction(BrowseAction::Point,view.target-1);
            if(key==UiKey::Down && view.target+1<view.summary.records) browseAction(BrowseAction::Point,view.target+1);
            return;
        }
        if(view.loading) return;
        if(screen_==ScreenId::Logs) {
            if(key==UiKey::DeleteLog && selection_<view.count) {
                dialog_.open(DialogKind::DeleteLog, !(view.active_mask & (1u << selection_))); changed();
            } else if(key==UiKey::Up) {
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
    if (screen_ == ScreenId::Settings && (page_ == SettingsPage::DatasetList || page_ == SettingsPage::DatasetDetail)) {
        const auto& view = data_.datasets;
        if (view.applying) return;
        if (key == UiKey::Escape) {
            if (editing_) {
                editing_ = false; datasetAction(DatasetAction::CancelLanguage);
                std::strcpy(action_.settings.wordbook_language,dataset_edit_original_);
            }
            else if (page_ == SettingsPage::DatasetDetail) {
                page_ = SettingsPage::DatasetList; selection_ = 0;
                datasetAction(DatasetAction::Open, dataset_list_start_);
            } else { page_ = SettingsPage::DatasetMenu; selection_ = static_cast<uint8_t>(dataset_purpose_); changed(); }
            return;
        }
        if (view.loading) return;
        if(page_==SettingsPage::DatasetList && view.error!=DatasetError::None) {
            if(key==UiKey::Enter) datasetAction(DatasetAction::Open,dataset_list_start_);
            return;
        }
        if (page_ == SettingsPage::DatasetList) {
            if (key == UiKey::Up) {
                if (selection_) { --selection_; changed(); }
                else if (view.has_older) datasetAction(DatasetAction::Older);
            } else if (key == UiKey::Down) {
                if (selection_+1 < view.count) { ++selection_; changed(); }
                else if (view.has_newer) datasetAction(DatasetAction::Newer);
            } else if (key == UiKey::Enter && view.count && datasetRowBlock(data_,selection_)==DatasetError::None) {
                dataset_dialog_number_=view.rows[selection_].number;
                if(!dataset_dialog_number_) {dialog_.open(DialogKind::ClearDataset);changed();}
                else if(dataset_purpose_!=DatasetPurpose::Select) {
                    dialog_.open(dataset_purpose_==DatasetPurpose::Rebuild?DialogKind::RebuildIndex:DialogKind::VerifyIndex);changed();
                } else datasetAction(DatasetAction::Detail,selection_);
            }
            return;
        }
        if (view.error != DatasetError::None) {
            if (key == UiKey::Enter && datasetRecoverable(view.error) && datasetSelectionBlock(data_,true)==DatasetError::None) datasetAction(DatasetAction::Apply);
            return;
        }
        const unsigned count = datasetDetailRows(view, data_.settings.language == UiLanguage::Japanese);
        if (editing_) {
            if (key == UiKey::Enter) { editing_ = false; changed(); }
            else if (key == UiKey::Left || key == UiKey::Right) datasetAction(DatasetAction::Language,key==UiKey::Right?1:0);
            return;
        }
        if (key == UiKey::Up && selection_) { --selection_; changed(); }
        if (key == UiKey::Down && selection_+1u<count) { ++selection_; changed(); }
        if (key == UiKey::Enter) {
            if (selection_ == count-2) {
                std::strcpy(dataset_edit_original_,view.info.language);editing_ = true; changed();
            }
            else if (selection_ == count-1 && datasetSelectionBlock(data_,true)==DatasetError::None) datasetAction(DatasetAction::Apply);
        }
        return;
    }
    if(screen_==ScreenId::Settings && page_==SettingsPage::DatasetMenu) {
        if(data_.datasets.applying)return;
        if(key==UiKey::Escape) {page_=SettingsPage::Operation;selection_=static_cast<uint8_t>(SettingId::Dataset);changed();return;}
        if(key==UiKey::Up && selection_) {--selection_;changed();}
        if(key==UiKey::Down && selection_<2) {++selection_;changed();}
        if(key==UiKey::Enter && datasetMenuBlock(data_,selection_)==DatasetError::None) {
            dataset_purpose_=static_cast<DatasetPurpose>(selection_);
            dataset_list_start_=dataset_purpose_==DatasetPurpose::Select?0:1;
            page_=SettingsPage::DatasetList;selection_=0;datasetAction(DatasetAction::Open,dataset_list_start_);
        }
        return;
    }
    if(screen_==ScreenId::Settings && page_==SettingsPage::DatasetIndex) {
        if(data_.datasets.applying)return;
        if(key==UiKey::Escape || key==UiKey::Enter) {
            if(data_.storage.busy)return;
            page_=SettingsPage::DatasetList;selection_=0;datasetAction(DatasetAction::Open,dataset_list_start_);
        }
        return;
    }
    if (card_info_) {
        if (key == UiKey::Escape || key == UiKey::Enter) { card_info_ = false; changed(); }
        return;
    }
    if (page_ == SettingsPage::System) {
        if (editing_) {
            if (key == UiKey::Escape) { editing_ = false; changed(); }
            else if (key == UiKey::Left || key == UiKey::Right) { edit_value_ ^= 1; changed(); }
            else if (key == UiKey::Enter) { system_usb_mode_ = edit_value_ != 0; editing_ = false; changed(); }
        } else if (key == UiKey::Escape) {
            system_usb_mode_ = false; page_ = SettingsPage::Root; selection_ = SettingsSystemRow; changed();
        } else if (key == UiKey::Up && selection_) { --selection_; changed(); }
        else if (key == UiKey::Down && selection_ + 1u < SettingsSystemCount) { ++selection_; changed(); }
        else if (key == UiKey::Enter && !data_.storage.busy) {
            if (!selection_) { edit_value_ = system_usb_mode_; editing_ = true; changed(); }
            else if (selection_ == 2 && system_usb_mode_ && usbEntryAllowed(data_)) {
                dialog_.open(DialogKind::EnterUsbMode); changed();
            } else if (selection_ == SettingsSystemResetRow) {
                page_ = SettingsPage::Reset; selection_ = 0; changed();
            }
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
    if (page_ == SettingsPage::About) {
        if (key == UiKey::Escape || key == UiKey::Enter) {
            page_ = SettingsPage::Root;
            selection_ = settingsRootIndex(SettingsRootItem::About); changed();
        }
        return;
    }
    if (key == UiKey::Escape) {
        if (page_ == SettingsPage::Root) {
            if (data_.boot.maintenance) { dialog_.open(DialogKind::RestartLogger); changed(); }
            else action(ActionKind::ChangeScreen, ScreenId::LivePlace);
        }
        else if (page_ == SettingsPage::Reset) { page_ = SettingsPage::System; selection_ = SettingsSystemResetRow; changed(); }
        else { page_ = SettingsPage::Root; selection_ = 0; changed(); }
        return;
    }
    if (page_ == SettingsPage::Licenses) {
        if (key == UiKey::Up && license_offset_ > 0) { --license_offset_; changed(); }
        if (key == UiKey::Down && license_offset_ + SettingsLayout::LicenseRows < licenseLineCount()) { ++license_offset_; changed(); }
        if (key == UiKey::Right) {
            for (size_t section = 0; section < licenseSectionCount(); ++section) {
                const auto start = licenseSectionStart(section);
                if (start > license_offset_) { license_offset_ = start; changed(); break; }
            }
        } else if (key == UiKey::Left && licenseSectionCount()) {
            size_t current = 0;
            while (current + 1 < licenseSectionCount() && licenseSectionStart(current + 1) <= license_offset_) ++current;
            const auto target = license_offset_ < licenseSectionStart(0) ? 0 : licenseSectionStart(current ? current - 1 : 0);
            if (target != license_offset_) { license_offset_ = target; changed(); }
        }
        return;
    }
    const unsigned count = page_ == SettingsPage::Root ? SettingsRootCount : page_ == SettingsPage::Operation ? OperationRowCount : page_ == SettingsPage::Reset ? 1 : 4;
    if (key == UiKey::Up && selection_ > 0) { --selection_; changed(); }
    if (key == UiKey::Down && static_cast<unsigned>(selection_) + 1 < count) { ++selection_; changed(); }
    if (key != UiKey::Enter) return;
    if (page_ == SettingsPage::Root) {
        switch (static_cast<SettingsRootItem>(selection_)) {
            case SettingsRootItem::Operation: page_ = SettingsPage::Operation; break;
            case SettingsRootItem::System: page_ = SettingsPage::System; system_usb_mode_ = false; break;
            case SettingsRootItem::Storage: page_ = SettingsPage::Storage; break;
            case SettingsRootItem::Licenses: page_ = SettingsPage::Licenses; break;
            case SettingsRootItem::About: page_ = SettingsPage::About; break;
            default: return;
        }
        selection_ = 0; license_offset_ = 0;
        changed();
    } else if (page_ == SettingsPage::Operation && selection_ == static_cast<unsigned>(SettingId::Dataset)) {
        page_ = SettingsPage::DatasetMenu; selection_ = 0;changed();
    } else if (page_ == SettingsPage::Operation && settingEditable(selection_) && !data_.storage.busy) {
        edit_value_ = settingValue(data_.settings, selection_);
        editing_ = true; changed();
    } else if (page_ == SettingsPage::Reset && !data_.storage.busy) {
        dialog_.open(DialogKind::ResetSettings); changed();
    } else if (page_ == SettingsPage::Storage && !data_.storage.busy) {
        if (selection_ == 0) { card_info_ = true; changed(); }
        else if (selection_ == 3) { dialog_.open(DialogKind::FormatSd); changed(); }
        else if (selection_ == 1 || selection_ == 2) {
            action(ActionKind::Storage);
            action_.storage_operation = selection_ == 1 ? StorageOperation::Remount : StorageOperation::Unmount;
        }
    }
}

void UiModel::usbAction(UsbAction kind) {
    usb_return_page_=usb_page_;
    action(ActionKind::UsbExport);action_.usb_action=kind;
    if(kind==UsbAction::Start) {
        action_.browse_index=usb_file_;action_.browse_revision=data_.browse.revision;
        action_.export_format=static_cast<ExportFormat>(selection_);
    }
    if(kind!=UsbAction::Older && kind!=UsbAction::Newer)usb_page_=UsbPage::Busy;
}
void UiModel::usbKey(UiKey key) {
    if(usb_page_==UsbPage::Menu) {
        if(key==UiKey::Up && selection_) {selection_=0;changed();}
        else if(key==UiKey::Down && !selection_) {selection_=1;changed();}
        else if(key==UiKey::Escape || (key==UiKey::Enter && selection_)) {
            dialog_.open(DialogKind::ExitUsbMode);changed();
        } else if(key==UiKey::Enter && data_.storage.usb.ready)usbAction(UsbAction::Begin);
    } else if(usb_page_==UsbPage::WaitEject) {
        if(key==UiKey::Escape || key==UiKey::Enter)usbAction(UsbAction::Publish);
    } else if(usb_page_==UsbPage::Files) {
        const auto& view=data_.browse;
        if(key==UiKey::Escape) {usbAction(UsbAction::Publish);return;}
        if(view.loading)return;
        if(key==UiKey::Up) {
            if(selection_) {--selection_;changed();}
            else if(view.has_newer)usbAction(UsbAction::Newer);
        } else if(key==UiKey::Down) {
            if(selection_+1u<view.count) {++selection_;changed();}
            else if(view.has_older)usbAction(UsbAction::Older);
        } else if(key==UiKey::Enter && selection_<view.count && view.error==LogReadError::None) {
            usb_file_=selection_;selection_=0;usb_page_=UsbPage::Format;changed();
        }
    } else if(usb_page_==UsbPage::Format) {
        if(key==UiKey::Escape) {usb_page_=UsbPage::Files;selection_=usb_file_;changed();}
        else if(key==UiKey::Up && selection_) {--selection_;changed();}
        else if(key==UiKey::Down && selection_<2) {++selection_;changed();}
        else if(key==UiKey::Enter)usbAction(UsbAction::Start);
    } else if(usb_page_==UsbPage::Complete || usb_page_==UsbPage::Error) {
        if(key==UiKey::Enter || key==UiKey::Escape)usbAction(UsbAction::Publish);
    }
}

void UiModel::acceptDialog(DialogKind kind) {
    switch (kind) {
        case DialogKind::RestartLogger:
            action(ActionKind::RestartLogger); action_.confirmed = true; break;
        case DialogKind::PauseLog: case DialogKind::StopLog:
            action(ActionKind::Logging);
            action_.log_action = kind == DialogKind::PauseLog ? LogAction::Pause : LogAction::Stop;
            break;
        case DialogKind::DeleteLog:
            browseAction(BrowseAction::Delete, selection_);
            break;
        case DialogKind::FormatSd:
            action(ActionKind::Storage); action_.storage_operation = StorageOperation::Format;
            break;
        case DialogKind::ResetSettings:
            action(ActionKind::ResetSettings); action_.settings = defaultSettings();
            break;
        case DialogKind::EnterUsbMode: case DialogKind::ExitUsbMode:
            action(ActionKind::Storage);
            action_.storage_operation = kind == DialogKind::EnterUsbMode ? StorageOperation::EnterUsbMode : StorageOperation::ExitUsbMode;
            break;
        case DialogKind::ClearDataset:
            datasetAction(DatasetAction::Clear);break;
        case DialogKind::RebuildIndex:case DialogKind::VerifyIndex:
            datasetAction(kind==DialogKind::RebuildIndex?DatasetAction::RebuildIndex:DatasetAction::VerifyIndex,selection_);
            action_.index_target=IndexTarget::All;page_=SettingsPage::DatasetIndex;changed();break;
        case DialogKind::None: return;
    }
    action_.confirmed = true;
}

void UiModel::datasetAction(DatasetAction kind,uint32_t index) {
    action(ActionKind::Dataset);action_.dataset_action=kind;action_.browse_index=index;
    action_.dataset_purpose=dataset_purpose_;action_.browse_revision=data_.datasets.revision;
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
    const bool was_dataset_notice=datasetFailureVisible();
    last_tick_ms_ = now_ms;
    // Start the visible interval when Live Place can actually show the notice.
    if(screen_!=ScreenId::LivePlace || mode_==DisplayMode::Background) dataset_notice_started_ms_=now_ms;
    if(was_dataset_notice!=datasetFailureVisible())redraw_=true;
    if (sdAccessLit() != was_lit) redraw_ = true;
    if (screen_ == ScreenId::Boot || screen_ == ScreenId::UsbStorage || switchingMode(data_) || data_.datasets.applying ||
        (screen_==ScreenId::Logs && data_.browse.deleting)) return;
    if (data_.message[0] && now_ms - data_.message_at_ms >= 3000u) {
        data_.message[0] = '\0';
        redraw_ = true;  // Local toast expiry does not notify App.
    }
    if (!last_input_ms_) last_input_ms_ = now_ms;
    if (data_.boot.maintenance || mode_ != DisplayMode::View || generation_ == 0 || action_.kind != ActionKind::None ||
        !validSettings(data_.settings)) return;
    const auto timeout = ScreenTimeouts[data_.settings.screen_timeout];
    if (timeout && now_ms - last_input_ms_ >= timeout * 1000u) {
        editing_ = false; dialog_.close();
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

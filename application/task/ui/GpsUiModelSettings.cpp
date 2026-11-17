#include "GpsUiModel.h"
#include "AlarmText.h"
#include "LicenseText.h"
#include "SettingsLayout.h"
#include "DatasetPresentation.h"
#include "SettingsAvailability.h"
#include "ScreenNavigation.h"
#include <algorithm>
#include <cstdio>

namespace geoputer {

unsigned GpsUiModel::selectionCount() const {
    if (screen_ == ScreenId::Menu) return MenuCount;
    switch (screen_resources_.settings.page) {
        case SettingsPage::Root: return SettingsRootCount;
        case SettingsPage::Operation: return OperationRowCount;
        case SettingsPage::System: return SettingsSystemCount;
        case SettingsPage::Storage: return SettingsStorageCount;
        case SettingsPage::Reset: return 1;
        case SettingsPage::Alarm:return AlarmRows;
        case SettingsPage::DatasetMenu: return DatasetMenuRows;
        case SettingsPage::DatasetList: return data_.datasets().count;
        case SettingsPage::DatasetChoice:return DatasetChoiceRows;
        case SettingsPage::DatasetDetail:
            return !data_.datasets().loading && data_.datasets().error==DatasetError::None ?
                datasetDetailRows(data_.datasets(),data_.settings.language==UiLanguage::Japanese) : 0;
        default: return 0;
    }
}

uint32_t GpsUiModel::licenseTarget(UiKey key) const {
    if (key==UiKey::Up && screen_resources_.settings.license_offset) return screen_resources_.settings.license_offset-1;
    if (key==UiKey::Down && screen_resources_.settings.license_offset+SettingsLayout::LicenseRows<licenseLineCount()) return screen_resources_.settings.license_offset+1;
    if (key==UiKey::Right) {
        for (size_t n=0;n<licenseSectionCount();++n)
            if (licenseSectionStart(n)>screen_resources_.settings.license_offset) return licenseSectionStart(n);
    } else if (key==UiKey::Left && licenseSectionCount()) {
        size_t current=0;
        while(current+1<licenseSectionCount() && licenseSectionStart(current+1)<=screen_resources_.settings.license_offset)++current;
        return screen_resources_.settings.license_offset<licenseSectionStart(0)?0:licenseSectionStart(current?current-1:0);
    }
    return screen_resources_.settings.license_offset;
}

unsigned GpsUiModel::editableSettingChoices() const {
    if(screen_!=ScreenId::Settings || screen_resources_.settings.card_info)return 0;
    const auto& settings=screen_resources_.settings;
    switch(settings.page) {
        case SettingsPage::Operation:
            return operationSettingEnabled(data_,selection_)?settingChoices(operationSettingIndex(selection_)):0;
        case SettingsPage::System:
            return selection_==0 && systemSettingEnabled(data_,settings.usb_mode,selection_)?3:0;
        case SettingsPage::Alarm:
            return alarmSettingEnabled(data_,selection_)?alarmChoices(selection_):0;
        default:return 0;
    }
}

bool GpsUiModel::editDirty() const {
    const auto& settings=screen_resources_.settings;
    if(!settings.editing)return false;
    return settings.edit_value!=settings.edit_original;
}

bool GpsUiModel::commitSettingEdit() {
    auto& settings=screen_resources_.settings;
    if(!settings.editing)return true;
    if(editDirty()) {
        if(settings.page==SettingsPage::Operation) {
            if(!operationSettingEnabled(data_,selection_))return false;
            Settings value=data_.settings;
            if(!setSettingValue(value,operationSettingIndex(selection_),settings.edit_value))return false;
            if(!sameSettings(value,data_.settings)) {
                action(ActionKind::SaveSettings);action_.settings=value;
            }
        } else if(settings.page==SettingsPage::Alarm) {
            if(data_.storage.busy || data_.storage.nvs_error || !data_.storage.alarm.usable())return false;
            if(settings.edit_value!=alarmValue(data_.storage.alarm.settings,selection_)) {
                action(ActionKind::SaveAlarm);action_.alarm_settings=data_.storage.alarm.settings;
                setAlarmValue(action_.alarm_settings,selection_,settings.edit_value);
                action_.alarm_identity=data_.storage.alarm.identity;
            }
        } else if(settings.page==SettingsPage::System)settings.usb_mode=settings.edit_value;
        // Dataset language is a local candidate. Apply still owns persistence/restart.
    }
    settings.editing=false;changed();return true;
}

void GpsUiModel::alarmSettingsKey(UiKey key) {
    auto& settings=screen_resources_.settings;
    if(key==UiKey::Escape) {settings.page=SettingsPage::Root;selection_=settingsRootIndex(SettingsRootItem::Alarm);changed();return;}
    if(key==UiKey::Up){--selection_;changed();}else if(key==UiKey::Down){++selection_;changed();}
    else if(key==UiKey::Enter && selection_==AlarmReloadRow && alarmSettingEnabled(data_,selection_)) {
        action(ActionKind::ReloadAlarm);
    }
}

void GpsUiModel::datasetSettingsKey(UiKey key) {
    if (isDatasetBrowserPage(screen_resources_.settings.page)) {
        const auto& view = data_.datasets();
        if (view.applying) return;
        if (key == UiKey::Escape) {
            if(dataset_borrow_.token)dataset_publishable_=false;
            if (screen_resources_.settings.page == SettingsPage::DatasetDetail) {
                screen_resources_.settings.page = SettingsPage::DatasetChoice; selection_ = 2;changed();
            } else if(screen_resources_.settings.page == SettingsPage::DatasetChoice) {
                screen_resources_.settings.page = SettingsPage::DatasetList; selection_ = 0;
                datasetAction(DatasetAction::Open);
            } else { screen_resources_.settings.page = SettingsPage::DatasetMenu; selection_ = static_cast<uint8_t>(screen_resources_.settings.dataset.purpose); changed(); }
            return;
        }
        if (view.loading) return;
        if(screen_resources_.settings.page==SettingsPage::DatasetList && view.error!=DatasetError::None) {
            if(key==UiKey::Enter) datasetAction(DatasetAction::Open);
            return;
        }
        if (screen_resources_.settings.page == SettingsPage::DatasetList) {
            if (key == UiKey::Up) {
                if (selection_) { --selection_;changed(); }
                else if (view.has_older) datasetAction(DatasetAction::Older);
            } else if (key == UiKey::Down) {
                if (selection_+1 < view.count) { ++selection_;changed(); }
                else if (view.has_newer) datasetAction(DatasetAction::Newer);
            } else if (key == UiKey::Enter && view.count && datasetRowBlock(data_,selection_)==DatasetError::None) {
                screen_resources_.settings.dataset.dialog_id=view.rows[selection_].id;
                if(!screen_resources_.settings.dataset.dialog_id) {dialog_.open(DialogKind::ClearDataset);changed();}
                else if(screen_resources_.settings.dataset.purpose!=DatasetPurpose::Select) {
                    dialog_.open(screen_resources_.settings.dataset.purpose==DatasetPurpose::Rebuild?DialogKind::RebuildIndex:DialogKind::VerifyIndex);changed();
                } else datasetAction(DatasetAction::Detail,selection_);
            }
            return;
        }
        if(screen_resources_.settings.page==SettingsPage::DatasetChoice) {
            if(key==UiKey::Left || key==UiKey::Right) {
                if(selection_==0){
                    auto& offset=screen_resources_.settings.dataset.title_offset;
                    if(key==UiKey::Left && offset){--offset;changed();}
                    else if(key==UiKey::Right && uiTextCanScroll(view.info.title,data_.settings.language==UiLanguage::Japanese,offset,200))
                        {++offset;changed();}
                } else if(selection_==1 && view.info.language_count>1){
                    auto& candidate=screen_resources_.settings.dataset.wordbook_index;
                    const auto count=view.info.language_count;
                    candidate=static_cast<uint8_t>((candidate+count+(key==UiKey::Right?1:count-1))%count);
                    changed();
                }
            } else if(key==UiKey::Up){if(selection_){--selection_;changed();}}
            else if(key==UiKey::Down){if(unsigned(selection_)+1<DatasetChoiceRows){++selection_;changed();}}
            else if(key==UiKey::Enter){
                if(selection_==2 && view.error==DatasetError::None){screen_resources_.settings.page=SettingsPage::DatasetDetail;selection_=0;changed();}
                else if(selection_==3 && datasetChoiceCanApply(data_))datasetAction(DatasetAction::Apply);
            }
            return;
        }
        if (view.error != DatasetError::None) return;
        const unsigned count = selectionCount();
        if (key == UiKey::Up && selection_) { --selection_; changed(); }
        const unsigned last=count>DatasetDetailVisibleRows?count-DatasetDetailVisibleRows:0;
        if (key == UiKey::Down && selection_<last) { ++selection_; changed(); }
        if (key == UiKey::Left && selection_) { selection_=selection_>DatasetDetailVisibleRows?selection_-DatasetDetailVisibleRows:0;changed(); }
        if (key == UiKey::Right && selection_<last) { selection_=std::min<unsigned>(selection_+DatasetDetailVisibleRows,last);changed(); }
        return;
    }
    if(screen_resources_.settings.page==SettingsPage::DatasetMenu) {
        if(data_.datasets().applying)return;
        if(key==UiKey::Escape) {screen_resources_.settings.page=SettingsPage::Root;selection_=settingsRootIndex(SettingsRootItem::Dataset);changed();return;}
        if(key==UiKey::Up) {--selection_;changed();}
        if(key==UiKey::Down) {++selection_;changed();}
        if(key==UiKey::Enter && datasetMenuBlock(data_,selection_)==DatasetError::None) {
            screen_resources_.settings.dataset.purpose=static_cast<DatasetPurpose>(selection_);
            screen_resources_.settings.dataset.list_start={};screen_resources_.settings.dataset.page=0;
            screen_resources_.settings.page=SettingsPage::DatasetList;selection_=0;datasetAction(DatasetAction::Open);
        }
        return;
    }
    if(screen_resources_.settings.page==SettingsPage::DatasetIndex) {
        if(data_.datasets().applying)return;
        if(key==UiKey::Escape || key==UiKey::Enter) {
            if(data_.storage.busy)return;
            screen_resources_.settings.page=SettingsPage::DatasetList;selection_=0;datasetAction(DatasetAction::Open);
        }
        return;
    }
}

void GpsUiModel::settingsKey(UiKey key) {
    auto& edit=screen_resources_.settings;
    if(edit.editing) {
        if(key==UiKey::Escape) {
            const bool dirty=editDirty();edit.editing=false;
            if(dirty) {
                changed();return; // A second Escape leaves the page.
            }
        } else if(key==UiKey::Enter || key==UiKey::Up || key==UiKey::Down) {
            if(!commitSettingEdit())return;
            if(key==UiKey::Enter)return;
        }
    }
    if(key==UiKey::Left || key==UiKey::Right) {
        const auto count=editableSettingChoices();
        if(count>1) {
            if(!edit.editing) {
                edit.edit_original=edit.page==SettingsPage::System?edit.usb_mode:
                    edit.page==SettingsPage::Alarm?alarmValue(data_.storage.alarm.settings,selection_):
                    settingValue(data_.settings,operationSettingIndex(selection_));
                edit.edit_value=edit.edit_original;
                edit.editing=true;
            }
            edit.edit_value=(edit.edit_value+(key==UiKey::Right?1:count-1))%count;changed();
            if(edit.page==SettingsPage::Alarm && selection_==AlarmSoundRow) {
                action(ActionKind::PreviewAlarm);action_.alarm_settings=data_.storage.alarm.settings;
                action_.alarm_settings.tone=static_cast<alarm::Tone>(edit.edit_value);
                action_.alarm_identity=data_.storage.alarm.identity;
            }
            return;
        }
    }
    if(screen_resources_.settings.page==SettingsPage::Alarm) {alarmSettingsKey(key);return;}
    if(isDatasetSettingsPage(screen_resources_.settings.page)) {datasetSettingsKey(key);return;}
    if (screen_resources_.settings.card_info) {
        if (key == UiKey::Escape || key == UiKey::Enter) { screen_resources_.settings.card_info = false; changed(); }
        return;
    }
    if (screen_resources_.settings.page == SettingsPage::System) {
        if (key == UiKey::Escape) {
            screen_resources_.settings.usb_mode = false; screen_resources_.settings.page = SettingsPage::Root; selection_ = SettingsSystemRow; changed();
        } else if (key == UiKey::Up) { --selection_; changed(); }
        else if (key == UiKey::Down) { ++selection_; changed(); }
        else if (key == UiKey::Enter && systemSettingEnabled(data_,screen_resources_.settings.usb_mode,selection_)) {
            if (selection_ == 2) {
                dialog_.open(screen_resources_.settings.usb_mode==2?DialogKind::EnterWifiMode:DialogKind::EnterUsbMode); changed();
            } else if (selection_ == SettingsSystemResetRow) {
                screen_resources_.settings.page = SettingsPage::Reset; selection_ = 0; changed();
            }
        }
        return;
    }
    if (screen_resources_.settings.page == SettingsPage::About) {
        if (key == UiKey::Escape || key == UiKey::Enter) {
            screen_resources_.settings.page = SettingsPage::Root;
            selection_ = settingsRootIndex(SettingsRootItem::About); changed();
        }
        return;
    }
    if (key == UiKey::Escape) {
        if (screen_resources_.settings.page == SettingsPage::Root) {
            if (data_.boot.maintenance) { dialog_.open(DialogKind::RestartLogger); changed(); }
            else action(ActionKind::ChangeScreen, ScreenId::LivePlace);
        }
        else if (screen_resources_.settings.page == SettingsPage::Reset) { screen_resources_.settings.page = SettingsPage::System; selection_ = SettingsSystemResetRow; changed(); }
        else { screen_resources_.settings.page = SettingsPage::Root; selection_ = 0; changed(); }
        return;
    }
    if (screen_resources_.settings.page == SettingsPage::Licenses) {
        const auto target = licenseTarget(key);
        if (target != screen_resources_.settings.license_offset) { screen_resources_.settings.license_offset = target; changed(); }
        return;
    }
    if (key == UiKey::Up) { --selection_; changed(); }
    if (key == UiKey::Down) { ++selection_; changed(); }
    if (key != UiKey::Enter) return;
    if (screen_resources_.settings.page == SettingsPage::Root) {
        switch (static_cast<SettingsRootItem>(selection_)) {
            case SettingsRootItem::Operation: screen_resources_.settings.page = SettingsPage::Operation; break;
            case SettingsRootItem::System: screen_resources_.settings.page = SettingsPage::System; screen_resources_.settings.usb_mode = false; break;
            case SettingsRootItem::Dataset: screen_resources_.settings.page = SettingsPage::DatasetMenu; break;
            case SettingsRootItem::Storage: screen_resources_.settings.page = SettingsPage::Storage; break;
            case SettingsRootItem::Licenses: screen_resources_.settings.page = SettingsPage::Licenses; break;
            case SettingsRootItem::About: screen_resources_.settings.page = SettingsPage::About; break;
            case SettingsRootItem::Alarm:screen_resources_.settings.page=SettingsPage::Alarm;break;
            default: return;
        }
        selection_ = 0; screen_resources_.settings.license_offset = 0;
        changed();
    } else if (screen_resources_.settings.page == SettingsPage::Reset && !data_.storage.busy) {
        dialog_.open(DialogKind::ResetSettings); changed();
    } else if (screen_resources_.settings.page == SettingsPage::Storage && !data_.storage.busy) {
        if (selection_ == 0) { screen_resources_.settings.card_info = true; changed(); }
        else if (selection_ == 3) { dialog_.open(DialogKind::FormatSd); changed(); }
        else if (selection_ == 1 || selection_ == 2) {
            action(ActionKind::Storage);
            action_.storage_operation = selection_ == 1 ? StorageOperation::Remount : StorageOperation::Unmount;
        }
    }
}




void GpsUiModel::datasetAction(DatasetAction kind,uint32_t index) {
    action(ActionKind::Dataset);action_.dataset_action=kind;
    action_.dataset_purpose=screen_resources_.settings.dataset.purpose;
    const auto& view=data_.datasets();
    action_.dataset_page=kind==DatasetAction::Open?screen_resources_.settings.dataset.page:view.page;
    switch(kind) {
        case DatasetAction::Open:action_.dataset_id=screen_resources_.settings.dataset.list_start;break;
        case DatasetAction::Older:case DatasetAction::Newer:
            if(view.count)action_.dataset_id=view.rows[kind==DatasetAction::Older?0:view.count-1].id;
            break;
        case DatasetAction::Detail:case DatasetAction::RebuildIndex:case DatasetAction::VerifyIndex:
            if(index<view.count)action_.dataset_id=view.rows[index].id;
            break;
        case DatasetAction::Clear:break;
        default:action_.dataset_id=view.id;break;
    }
    if(kind==DatasetAction::Apply) {
        const auto candidate=screen_resources_.settings.dataset.wordbook_index;
        const char* language=candidate<view.info.language_count?
            view.info.wordbook_types[candidate]:view.info.language;
        std::snprintf(action_.settings.wordbook_language,sizeof(action_.settings.wordbook_language),"%s",language);
    }
    if(dataset_borrow_.token)dataset_publishable_=false;
}

}  // namespace geoputer

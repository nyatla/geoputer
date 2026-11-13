#pragma once

#include "GpsUiTypes.h"
#include "DisplayClock.h"
#include "ChoiceDialog.h"
#include "UiKeyGuide.h"
#include "UiShortcutGuide.h"
#include "ScreenNavigation.h"
#include "UiScreenResources.h"
#include "../../map/MapFrameReceiver.h"
#include "../../map/MapTracking.h"
#include "../../GpsMessages.h"

namespace geoputer {

// Owned exclusively by UITask. This state has no LCD/RTOS dependency.
class GpsUiModel {
public:
    OperationStatus setScreen(const SetUiScreen& command);
    OperationStatus updateContent(const UpdateUiContent& command);
    OperationStatus beginDatasetRead(const DatasetRequest&,map::BufferLease&);
    OperationStatus commitDatasetRead(const DatasetCompletion&);
    OperationStatus discardDatasetRead(const DatasetCompletion&);
    OperationStatus datasetReadError(const DatasetCompletion&);
    OperationStatus updateDatasetProgress(const IndexProgress&);
    OperationStatus beginPlaceSearch(const PlaceRequest& request,map::BufferLease& buffer);
    OperationStatus commitPlaceSearch(const PlaceRequest& request);
    OperationStatus discardPlaceSearch(const PlaceRequest& request);
    OperationStatus beginMapFrame(const map::Request& request,map::BufferLease& buffer);
    OperationStatus discardMapFrame(const map::Request& request);
    OperationStatus commitMapFrame(const map::FrameInfo& info);
    const map::MapFrameReceiver& mapFrame() const { return screen_resources_.map.frame; }
    const map::CursorOffset& mapCursor() const { return screen_resources_.map.cursor; }
    void keyPressed(UiKey key, uint32_t now_ms);
    void tick(uint32_t now_ms);
    UiSnapshot snapshot() const;
    const UiData& data() const { return data_; }
    DisplayMode mode() const { return mode_; }
    bool takeRedraw();
    static constexpr uint32_t ScreenNameMs=1000;
    bool screenNameVisible()const {
        return screen_name_active_ && mode_==DisplayMode::View && !dialog_.active() &&
            !(audio_active_ && !audio_preview_) && last_tick_ms_-screen_name_started_ms_<ScreenNameMs;
    }
    const char* screenNameText()const {return screenName(screen_);}
    bool alarmSet()const {const auto& p=data_.storage.alarm;return p.usable() && p.settings.enabled &&
        data_.settings.dataset && p.dataset==data_.settings.dataset;}
    bool alarmRinging()const {return audio_active_ && !audio_preview_;}
    bool notificationPending() const { return notification_pending_; }
    void notificationSent() { notification_pending_ = false; }
    uint8_t systemUsbMode() const { return screen_resources_.settings.usb_mode; }
    bool editing() const { return screen_resources_.settings.editing; }
    bool editDirty() const;
    uint8_t editValue() const { return screen_resources_.settings.edit_value; }
    void setAudioActive(bool active) {if(active!=audio_active_){audio_active_=active;redraw_=true;}if(!active)audio_preview_=false;}
    void setAudioPreview(bool preview) {if(preview!=audio_preview_){audio_preview_=preview;redraw_=true;}}
    bool confirming() const { return dialog_.active(); }
    bool confirmChoice() const { return dialog_.selected(); }
    const ChoiceDialog& dialog() const { return dialog_; }
    UiKeyGuide keyGuide() const;
    UiShortcutGuide shortcutGuide() const;
    uint32_t licenseOffset() const { return screen_resources_.settings.license_offset; }
    const DatasetId& datasetDialogId() const { return screen_resources_.settings.dataset.dialog_id; }
    static constexpr uint32_t DatasetNoticeMs = 5000;
    bool datasetFailureVisible() const {
        return !data_.settings.dataset && data_.storage.dataset_failure.revision &&
            last_tick_ms_ - dataset_notice_started_ms_ < DatasetNoticeMs;
    }
    bool viewingLicenses() const { return screen_ == ScreenId::Settings && screen_resources_.settings.page == SettingsPage::Licenses; }
    bool cardInfo() const { return screen_resources_.settings.card_info; }
    const DisplayClock& clock() const { return clock_; }
    static constexpr uint32_t AccessLampMinMs = 150;
    bool sdAccessLit() const {
        return sd_seen_ && (data_.storage.sd.active || last_tick_ms_ - access_started_ms_ < AccessLampMinMs);
    }
    const BatteryStatus& battery() const { return battery_; }
    void setBattery(int percent, bool charging, bool known) {
        const int8_t value = percent >= 0 && percent <= 100 ? percent : -1;
        if (value != battery_.percent || charging != battery_.charging || known != battery_.charge_known) {
            battery_ = {value, charging, known}; redraw_ = true;
        }
    }

private:
    UiKey nextNumericKey() const;
    void appendEnterShortcut(UiShortcutGuide& row,bool accepting) const;
    void openGpsControls();
    void gpsLoggerKey(UiKey key);
    void logBrowserKey(UiKey key);
    void settingsKey(UiKey key);
    unsigned editableSettingChoices() const;
    bool commitSettingEdit();

    void updateScreenName();
    void copyData(const UiData& data);
    void datasetChanged();
    void updateDialogState();
    bool canDeleteLog() const;
    unsigned selectionCount() const;
    uint32_t licenseTarget(UiKey key) const;
    void acknowledge(uint32_t action_id, ActionResult result);
    void changed();
    void datasetAction(DatasetAction kind, uint32_t index = 0);
    void browseAction(BrowseAction kind,uint32_t index=0);
    void action(ActionKind kind, ScreenId target = ScreenId::LivePlace);
    void acceptDialog(DialogKind kind);



    UiScreenResources screen_resources_;
    DatasetRequest dataset_borrow_{};
    bool dataset_owned_=false,dataset_publishable_=false;
    PlaceRequest place_borrow_{}; // Survives screen changes until Storage has stopped.
    void updateMapCursor();
    bool acceptsMap(const map::Request& request) const;
    bool acceptsMap(const map::FrameInfo& info) const;
    ScreenId screen_ = ScreenId::LivePlace;
    DisplayMode mode_ = DisplayMode::View;
    uint32_t generation_ = 0;
    uint32_t revision_ = 0;
    uint32_t next_action_id_ = 0;
    uint32_t last_input_ms_ = 0;
    uint32_t last_tick_ms_ = 0;
    uint32_t access_started_ms_ = 0;
    uint32_t dataset_notice_started_ms_ = 0;
    uint32_t screen_name_started_ms_=0;
    uint16_t screen_name_key_=UINT16_MAX;
    bool screen_name_active_=false;
    uint8_t selection_ = 0;




    ChoiceDialog dialog_;







    bool audio_active_=false,audio_preview_=false;
    bool early_maintenance_key_ = false;
    RecordingState gps_dialog_state_ = RecordingState::Stopped;
    UiData data_{};
    BatteryStatus battery_{};
    bool sd_seen_ = false;
    DisplayClock clock_;
    UiAction action_{};
    bool redraw_ = false;
    bool notification_pending_ = false;
};

}  // namespace geoputer

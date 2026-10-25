#pragma once

#include "UiTypes.h"
#include "DisplayClock.h"
#include "ChoiceDialog.h"
#include "UiKeyGuide.h"
#include "UiScreenResources.h"
#include "../../map/MapFrameReceiver.h"
#include "../../map/MapTracking.h"
#include "../../TaskMessages.h"

namespace geoputer {

// Owned exclusively by UITask. This state has no LCD/RTOS dependency.
class UiModel {
public:
    OperationStatus setScreen(const SetUiScreen& command);
    OperationStatus updateContent(const UpdateUiContent& command);
    OperationStatus beginMapFrame(const map::Transfer& transfer);
    OperationStatus writeMapRows(const map::Transfer& transfer);
    OperationStatus commitMapFrame(const map::Transfer& transfer);
    const map::MapFrameReceiver& mapFrame() const { return screen_resources_.map.frame; }
    const map::CursorOffset& mapCursor() const { return screen_resources_.map.cursor; }
    void keyPressed(UiKey key, uint32_t now_ms);
    void tick(uint32_t now_ms);
    UiSnapshot snapshot() const;
    const UiData& data() const { return data_; }
    DisplayMode mode() const { return mode_; }
    bool takeRedraw();
    bool notificationPending() const { return notification_pending_; }
    void notificationSent() { notification_pending_ = false; }
    bool systemUsbMode() const { return screen_resources_.settings.usb_mode; }
    bool editing() const { return screen_resources_.settings.editing; }
    uint8_t editValue() const { return screen_resources_.settings.edit_value; }
    bool confirming() const { return dialog_.active(); }
    bool confirmChoice() const { return dialog_.selected(); }
    const ChoiceDialog& dialog() const { return dialog_; }
    UiKeyGuide keyGuide() const;
    uint32_t licenseOffset() const { return screen_resources_.settings.license_offset; }
    uint8_t datasetDialogNumber() const { return screen_resources_.settings.dataset.dialog_number; }
    static constexpr uint32_t DatasetNoticeMs = 5000;
    bool datasetFailureVisible() const {
        return !data_.settings.dataset && data_.storage.dataset_failure.revision &&
            last_tick_ms_ - dataset_notice_started_ms_ < DatasetNoticeMs;
    }
    bool viewingLicenses() const { return screen_ == ScreenId::Settings && screen_resources_.settings.page == SettingsPage::Licenses; }
    bool cardInfo() const { return screen_resources_.settings.card_info; }
    UsbPage usbPage() const { return screen_resources_.usb.page; }
    uint8_t usbFile() const { return screen_resources_.usb.file; }
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
    void openGpsControls();
    void gpsLoggerKey(UiKey key);
    void logBrowserKey(UiKey key);
    void settingsKey(UiKey key);

    void copyData(const UiData& data);
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
    void usbKey(UiKey key);
    void usbAction(UsbAction action);



    UiScreenResources screen_resources_;
    void updateMapCursor();
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
    uint8_t selection_ = 0;




    ChoiceDialog dialog_;







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

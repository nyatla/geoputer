#pragma once

#include "UiTypes.h"
#include "DisplayClock.h"
#include "../../TaskMessages.h"

namespace geoputer {

// Owned exclusively by UITask. This state has no LCD/RTOS dependency.
class UiModel {
public:
    OperationStatus setScreen(const SetUiScreen& command);
    OperationStatus updateContent(const UpdateUiContent& command);
    void keyPressed(UiKey key, uint32_t now_ms);
    void tick(uint32_t now_ms);
    UiSnapshot snapshot() const;
    const UiData& data() const { return data_; }
    DisplayMode mode() const { return mode_; }
    bool takeRedraw();
    bool notificationPending() const { return notification_pending_; }
    void notificationSent() { notification_pending_ = false; }
    bool editing() const { return editing_; }
    uint8_t editValue() const { return edit_value_; }
    bool confirming() const { return confirming_; }
    bool confirmChoice() const { return confirm_yes_; }
    uint32_t licenseOffset() const { return license_offset_; }
    bool cardInfo() const { return card_info_; }
    bool datasetWarning() const { return dataset_warning_ && screen_ != ScreenId::Boot; }
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
    void copyData(const UiData& data);
    void acknowledge(uint32_t action_id, ActionResult result);
    void changed();
    void browseAction(BrowseAction kind,uint32_t index=0);
    void action(ActionKind kind, ScreenId target = ScreenId::LivePlace);

    ScreenId screen_ = ScreenId::LivePlace;
    DisplayMode mode_ = DisplayMode::View;
    uint32_t generation_ = 0;
    uint32_t revision_ = 0;
    uint32_t next_action_id_ = 0;
    uint32_t last_input_ms_ = 0;
    uint32_t last_tick_ms_ = 0;
    uint32_t access_started_ms_ = 0;
    uint8_t selection_ = 0;
    SettingsPage page_ = SettingsPage::Root;
    bool editing_ = false;
    bool confirming_ = false;
    bool confirm_yes_ = false;
    uint8_t edit_value_ = 0;
    uint32_t license_offset_ = 0;
    bool card_info_ = false;
    bool dataset_warning_ = false;
    UiData data_{};
    BatteryStatus battery_{};
    bool sd_seen_ = false;
    LogAction confirm_log_action_ = LogAction::Pause;
    DisplayClock clock_;
    UiAction action_{};
    bool redraw_ = false;
    bool notification_pending_ = false;
};

}  // namespace geoputer

#pragma once
#include "../../ScreenGroup.h"
#include "../../map/MapFrameReceiver.h"
#include "../../map/MapTracking.h"

namespace geoputer {
struct UiMapResources {
    map::MapFrameReceiver frame;
    map::CursorOffset cursor{};
    void clear() {frame.clear();cursor={};}
};
struct UiDatasetResources {
    DatasetPurpose purpose=DatasetPurpose::Select;
    DatasetId dialog_id{},list_start{};
    uint16_t page=0;
    uint8_t wordbook_index=0;
    uint8_t title_offset=0;
};
struct UiSettingsResources {
    SettingsPage page=SettingsPage::Root;
    UiDatasetResources dataset;
    uint32_t license_offset=0;
    bool editing=false,card_info=false;
    uint8_t usb_mode=0; // BootMode candidate: 0 GPS, 1 USB, 2 Wi-Fi.
    uint8_t edit_value=0,edit_original=0;
};
// UI alone owns these resources. Two image arrays remain at fixed addresses.
// Screen entry hides old images and resets local state; pending loans survive.
struct UiScreenResources {
    UiMapResources map;
    UiSettingsResources settings;
    void enter(ScreenGroup group) {
        map.clear();settings={};group_=group;
    }
    ScreenGroup group() const {return group_;}
private:
    ScreenGroup group_=ScreenGroup::Common;
};
} // namespace geoputer

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
    uint8_t dialog_number=0,list_start=0;
    char edit_original[config::DatasetLanguageBytes]{};
};
struct UiSettingsResources {
    SettingsPage page=SettingsPage::Root;
    UiDatasetResources dataset;
    uint32_t license_offset=0;
    bool editing=false,card_info=false,usb_mode=false;
    uint8_t edit_value=0;
};
struct UiUsbResources {
    UsbPage page=UsbPage::Menu,return_page=UsbPage::Menu;
    uint8_t file=0;
};
// UI alone owns these resources. Images are allocated only on the Map screen;
// screen entry releases both image buffers and resets all departed local state.
struct UiScreenResources {
    UiMapResources map;
    UiSettingsResources settings;
    UiUsbResources usb;
    void enter(ScreenGroup group) {
        map.clear();settings={};usb={};group_=group;
    }
    ScreenGroup group() const {return group_;}
private:
    ScreenGroup group_=ScreenGroup::Common;
};
} // namespace geoputer

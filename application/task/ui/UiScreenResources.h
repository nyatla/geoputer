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
    bool editing=false,card_info=false;
    uint8_t usb_mode=0; // BootMode candidate: 0 GPS, 1 USB, 2 Wi-Fi.
    uint8_t edit_value=0,edit_original=0;
};
struct UiUsbResources {
    UsbPage page=UsbPage::Menu,return_page=UsbPage::Menu;
    uint8_t file=0;
};
// UI alone owns these resources. Two image arrays remain at fixed addresses.
// Screen entry hides old images and resets local state; pending loans survive.
struct UiScreenResources {
    UiMapResources map;
    UiSettingsResources settings;
    UiUsbResources usb;
    WifiPage wifi_page=WifiPage::ConnectionQr;
    void enter(ScreenGroup group) {
        map.clear();settings={};usb={};wifi_page=WifiPage::ConnectionQr;group_=group;
    }
    ScreenGroup group() const {return group_;}
private:
    ScreenGroup group_=ScreenGroup::Common;
};
} // namespace geoputer

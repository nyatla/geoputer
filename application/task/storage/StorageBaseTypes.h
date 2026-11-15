#pragma once
#include <cstdint>
namespace geoputer {
enum class SettingsState : uint8_t { Unavailable, Defaults, Loaded, Invalid, Saving, Saved, Failed };
enum class CardState : uint8_t { Unknown, NoCard, Mounted, Unmounted, Unsupported, Failed, NeedsSetup };
enum class StorageOperation : uint8_t { None, SaveSettings, Remount, Unmount, Format, EnterUsbMode, ExitUsbMode, SaveAlarm, ReloadAlarm, EnterWifiMode, ExitWifiMode, RegenerateWifi, PrepareDirectories };
inline const char* cardStateName(CardState state) {
    switch (state) {
        case CardState::Unknown: return "WAITING";
        case CardState::NoCard: return "NO CARD";
        case CardState::Mounted: return "MOUNTED";
        case CardState::Unmounted: return "SAFE TO REMOVE";
        case CardState::Unsupported: return "FAT32 REQUIRED";
        case CardState::Failed: return "SD ERROR";
        case CardState::NeedsSetup: return "SETUP REQUIRED";
    }
    return "SD ERROR";
}

}

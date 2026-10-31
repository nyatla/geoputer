#pragma once
#include "../../wifi/WifiTypes.h"
#include "../../task/storage/StorageActivity.h"
#include "../common/PanelChannels.h"
namespace geoputer {
struct WifiState {uint32_t revision=0;PanelSettings settings{};WifiView wifi{};SdAccessSnapshot sd{};CardState card_state=CardState::Unknown;};
struct WifiUiData {WifiState storage{};PanelSettings settings{};char message[40]{};};
enum class WifiActionKind:uint8_t {None,Exit,Regenerate};
struct WifiAction {WifiActionKind kind=WifiActionKind::None;uint32_t id=0;};
struct WifiCommand {WifiActionKind action=WifiActionKind::None;};
using WifiChannels=PanelChannels<WifiUiData,WifiAction,WifiCommand,WifiState>;
}

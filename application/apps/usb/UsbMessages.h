#pragma once
#include "../../Settings.h"
#include "../../task/storage/StorageBaseTypes.h"
#include "../../task/storage/StorageActivity.h"
#include "../../task/storage/usb/UsbStorageTypes.h"
#include "../../task/storage/export/ExportTypes.h"
#include "../common/PanelChannels.h"
namespace geoputer {
struct UsbFiles {char names[LogListRows][LogNameBytes]{};uint32_t revision=0;uint8_t count=0,selected=0;bool loading=false,has_older=false,has_newer=false;LogReadError error=LogReadError::None;};
struct UsbState {uint32_t revision=0;PanelSettings settings{};UsbStorageView usb{};ExportSnapshot export_job{};SdAccessSnapshot sd{};UsbFiles files{};CardState card_state=CardState::Unknown;bool busy=false,restart_ready=false;};
struct UsbUiData {UsbState storage{};PanelSettings settings{};char message[40]{};const UsbFiles& browse()const{return storage.files;}};
enum class UsbActionKind:uint8_t {None,Begin,Start,Publish,List,Exit};
struct UsbUiAction {UsbActionKind kind=UsbActionKind::None;uint32_t id=0;uint8_t index=0;ExportFormat format=ExportFormat::Gpx;LogListDirection direction=LogListDirection::Newest;};
struct UsbCommand {UsbActionKind action=UsbActionKind::None;ExportFormat format=ExportFormat::Gpx;LogListDirection direction=LogListDirection::Newest;char name[LogNameBytes]{};};
using UsbChannels=PanelChannels<UsbUiData,UsbUiAction,UsbCommand,UsbState>;
}

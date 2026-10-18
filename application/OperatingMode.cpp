#include "Application.h"
#include "utils/Console.h"
#include <cstdio>

namespace geoputer {
bool Application::beginUsb() {
    storage_.setUsbMode(true);
    // As in normal boot, wait for M5 display/board initialization before
    // configuring the SD SPI peripheral. UI startup runs in another task.
    if (!setScreen(ScreenId::UsbStorage)) return false;
    StorageRequest request{}; request.kind = MessageKind::InitializeStorage;
    SyncReturn result{};
    const bool started = storage_.initialize() && storage_.start("USBStorage",12288,1);
    if (started && callTask(TaskId::Storage,storage_.inbox(),request,result)) applyStorage(result.storage);
    else message("USB STORAGE INIT FAILED");
    Console.println("[APP] USB mode: UI and raw SD only; GPS logger is not started");
    return publishUiContent();
}
bool Application::operateBootMode(const UiAction& action) {
    if (!action.confirmed || storage_operation_id_ || dataset_applying_) return false;
    if (!usb_mode_ && (!usbEntryAllowed(data_) || log_control_.kind != ActionKind::None)) {
        message("STOP LOGGER FIRST"); return false;
    }
    StorageRequest request{}; request.kind = MessageKind::OperateStorage;
    auto& command = request.payload.storage_command;
    command.operation = usb_mode_ ? StorageOperation::ExitUsbMode : StorageOperation::EnterUsbMode;
    if (command.operation != action.storage_operation) return false;
    command.confirmed = true; command.operation_id = action.action_id;
    SyncReturn result{};
    // Completion notifications may be drained while waiting for the sync return.
    storage_operation_id_ = action.action_id;
    if (!callTask(TaskId::Storage,storage_.inbox(),request,result) || result.status != OperationStatus::Ok) {
        storage_operation_id_ = 0;
        message("MODE SWITCH REJECTED"); return false;
    }
    applyStorage(result.storage);
    if (storage_operation_id_) message("SWITCHING MODE...");
    return true;
}
bool Application::operateUsbExport(const UiAction& action) {
    if(!usb_mode_ || storage_operation_id_ || restart_pending_)return false;
    auto& view=data_.browse;
    if(action.usb_action==UsbAction::Older || action.usb_action==UsbAction::Newer) {
        const bool older=action.usb_action==UsbAction::Older;
        if(!usbSelecting() || view.loading || !view.count || (older?!view.has_older:!view.has_newer))return false;
        browse_request_={};browse_request_.kind=LogReadKind::List;browse_request_.count=4;
        browse_request_.direction=older?LogListDirection::Older:LogListDirection::Newer;
        std::snprintf(browse_request_.anchor,sizeof(browse_request_.anchor),"%s",view.names[older?view.count-1:0]);
        view.loading=browse_need_=true;++data_.revision;ui_data_dirty_=true;return true;
    }
    if(action.usb_action!=UsbAction::Begin && action.usb_action!=UsbAction::Start && action.usb_action!=UsbAction::Publish)return false;
    StorageRequest request{};request.kind=MessageKind::UsbExportOperation;
    auto& command=request.payload.export_request;
    command.operation=action.usb_action==UsbAction::Begin?ExportOperation::Begin:
                      action.usb_action==UsbAction::Start?ExportOperation::Start:ExportOperation::Publish;
    if(command.operation==ExportOperation::Start) {
        if(!usbSelecting() || view.loading || action.browse_revision!=view.revision || action.browse_index>=view.count ||
           !validExportFormat(action.export_format))return false;
        command.format=action.export_format;
        std::snprintf(command.name,sizeof(command.name),"%s",view.names[action.browse_index]);
    }
    if(command.operation==ExportOperation::Publish) {
        browse_need_=false;
        if(browse_waiting_)browse_cancel_=true;
    }
    SyncReturn result{};
    if(!callTask(TaskId::Storage,storage_.inbox(),request,result) || result.status!=OperationStatus::Ok) {
        message("USB OPERATION REJECTED");return false;
    }
    applyStorage(result.storage);message("");return true;
}
}

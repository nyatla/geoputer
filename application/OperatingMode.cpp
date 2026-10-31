#include "GpsApplication.h"
#include "utils/Console.h"
#include <cstdio>

namespace geoputer {

bool GpsApplication::operateBootMode(const UiAction& action) {
    if (!action.confirmed || storage_operation_id_ || gpsState().dataset_applying_) return false;
    if (!usbEntryAllowed(data_) || gpsState().log_control_.kind != ActionKind::None) {
        message("STOP LOGGER FIRST"); return false;
    }
    StorageRequest request{}; request.kind = MessageKind::OperateStorage;
    auto& command = request.payload.storage_command;
    command.operation = action.storage_operation;
    if(command.operation!=StorageOperation::EnterUsbMode && command.operation!=StorageOperation::EnterWifiMode)return false;
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

}

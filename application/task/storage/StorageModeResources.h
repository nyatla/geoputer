#pragma once
#include "StorageCodeImages.h"
#include "StorageScreenResources.h"
#include "galuchat/PlaceLookup.h"
#include "log/LogWriter.h"
#include "usb/UsbStorage.h"
#include "export/LogExporter.h"
#include "../../utils/BootModeResources.h"
#include <memory>
#include "../../wifi/WifiService.h"
namespace geoputer {
struct StorageGpsResources {
    explicit StorageGpsResources(StorageObserver* observer)
        :screen_resources_(code_images_.map.image(),observer),place_lookup_(code_images_.point,selected_dataset_),
         log_writer_(config::LogDirectory,observer) {
        place_lookup_.setObserver(observer);place_lookup_.setFiles(&screen_resources_.work.files());
    }
    StorageCodeImages code_images_;
    SelectedDataset selected_dataset_;
    StorageScreenResources screen_resources_;
    PlaceLookup place_lookup_;
    LogWriter log_writer_;
    uint32_t next_log_retry_=0;
    PlaceRequest place_request_{};
    DatasetRequest dataset_request_{};
    IndexProgress index_progress_{};
    uint32_t index_started_at_=0,index_progress_at_=0;
    bool index_progress_dirty_=false;
};
struct StorageUsbResources {
    explicit StorageUsbResources(StorageObserver* observer):work_(observer){}
    UsbStorage usb_;
    StorageWorkArea work_;
    // Existing lazy converter allocation is confined to this boot mode.
    std::unique_ptr<LogExporter> exporter_;
    enum class Work:uint8_t {None,Prepare,Convert,PublishMenu,PublishResult};
    Work usb_work_=Work::None;
    bool export_started_=false;
    uint32_t export_progress_at_=0;
};
struct StorageWifiResources {WifiService service;};
} // namespace geoputer

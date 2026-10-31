#pragma once
#include "StorageCodeImages.h"
#include "StorageScreenResources.h"
#include "galuchat/PlaceLookup.h"
#include "log/LogWriter.h"
namespace geoputer {
struct GpsStorageResources {
    explicit GpsStorageResources(StorageObserver* observer)
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
}

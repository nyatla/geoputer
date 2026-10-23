#pragma once
#include "task/storage/dataset/DatasetIndex.h"
namespace geoputer::benchmark {
struct IndexGenerationMeasurement {
    uint64_t prepare_us=0,generate_us=0,verify_us[3]{},source_check_us=0;
    uint32_t map_bytes=0,book_bytes=0,index_bytes=0;
};
// Diagnostic only: append the companion .inc to a COPY of DatasetIndex.cpp.
// Uses the production generator and validator, never publishes an index or saves NVS.
IndexGenerationMeasurement measureIndexGeneration(const std::string& directory,
    const DatasetMetadata& metadata,uint8_t drive,StorageObserver* observer=nullptr,
    void (*stage)(const char*)=nullptr);
}

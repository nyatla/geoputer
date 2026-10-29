#pragma once
#include "MapBuildJob.h"
#include "log/LogReader.h"
#include <variant>
#include <cassert>

namespace geoputer {
static_assert(sizeof(MapBuildJob)<=sizeof(LogReader), "Map worker must fit the log workspace");

// Storage-only ownership. Exactly one read job or map job occupies this area.
// A finished map can stay here without occupying the Storage task.
class StorageWorkArea {
public:
    explicit StorageWorkArea(StorageObserver* observer=nullptr)
        : observer_(observer),data_(std::in_place_type<LogReader>,config::LogDirectory,observer,&files_.primary()) {}
    ReadFileSlots& files() {return files_;}
    MapBuildJob* activeMap() {return std::get_if<MapBuildJob>(&data_);}
    const MapBuildJob* activeMap() const {return std::get_if<MapBuildJob>(&data_);}
    LogReader* activeLogs() {return std::get_if<LogReader>(&data_);}
    bool mapBusy() const {const auto* map=activeMap();return map && map->occupied();}
    bool releaseLogs(uint32_t generation) {
        auto* logs=activeLogs();
        // A delayed release must never clear a newer browsing session or a map.
        if(!logs || logs->result().request.screen_generation>generation)return true;
        return logs->releaseForReuse();
    }
    MapBuildJob* forMap() {
        if(auto* map=activeMap())return map;
        if(!activeLogs()->releaseForReuse())return nullptr;
        return &data_.emplace<MapBuildJob>(files_.primary());
    }
    LogReader* forLogs() {
        if(auto* logs=activeLogs())return logs;
        if(mapBusy())return nullptr;
        auto& logs=data_.emplace<LogReader>(config::LogDirectory,observer_,&files_.primary());logs.setDrive(drive_);return &logs;
    }
    LogReader& logs() {auto* result=activeLogs();assert(result);return *result;}
    void setDrive(uint8_t drive) {drive_=drive;if(auto* logs=activeLogs())logs->setDrive(drive);}
private:
    StorageObserver* observer_;
    uint8_t drive_=0xff;
    ReadFileSlots files_; // Destroyed after the active job releases its loans.
    std::variant<LogReader,MapBuildJob> data_;
};
} // namespace geoputer

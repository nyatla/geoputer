#pragma once
#include "galuchat/MapSource.h"
#include "../../map/BoundaryExtractor.h"
#include "../../Config.h"
#include <cstring>

namespace geoputer {
class MapBuildJob {
public:
    bool start(const map::Request& request) {
        if(occupied() || !request.token)return false;
        info_={};info_.request=request;phase_=Phase::Reading;notified_=false;return true;
    }
    bool occupied() const { return phase_!=Phase::Idle; }
    bool reading() const { return phase_==Phase::Reading; }
    bool complete() const { return phase_==Phase::Complete; }
    bool notificationPending() const { return complete() && !notified_; }
    void notificationSent() { notified_=true; }
    const map::FrameInfo& info() const { return info_; }
    void loaded(MapSourceResult result) {
        if(!reading())return;
        info_=result.info;
        if(info_.error!=map::Error::None) {phase_=Phase::Complete;return;}
        if(!image_.resize(info_.width,info_.height)) {fail(map::Error::NoMemory);return;}
        source_=std::move(result);row_=0;phase_=Phase::Rendering;
    }
    void fail(map::Error error) { info_.error=error;source_={};image_.clear();phase_=Phase::Complete; }
    void step() {
        if(phase_!=Phase::Rendering)return;
        const unsigned rows=std::min<unsigned>(config::MapViewRowsPerStep,info_.height-row_);
        const auto error=map::buildBoundaryRows(source_.view(),source_.plan,info_.region,row_,rows,image_);
        if(error!=map::Error::None) {fail(error);return;}
        row_+=rows;
        if(row_==info_.height) {source_={};phase_=Phase::Complete;}
    }
    bool read(const map::Request& request,map::Transfer& transfer) const {
        if(!complete() || info_.error!=map::Error::None || !matches(request) || request.row>=image_.height())return false;
        transfer={};transfer.info=info_;transfer.row=request.row;
        transfer.rows=std::min<size_t>(map::TransferRows,image_.height()-request.row);
        std::memcpy(transfer.pixels,image_.data()+request.row*image_.stride(),transfer.rows*image_.stride());return true;
    }
    bool release(const map::Request& request) {
        if(!occupied())return true;
        if(!matches(request))return false;
        source_={};image_.clear();info_={};row_=0;phase_=Phase::Idle;notified_=false;return true;
    }
private:
    bool matches(const map::Request& request) const { return request.token==info_.request.token &&
        request.screen_generation==info_.request.screen_generation && request.dataset_generation==info_.request.dataset_generation; }
    enum class Phase { Idle, Reading, Rendering, Complete };
    Phase phase_=Phase::Idle;
    map::FrameInfo info_{};
    MapSourceResult source_;
    map::BoundaryImage image_;
    unsigned row_=0;
    bool notified_=false;
};
} // namespace geoputer

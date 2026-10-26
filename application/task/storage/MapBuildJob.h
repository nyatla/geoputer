#pragma once
#include "galuchat/MapSource.h"
#include "../../map/BoundaryExtractor.h"
#include "../../Config.h"
#include "../../utils/FileReadBuffer.h"
#include <optional>

namespace geoputer {
// Storage borrows the UI's fixed back image for one accepted operation.
// The same bytes serve the index Reader first, then the finished boundary image.
class MapBuildJob {
public:
    bool start(const map::Request& request,map::MapSession& session) {
        if(occupied() || !request.token || !map::validBuffer(request.buffer))return false;
        session_=&session;info_={};info_.request=request;phase_=Phase::Reading;notified_=false;
        index_buffer_.emplace(request.buffer.data,request.buffer.capacity);
        if(request.reuse) {
            if(!session.matches(request)) {fail(map::Error::Stale);return true;}
            info_=session.info;info_.request=request;info_.region=request.region;
            if(!prepareImage()) {fail(map::Error::InvalidInput);return true;}
            row_=0;phase_=Phase::Rendering;
        } else session.invalidate(request);
        return true;
    }
    BorrowedReadBuffer* indexReadBuffer() {return reading()?&*index_buffer_:nullptr;}
    bool occupied() const {return phase_!=Phase::Idle;}
    bool reading() const {return phase_==Phase::Reading;}
    bool complete() const {return phase_==Phase::Complete;}
    bool notificationPending() const {return complete() && !notified_;}
    void notificationSent() {notified_=true;}
    const map::FrameInfo& info() const {return info_;}
    void loaded(MapSourceResult result) {
        if(!reading())return;
        if(index_buffer_->inUse()) {fail(map::Error::InvalidInput);return;}
        // Never accept a loader result for a different buffer or operation.
        if(!map::sameRequest(info_.request,result.info.request)) {fail(map::Error::Stale);return;}
        info_=result.info;*session_=std::move(result);
        if(info_.error!=map::Error::None) {phase_=Phase::Complete;return;}
        if(!session_->pixels || !prepareImage()) {fail(map::Error::InvalidInput);return;}
        session_->valid=true;
        if(!session_->capacity)session_->capacity=session_->view().count;
        row_=0;phase_=Phase::Rendering;
    }
    void fail(map::Error error) {info_.error=error;image_={};phase_=Phase::Complete;}
    void step() {
        if(phase_!=Phase::Rendering)return;
        const unsigned rows=std::min<unsigned>(config::MapViewRowsPerStep,info_.height-row_);
        const auto error=map::buildBoundaryRows(session_->view(),session_->plan,info_.region,row_,rows,image_);
        if(error!=map::Error::None) {fail(error);return;}
        row_+=rows;
        if(row_==info_.height) {session_->info=info_;phase_=Phase::Complete;}
    }
    // Acknowledgement is the write-ownership barrier. No image pointer remains
    // in this worker after return, so App can safely commit/discard the UI loan.
    bool release(const map::Request& request) {
        if(index_buffer_ && index_buffer_->inUse())return false;
        if(!occupied())return true;
        if(!map::sameRequest(request,info_.request))return false;
        image_={};index_buffer_.reset();info_={};row_=0;phase_=Phase::Idle;notified_=false;session_=nullptr;return true;
    }
private:
    bool prepareImage() {
        if(!info_.width || !info_.height || info_.width>map::MaxWidth || info_.height>map::MaxHeight ||
           (index_buffer_ && index_buffer_->inUse()))return false;
        image_={info_.request.buffer.data,info_.width,info_.height};
        std::fill_n(image_.data(),image_.bytes(),0);return true;
    }
    enum class Phase {Idle,Reading,Rendering,Complete};
    Phase phase_=Phase::Idle;
    map::FrameInfo info_{};
    map::MapSession* session_=nullptr;
    map::BoundaryImageView image_;
    std::optional<BorrowedReadBuffer> index_buffer_;
    unsigned row_=0;
    bool notified_=false;
};
} // namespace geoputer

#pragma once
#include "galuchat/MapSource.h"
#include "ReadFile.h"
#include "ReadWorkspace.h"
#include "../../map/BoundaryExtractor.h"
#include "../../Config.h"
#include "../../utils/FileReadBuffer.h"
#include <optional>

namespace geoputer {
// Storage borrows the UI's fixed back image for one accepted operation.
// The same bytes serve index and GLC Readers in turn, then the boundary image.
class MapBuildJob {
public:
    ~MapBuildJob() {assert(!file_.inUse() && (!read_buffer_ || !read_buffer_->inUse()));finishReadMemory();}
    bool start(const map::Request& request,map::MapSession& session) {
        if(occupied() || file_.inUse() || !request.token || !map::validBuffer(request.buffer) || !session.pixels || !session.capacity)return false;
        session_=&session;info_={};info_.request=request;phase_=Phase::Reading;notified_=false;
        if(request.reuse || session.samePixels(request)) {
            if(request.reuse && !session.matches(request)) {fail(map::Error::Stale);return true;}
            info_=session.info;info_.request=request;info_.region=(request.reuse || request.region_known)?request.region:
                session.view().get(session.plan.center_x,session.plan.center_y);
            if(!prepareImage()) {fail(map::Error::InvalidInput);return true;}
            row_=0;phase_=Phase::Rendering;
        } else {
            session.invalidate(request);
            workspace_=BorrowedLayout<MapReadWorkspace>::construct(request.buffer.data,request.buffer.capacity);
            read_buffer_.emplace(workspace_->reader.data(),workspace_->reader.size());
        }
        return true;
    }
    BorrowedReadBuffer* readBuffer() {return reading()?&*read_buffer_:nullptr;}
    ReadFileSlot* readFile() {return reading()?&file_:nullptr;}
    NativePathBuffer pathBuffer()const {return reading() && workspace_?workspace_->nativePath():NativePathBuffer{};}
    bool occupied() const {return phase_!=Phase::Idle;}
    bool reading() const {return phase_==Phase::Reading;}
    bool complete() const {return phase_==Phase::Complete;}
    bool notificationPending() const {return complete() && !notified_;}
    void notificationSent() {notified_=true;}
    const map::FrameInfo& info() const {return info_;}
    void loaded(MapSourceResult result) {
        if(!reading())return;
        if(read_buffer_->inUse() || file_.inUse()) {fail(map::Error::InvalidInput);return;}
        // Never accept a loader result for a different buffer or operation.
        if(!map::sameRequest(info_.request,result.info.request)) {fail(map::Error::Stale);return;}
        info_=result.info;*session_=std::move(result);
        if(info_.error!=map::Error::None) {phase_=Phase::Complete;return;}
        if(!session_->pixels || !prepareImage()) {fail(map::Error::InvalidInput);return;}
        session_->valid=true;
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
        if(file_.inUse() || (read_buffer_ && read_buffer_->inUse()))return false;
        if(!occupied())return true;
        if(!map::sameRequest(request,info_.request))return false;
        image_={};finishReadMemory();info_={};row_=0;phase_=Phase::Idle;notified_=false;session_=nullptr;return true;
    }
private:
    void finishReadMemory() {
        read_buffer_.reset();
        if(workspace_) {
            BorrowedLayout<MapReadWorkspace>::destroy(info_.request.buffer.data,info_.request.buffer.capacity);
            workspace_=nullptr;
        }
    }
    bool prepareImage() {
        if(!info_.width || !info_.height || info_.width>map::MaxWidth || info_.height>map::MaxHeight ||
           (read_buffer_ && read_buffer_->inUse()) || file_.inUse())return false;
        finishReadMemory();
        image_={info_.request.buffer.data,info_.width,info_.height};
        std::fill_n(image_.data(),image_.bytes(),0);return true;
    }
    enum class Phase {Idle,Reading,Rendering,Complete};
    // This FIL occupies the same fixed StorageWorkArea bytes as LogReader.
    ReadFileSlot file_;
    Phase phase_=Phase::Idle;
    map::FrameInfo info_{};
    map::MapSession* session_=nullptr;
    map::BoundaryImageView image_;
    std::optional<BorrowedReadBuffer> read_buffer_;
    MapReadWorkspace* workspace_=nullptr;
    unsigned row_=0;
    bool notified_=false;
};
} // namespace geoputer

#pragma once
#include "BoundaryImageView.h"
#include <cmath>

namespace geoputer::map {
// UI owns both fixed images. Only Storage may touch a leased back image.
// clear() hides old screen content but cannot revoke an outstanding lease.
class MapFrameReceiver {
public:
    bool begin(const Request& request,BufferLease& buffer) {
        if(leased_ || !request.token || request.buffer.data || request.buffer.id || request.buffer.capacity)return false;
        auto& back=images_[1-front_];
        if(!back.bind(MaxWidth,MaxHeight))return false;
        if(++next_lease_==0)++next_lease_;
        pending_=request;pending_.buffer={back.data(),back.Capacity,next_lease_};
        work_=false;leased_=publishable_=true;buffer=pending_.buffer;return true;
    }
    // App calls this only after Storage has returned its write ownership.
    bool commit(const FrameInfo& info) {
        if(work_ || !leased_ || !publishable_ || !sameRequest(pending_,info.request) || info.error!=Error::None ||
           info.width!=MaxWidth || info.height!=MaxHeight ||
           !std::isfinite(info.meters_per_pixel) || info.meters_per_pixel<=0)return false;
        const auto& back=images_[1-front_];
        // Value 3 is reserved. Test all four packed pixels per byte.
        for(size_t i=0;i<BoundaryBytes;++i)
            if(back.data()[i] & (back.data()[i]>>1) & 0x55)return false;
        front_=1-front_;shown_=info;pending_={};leased_=false;return true;
    }
    // App calls this after cancellation/release acknowledgement, even if the
    // originating screen no longer exists. A stale return cannot end a new lease.
    bool discard(const Request& request) {
        if(work_ || !leased_ || !sameRequest(pending_,request))return false;
        images_[1-front_].clear();pending_={};leased_=false;return true;
    }
    // A non-image operation borrows the same unused back slot. It cannot be
    // promoted to front, and its lifetime is independent of screen visibility.
    bool beginWork(uint32_t token,BufferLease& buffer) {
        Request request{};request.token=token;
        if(!begin(request,buffer))return false;
        work_=true;return true;
    }
    bool returnWork(uint32_t token,const BufferLease& buffer) {
        if(!work_ || !leased_ || token!=pending_.token || !sameBuffer(buffer,pending_.buffer))return false;
        images_[1-front_].clear();pending_={};leased_=publishable_=work_=false;return true;
    }
    void clear() {
        images_[front_].clear();shown_={};publishable_=false;
        if(!leased_)images_[1-front_].clear();
    }
    bool leased() const {return leased_;}
    const FixedBoundaryImage& image() const {return images_[front_];}
    const FrameInfo& info() const {return shown_;}
private:
    FixedBoundaryImage images_[2];
    FrameInfo shown_{};
    Request pending_{};
    uint32_t next_lease_=0;
    unsigned front_=0;
    bool leased_=false,publishable_=false,work_=false;
};
} // namespace geoputer::map

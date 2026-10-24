#pragma once
#include "BoundaryImage.h"
#include <cstring>

namespace geoputer::map {
class MapFrameReceiver {
public:
    bool begin(const FrameInfo& info) {
        receiving_ = false; next_row_ = 0;
        if (!info.request.token || info.error != Error::None || !back_.resize(info.width,info.height)) return false;
        incoming_ = info; receiving_ = true; return true;
    }
    bool write(const Transfer& transfer) {
        if (!receiving_ || transfer.info.request.token != incoming_.request.token ||
            transfer.info.request.screen_generation != incoming_.request.screen_generation ||
            transfer.info.request.dataset_generation != incoming_.request.dataset_generation ||
            transfer.info.width != incoming_.width || transfer.info.height != incoming_.height ||
            transfer.row != next_row_ || !transfer.rows || transfer.rows > TransferRows ||
            transfer.rows > back_.height() - next_row_) return false;
        const size_t length = back_.stride() * transfer.rows;
        for (unsigned y=0;y<transfer.rows;++y) for(unsigned x=0;x<back_.width();++x)
            if (((transfer.pixels[y*back_.stride()+x/4] >> (6-2*(x%4)))&3)==3) return false;
        if (back_.width()%4) for(unsigned y=0;y<transfer.rows;++y)
            if(transfer.pixels[y*back_.stride()+back_.stride()-1]&((1u<<(2*(4-back_.width()%4)))-1)) return false;
        std::memcpy(back_.data()+next_row_*back_.stride(),transfer.pixels,length);
        next_row_ += transfer.rows; return true;
    }
    bool commit(const FrameInfo& info) {
        if (!receiving_ || next_row_ != back_.height() || info.request.token != incoming_.request.token ||
            info.request.screen_generation != incoming_.request.screen_generation ||
            info.request.dataset_generation != incoming_.request.dataset_generation) return false;
        front_.swap(back_); shown_ = incoming_; receiving_ = false; return true;
    }
    void clear() { front_.clear(); back_.clear(); shown_={}; incoming_={}; receiving_=false;next_row_=0; }
    const BoundaryImage& image() const { return front_; }
    const FrameInfo& info() const { return shown_; }
private:
    BoundaryImage front_, back_;
    FrameInfo shown_{}, incoming_{};
    unsigned next_row_ = 0;
    bool receiving_ = false;
};
} // namespace geoputer::map

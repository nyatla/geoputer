#pragma once
#include "../../../PlaceText.h"
#include "../../../utils/Utf8.h"
#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace geoputer {
// Non-owning writer into the reply itself. No second pair of output strings.
class PlaceEncodingError : public std::runtime_error {public: using std::runtime_error::runtime_error;};
class PlaceLineOutput {
public:
    PlaceLineOutput(char* data,size_t capacity,PlaceTextMode mode=PlaceTextMode::Japanese16,
                    unsigned max_pixels=config::PlaceLinePixels)
        :data_(data),capacity_(std::min(capacity,config::PlaceCachedLineBytes)),mode_(mode),max_pixels_(max_pixels) {
        if(!data || capacity<4)throw std::invalid_argument("place output capacity is too small");
        data_[0]='\0';
    }
    bool full()const {return clipped_;}
    char* tail()const {return data_+bytes_;}
    size_t available()const {return capacity_-1-bytes_;}
    void append(std::string_view text) {
        if(full())return;
        const size_t count=std::min(text.size(),available());
        if(count)std::memmove(tail(),text.data(),count);
        accept(count,count<text.size());
    }
    // Bytes have already been written into tail(), in their final order.
    void accept(size_t count,bool more) {
        const size_t start=bytes_;size_t used=0;
        while(used<count) {
            const auto first=static_cast<unsigned char>(data_[start+used]);
            const size_t length=first<0x80?1:first<0xe0?2:first<0xf0?3:4;
            if(length>count-used) {
                if(more){clip();return;}
                throw PlaceEncodingError("invalid UTF-8 place name");
            }
            if(!validUtf8({data_+start+used,length}))throw PlaceEncodingError("invalid UTF-8 place name");
            const bool ascii=first<0x80;
            if(pixels_+placeCharacterWidth(first,mode_)>max_pixels_ ||
               (ascii?ascii_>=config::PlaceAsciiLimit:wide_>=config::PlaceWideLimit)) {clip();return;}
            pixels_+=placeCharacterWidth(first,mode_);bytes_+=length;used+=length;
            if(ascii)++ascii_;else ++wide_;
            if(pixels_+placeEllipsisWidth(mode_)<=max_pixels_ && bytes_+3<capacity_)short_bytes_=bytes_;
        }
        data_[bytes_]='\0';if(more)clip();
    }
private:
    void clip() {bytes_=short_bytes_;std::memcpy(data_+bytes_,"...",3);bytes_+=3;std::memset(data_+bytes_,0,capacity_-bytes_);clipped_=true;}
    char* data_;size_t capacity_,bytes_=0,short_bytes_=0;
    unsigned pixels_=0,ascii_=0,wide_=0;bool clipped_=false;
    PlaceTextMode mode_;
    unsigned max_pixels_;
};
} // namespace geoputer

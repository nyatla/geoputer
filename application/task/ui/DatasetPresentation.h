#pragma once
#include "UiTypes.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace geoputer {
// A large Japanese cell is 16px; English ASCII is 8px, Japanese remains 16px.
inline unsigned datasetTextLines(const char* text,bool japanese,unsigned wanted,char* out,size_t capacity,unsigned columns=26) {
    unsigned row=0,width=0;size_t used=0;
    if(capacity) out[0]=0;
    for(size_t i=0;text[i];) {
        const auto c=static_cast<unsigned char>(text[i]);
        const size_t bytes=c<0x80?1:c<0xe0?2:c<0xf0?3:4;
        const unsigned advance=japanese || c>=0x80?2:1;
        if(width+advance>columns) {++row;width=0;}
        if(row==wanted && used+bytes<capacity) {std::memcpy(out+used,text+i,bytes);used+=bytes;out[used]=0;}
        width+=advance;i+=bytes;
    }
    return row+1;
}
inline void datasetSingleLine(const char* text,bool ja,char* out,size_t capacity,unsigned columns=26) {
    if(datasetTextLines(text,ja,0,out,capacity,columns)<=1 || capacity<4) return;
    auto units=[&]() {unsigned total=0;for(size_t i=0;out[i];) {
        const auto ch=static_cast<unsigned char>(out[i]);total+=ja || ch>=128?2:1;
        i+=ch<128?1:ch<224?2:ch<240?3:4;
    }return total;};
    size_t bytes=std::strlen(out);
    while(bytes && (units()+(ja?6:3)>columns || bytes+3>=capacity)) {
        --bytes;while(bytes && (static_cast<unsigned char>(out[bytes])&0xc0)==0x80) --bytes;
        out[bytes]=0;
    }
    std::memcpy(out+bytes,"...",4);
}
inline unsigned datasetDetailRows(const DatasetView& view,bool ja) {
    char unused[1];return 1+datasetTextLines(view.info.description,ja,UINT32_MAX,unused,sizeof(unused))+7;
}
inline bool datasetRecoverable(DatasetError error) {
    return error==DatasetError::None || error==DatasetError::NotStopped || error==DatasetError::SaveFailed;
}
inline bool datasetDetailLine(const DatasetView& view,unsigned row,bool ja,char* out,size_t size) {
    char unused[1];const auto lines=datasetTextLines(view.info.description,ja,UINT32_MAX,unused,sizeof(unused));
    if(!row) {datasetSingleLine(view.info.title,ja,out,size);return false;}
    if(row<=lines) {datasetTextLines(view.info.description,ja,row-1,out,size);return false;}
    const auto& d=view.info;
    switch(row-lines-1) {
        case 0:std::snprintf(out,size,"X 1/%lu deg",static_cast<unsigned long>(d.unit_inv_x));return true;
        case 1:std::snprintf(out,size,"Y 1/%lu deg",static_cast<unsigned long>(d.unit_inv_y));return true;
        case 2:std::snprintf(out,size,"W %.5f S %.5f",d.west,d.south);return true;
        case 3:std::snprintf(out,size,"E %.5f N %.5f",d.east,d.north);return true;
        case 4:std::snprintf(out,size,"CHUNKS %llu / %u",static_cast<unsigned long long>(d.chunks),static_cast<unsigned>(config::MaxDatasetChunks));return true;
        case 5:std::snprintf(out,size,"LANG %.21s",d.language);return true;
        default:std::snprintf(out,size,"%s",ja?"適用して再起動":"Apply and restart");return false;
    }
}
} // namespace geoputer

#pragma once
#include "GpsUiTypes.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace geoputer {
inline const char* datasetMenuLabel(unsigned row,bool ja) {
    switch(row) {
        case 0:return ja?"データセットを選択":"Select dataset";
        case 1:return ja?"キャッシュ再生成":"Rebuild cache";
        default:return ja?"キャッシュ検証":"Verify cache";
    }
}
inline DatasetError datasetMutationBlock(const UiData& data,bool requires_card) {
    if(data.storage.busy || data.datasets().loading || data.datasets().applying) return DatasetError::Busy;
    if(!datasetLogStopped(data.recording,data.storage.log)) return DatasetError::NotStopped;
    if(requires_card && data.storage.card_state!=CardState::Mounted) return DatasetError::CardUnavailable;
    return DatasetError::None;
}
inline DatasetError datasetSelectionBlock(const UiData& data,bool requires_card) {
    const auto reason=datasetMutationBlock(data,requires_card);
    return reason!=DatasetError::None?reason:data.storage.settings_state==SettingsState::Unavailable?DatasetError::SettingsUnavailable:DatasetError::None;
}
inline DatasetError datasetMenuBlock(const UiData& data,unsigned row) {
    if(row) return datasetMutationBlock(data,true);
    return data.storage.busy || data.datasets().loading || data.datasets().applying?DatasetError::Busy:DatasetError::None;
}
inline DatasetError datasetRowBlock(const UiData& data,unsigned row) {
    if(row>=data.datasets().count) return DatasetError::Missing;
    const auto& entry=data.datasets().rows[row];
    if(!entry.number) {
        const auto reason=datasetSelectionBlock(data,false);
        return reason!=DatasetError::None?reason:!data.settings.dataset?DatasetError::AlreadyUnselected:DatasetError::None;
    }
    if(entry.error!=DatasetError::None) return entry.error;
    if(data.datasets().purpose!=DatasetPurpose::Select) return datasetMutationBlock(data,true);
    if(data.storage.busy || data.datasets().loading) return DatasetError::Busy;
    return data.storage.card_state==CardState::Mounted?DatasetError::None:DatasetError::CardUnavailable;
}
inline const char* indexPhaseText(IndexPhase phase,bool ja) {
    switch(phase) {
        case IndexPhase::Preparing:return ja?"準備中":"Preparing";
        case IndexPhase::Map:return ja?"地図索引":"Map index";
        case IndexPhase::TI:return ja?"辞書の枝":"Wordbook roots";
        case IndexPhase::TT:return ja?"辞書の文字列":"Wordbook texts";
        case IndexPhase::TM:return ja?"辞書の断片":"Wordbook tokens";
        case IndexPhase::Structure:return ja?"構造とCRC":"Structure / CRC";
        case IndexPhase::Publish:return ja?"保存中":"Publishing";
        case IndexPhase::Complete:return ja?"完了":"Complete";
        case IndexPhase::Failed:return ja?"失敗":"Failed";
        default:return ja?"受付待ち":"Waiting";
    }
}

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
        default:std::snprintf(out,size,"%s",ja?"適用する":"Apply");return false;
    }
}
} // namespace geoputer

#pragma once
#include "GpsUiTypes.h"
#include "../../utils/UiTextClip.h"
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
    if(!entry.id) {
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
        const unsigned advance=japanese || (c>=0x80 && !isEllipsis(text+i))?2:1;
        if(width+advance>columns) {++row;width=0;}
        if(row==wanted && used+bytes<capacity) {std::memcpy(out+used,text+i,bytes);used+=bytes;out[used]=0;}
        width+=advance;i+=bytes;
    }
    return row+1;
}
inline void datasetSingleLine(const char* text,bool ja,char* out,size_t capacity,unsigned max_pixels=216) {
    clipUiText(text,ja,out,capacity,max_pixels);
}
inline const char* datasetRowTitle(const DatasetRow& row,bool ja) {
    return row.id?row.title:(ja?"未選択":"None");
}
inline constexpr unsigned DatasetChoiceRows=4; // Title, Wordbook, details, apply.
inline constexpr unsigned DatasetDetailVisibleRows=4;
inline void datasetWordbookList(const DatasetView& view,char* out,size_t capacity) {
    if(!capacity)return;
    size_t used=0;out[0]=0;
    for(unsigned i=0;i<view.info.language_count && i<config::MaxDatasetLanguages;++i){
        const auto* type=view.info.wordbook_types[i];
        const size_t length=std::strlen(type);
        if(used+length+(i?1:0)>=capacity)break;
        if(i)out[used++]='/';
        std::memcpy(out+used,type,length);used+=length;out[used]=0;
    }
}
inline unsigned datasetDetailRows(const DatasetView& view,bool ja) {
    char types[config::MaxDatasetLanguages*config::DatasetLanguageBytes],unused[1];
    datasetWordbookList(view,types,sizeof(types));
    return datasetTextLines(view.info.title,ja,UINT32_MAX,unused,sizeof(unused))+
        1+datasetTextLines(types,false,UINT32_MAX,unused,sizeof(unused))+
        datasetTextLines(view.info.description,ja,UINT32_MAX,unused,sizeof(unused))+5;
}
inline bool datasetDetailLine(const DatasetView& view,unsigned row,bool ja,char* out,size_t size) {
    char types[config::MaxDatasetLanguages*config::DatasetLanguageBytes],unused[1];
    datasetWordbookList(view,types,sizeof(types));
    const auto title_lines=datasetTextLines(view.info.title,ja,UINT32_MAX,unused,sizeof(unused));
    if(row<title_lines) {datasetTextLines(view.info.title,ja,row,out,size);return false;}
    row-=title_lines;
    if(!row) {std::snprintf(out,size,"%s",ja?"辞書":"WORD");return !ja;}
    const auto type_lines=datasetTextLines(types,false,UINT32_MAX,unused,sizeof(unused));
    if(row<=type_lines) {datasetTextLines(types,false,row-1,out,size);return true;}
    row-=type_lines;
    const auto lines=datasetTextLines(view.info.description,ja,UINT32_MAX,unused,sizeof(unused));
    if(row<=lines) {datasetTextLines(view.info.description,ja,row-1,out,size);return false;}
    const auto& d=view.info;
    switch(row-lines-1) {
        case 0:std::snprintf(out,size,"X 1/%lu deg",static_cast<unsigned long>(d.unit_inv_x));return true;
        case 1:std::snprintf(out,size,"Y 1/%lu deg",static_cast<unsigned long>(d.unit_inv_y));return true;
        case 2:std::snprintf(out,size,"W %.5f S %.5f",d.west,d.south);return true;
        case 3:std::snprintf(out,size,"E %.5f N %.5f",d.east,d.north);return true;
        case 4:std::snprintf(out,size,"CHUNKS %llu / %u",static_cast<unsigned long long>(d.chunks),static_cast<unsigned>(config::MaxDatasetChunks));return true;
        default:out[0]=0;return true;
    }
}
} // namespace geoputer

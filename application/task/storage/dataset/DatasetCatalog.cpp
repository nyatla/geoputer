#include "DatasetCatalog.h"
#include "DatasetIndex.h"
#include "../galuchat/ObservedFileReader.h"
#include <galuchat/gis_wordbook_reader.hpp>
#include <algorithm>
#include <cstdio>
#include <sys/stat.h>

namespace geoputer {
std::string DatasetCatalog::directory(unsigned number) const {
    char digits[12];std::snprintf(digits,sizeof(digits),"%02u",number);
    return root_+"/"+digits;
}
DatasetView DatasetCatalog::read(const DatasetRequest& request,UiLanguage ui) const {
    DatasetView view{};view.purpose=request.purpose;
    try {
        const bool listing=request.action==DatasetAction::Open || request.action==DatasetAction::Older || request.action==DatasetAction::Newer;
        if(listing) {
            const bool reverse=request.action==DatasetAction::Older;
            const int minimum=request.purpose==DatasetPurpose::Select?0:1;
            int start=std::max<int>(minimum,request.number);
            if(request.action==DatasetAction::Older) --start;
            if(request.action==DatasetAction::Newer) ++start;
            auto exists=[&](int n) {if(!n) return minimum==0;SdAccessScope access(observer_);struct stat st{};return !stat(directory(n).c_str(),&st) && S_ISDIR(st.st_mode);};
            for(int n=start;n>=minimum && n<=99 && view.count<config::DatasetPageRows;n+=reverse?-1:1) {
                if(!exists(n)) continue;
                auto& row=view.rows[view.count++];row.number=n;
                if(!n) continue;
                const auto metadata=readDatasetMetadata(directory(n),ui,"",observer_);
                row.error=metadata.error;
                datasetCopyText(row.title,sizeof(row.title),metadata.error==DatasetError::None?metadata.title:std::string(ui==UiLanguage::Japanese?"[メタデータ不正]":"[Invalid metadata]"));
            }
            if(reverse) {std::reverse(view.rows,view.rows+view.count);view.selected=view.count?view.count-1:0;}
            if(view.count) {
                for(int n=minimum;n<view.rows[0].number;++n) if(exists(n)) {view.has_older=true;break;}
                for(int n=view.rows[view.count-1].number+1;n<=99;++n) if(exists(n)) {view.has_newer=true;break;}
            }
            return view;
        }
        view.detail=true;view.info={};view.number=request.number;
        if(!request.number || request.number>99) {view.error=DatasetError::Missing;return view;}
        auto metadata=readDatasetMetadata(directory(request.number),ui,request.language,observer_);
        if(metadata.error!=DatasetError::None) {view.error=metadata.error;return view;}
        if(request.action==DatasetAction::Language) {
            const auto index=std::find(metadata.languages.begin(),metadata.languages.end(),metadata.language)-metadata.languages.begin();
            const auto next=(index+metadata.languages.size()+(request.direction<0?-1:1))%metadata.languages.size();
            metadata=readDatasetMetadata(directory(request.number),ui,metadata.languages[next].c_str(),observer_);
            if(metadata.error!=DatasetError::None) {view.error=metadata.error;return view;}
        }
        datasetCopyText(view.info.title,sizeof(view.info.title),metadata.title);
        datasetCopyText(view.info.description,sizeof(view.info.description),metadata.description);
        datasetCopyText(view.info.language,sizeof(view.info.language),metadata.language);
        view.info.language_count=metadata.languages.size();
        view.info.language_index=std::find(metadata.languages.begin(),metadata.languages.end(),metadata.language)-metadata.languages.begin();
        const auto map_path=directory(request.number)+"/"+metadata.map;
        {
            SdAccessScope access(observer_); struct stat st{};
            if (stat(map_path.c_str(),&st) || !S_ISREG(st.st_mode) ||
                stat((directory(request.number)+"/"+metadata.wordbook).c_str(),&st) || !S_ISREG(st.st_mode)) {
                view.error=DatasetError::Missing; return view;
            }
        }
        const auto info=DatasetIndex(drive_,observer_).inspectMap(map_path);
        auto u32=[&](size_t offset) { const auto* p=info.header.data()+offset; return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24; };
        view.info.chunks=u32(36); view.info.unit_inv_x=u32(40); view.info.unit_inv_y=u32(44);
        view.info.west=double(info.west)/view.info.unit_inv_x; view.info.south=double(info.south)/view.info.unit_inv_y;
        view.info.east=(double(info.west)+info.width)/view.info.unit_inv_x;
        view.info.north=(double(info.south)+info.height)/view.info.unit_inv_y;
        galuchat::GaluchatGisWordBookReader book(std::make_shared<ObservedFileReaderFactory>(
            directory(request.number)+"/"+metadata.wordbook,observer_,nullptr,
            FileReadOptions{config::WordBookReadBufferBytes,config::FileReadThroughSkipBytes},drive_),0);
        if (!wordBookUsesUtf8(book.metadata())) view.error=DatasetError::Encoding;
    } catch(const MapChunkLimitExceeded& error) {view.error=DatasetError::TooManyChunks;view.info.chunks=error.count;}
    catch(const std::bad_alloc&) {view.error=DatasetError::NoMemory;}
    catch(const std::exception&) {view.error=DatasetError::Data;}
    return view;
}
} // namespace geoputer

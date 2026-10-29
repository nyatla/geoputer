#include "DatasetCatalog.h"
#include "DatasetIndex.h"
#include <algorithm>
#include <cstdio>
#include <sys/stat.h>

namespace geoputer {
TemporaryPath DatasetCatalog::directory(unsigned number) const {
    return datasetDirectory(static_cast<uint8_t>(number),root_);
}
DatasetView DatasetCatalog::read(const DatasetRequest& request,UiLanguage ui) const {
    DatasetView result{};readInto(request,ui,result);return result;
}
void DatasetCatalog::readInto(const DatasetRequest& request,UiLanguage ui,DatasetView& view) const {
    view={};view.purpose=request.purpose;
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
                DatasetMetadata metadata;
                readDatasetMetadataInto(directory(n).view(),ui,"",metadata,observer_,{row.title,sizeof(row.title)});
                row.error=metadata.error;
                if(row.error!=DatasetError::None)datasetCopyText(row.title,sizeof(row.title),ui==UiLanguage::Japanese?"[メタデータ不正]":"[Invalid metadata]");
            }
            if(reverse) {std::reverse(view.rows,view.rows+view.count);view.selected=view.count?view.count-1:0;}
            if(view.count) {
                for(int n=minimum;n<view.rows[0].number;++n) if(exists(n)) {view.has_older=true;break;}
                for(int n=view.rows[view.count-1].number+1;n<=99;++n) if(exists(n)) {view.has_newer=true;break;}
            }
            return;
        }
        view.detail=true;view.info={};view.number=request.number;
        if(!request.number || request.number>99) {view.error=DatasetError::Missing;return;}
        DatasetMetadata metadata;
        readDatasetMetadataInto(directory(request.number).view(),ui,request.language,metadata,observer_,
            {view.info.title,sizeof(view.info.title),view.info.description,sizeof(view.info.description)},
            request.action==DatasetAction::Language?(request.direction<0?-1:1):0);
        if(metadata.error!=DatasetError::None) {view.error=metadata.error;return;}
        datasetCopyText(view.info.language,sizeof(view.info.language),metadata.language);
        view.info.language_count=metadata.language_count;view.info.language_index=metadata.language_index;
        const auto map_path=TemporaryPath({directory(request.number).view(),"/",metadata.map});
        {
            SdAccessScope access(observer_); struct stat st{};
            if (stat(map_path.c_str(),&st) || !S_ISREG(st.st_mode) ||
                stat(TemporaryPath({directory(request.number).view(),"/",metadata.wordbook}).c_str(),&st) || !S_ISREG(st.st_mode)) {
                view.error=DatasetError::Missing; return;
            }
        }
        DatasetIndex reader(drive_,observer_,nullptr,nullptr,files_,work_);
        const auto info=reader.inspectMap(map_path.view());
        auto u32=[&](size_t offset) { const auto* p=info.header.data()+offset; return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24; };
        view.info.chunks=u32(36); view.info.unit_inv_x=u32(40); view.info.unit_inv_y=u32(44);
        view.info.west=double(info.west)/view.info.unit_inv_x; view.info.south=double(info.south)/view.info.unit_inv_y;
        view.info.east=(double(info.west)+info.width)/view.info.unit_inv_x;
        view.info.north=(double(info.south)+info.height)/view.info.unit_inv_y;
        reader.inspectWordBook(TemporaryPath({directory(request.number).view(),"/",metadata.wordbook}).view());
    } catch(const MapChunkLimitExceeded& error) {view.error=DatasetError::TooManyChunks;view.info.chunks=error.count;}
    catch(const std::bad_alloc&) {view.error=DatasetError::NoMemory;}
    catch(const std::exception&) {view.error=DatasetError::Data;}
    return;
}
} // namespace geoputer

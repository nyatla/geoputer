#include "DatasetCatalog.h"
#include "../galuchat/PlaceLookup.h"
#include <algorithm>
#include <cstdio>
#include <sys/stat.h>

namespace geoputer {
std::string DatasetCatalog::directory(unsigned number) const {
    char digits[12];std::snprintf(digits,sizeof(digits),"%02u",number);
    return root_+"/"+digits;
}
DatasetView DatasetCatalog::read(const DatasetRequest& request,UiLanguage ui) const {
    DatasetView view{};
    try {
        const bool listing=request.action==DatasetAction::Open || request.action==DatasetAction::Older || request.action==DatasetAction::Newer;
        if(listing) {
            const bool reverse=request.action==DatasetAction::Older;
            int start=request.number?request.number:1;
            if(request.action==DatasetAction::Older) --start;
            if(request.action==DatasetAction::Newer) ++start;
            auto exists=[&](int n) {SdAccessScope access(observer_);struct stat st{};return !stat(directory(n).c_str(),&st) && S_ISDIR(st.st_mode);};
            for(int n=start;n>=1 && n<=99 && view.count<config::DatasetPageRows;n+=reverse?-1:1) {
                if(!exists(n)) continue;
                auto& row=view.rows[view.count++];row.number=n;
                const auto metadata=readDatasetMetadata(directory(n),ui,"",observer_);
                datasetCopyText(row.title,sizeof(row.title),metadata.error==DatasetError::None?metadata.title:std::string(ui==UiLanguage::Japanese?"[メタデータ不正]":"[Invalid metadata]"));
            }
            if(reverse) {std::reverse(view.rows,view.rows+view.count);view.selected=view.count?view.count-1:0;}
            if(view.count) {
                for(int n=1;n<view.rows[0].number;++n) if(exists(n)) {view.has_older=true;break;}
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
        auto probe=std::make_unique<PlaceLookup>(directory(request.number));
        probe->setObserver(observer_);
        probe->configure(directory(request.number),metadata.language);
        const auto result=probe->prepare();
        view.info.chunks=result.dataset.chunks;
        switch(result.state) {
            case PlaceState::Ready:probe->describe(view.info);break;
            case PlaceState::DatasetMissing:view.error=DatasetError::Missing;break;
            case PlaceState::EncodingError:view.error=DatasetError::Encoding;break;
            case PlaceState::TooManyChunks:view.error=DatasetError::TooManyChunks;break;
            case PlaceState::OutOfMemory:view.error=DatasetError::NoMemory;break;
            default:view.error=DatasetError::Data;break;
        }
    } catch(const std::bad_alloc&) {view.error=DatasetError::NoMemory;}
    catch(const std::exception&) {view.error=DatasetError::Data;}
    return view;
}
} // namespace geoputer

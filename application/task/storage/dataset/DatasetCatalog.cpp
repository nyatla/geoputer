#include "DatasetCatalog.h"
#include "DatasetIndex.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace geoputer {
TemporaryPath DatasetCatalog::directory(std::string_view id) const {
    return datasetDirectory(id,root_);
}
DatasetView DatasetCatalog::read(const DatasetRequest& request,UiLanguage ui) const {
    DatasetView result{};readInto(request,ui,result);return result;
}
void DatasetCatalog::readInto(const DatasetRequest& request,UiLanguage ui,DatasetView& view) const {
    view={};view.purpose=request.purpose;
    try {
        if(request.action==DatasetAction::Open || request.action==DatasetAction::Older || request.action==DatasetAction::Newer)
            readListInto(request,ui,view);
        else readDetailInto(request,ui,view);
    } catch(const MapChunkLimitExceeded& error) {view.error=DatasetError::TooManyChunks;view.info.chunks=error.count;}
    catch(const std::bad_alloc&) {view.error=DatasetError::NoMemory;}
    catch(const std::exception&) {view.error=DatasetError::Data;}
    return;
}

void DatasetCatalog::readListInto(const DatasetRequest& request,UiLanguage ui,DatasetView& view) const {
    const bool reverse=request.action==DatasetAction::Older;
    if(!request.id.valid(request.action==DatasetAction::Open ||
                         (request.action==DatasetAction::Newer && request.purpose==DatasetPurpose::Select))) {
        view.error=DatasetError::Missing;return;
    }
    if(reverse && !request.page) {view.error=DatasetError::Missing;return;}
    view.page=request.action==DatasetAction::Open?request.page:reverse?request.page-1:request.page+1;
    DatasetId minimum{},maximum{};bool have_any=false;
    auto consider=[&](const DatasetId& id) {
        if(!have_any) {minimum=maximum=id;have_any=true;}
        else {if(std::strcmp(id.c_str(),minimum.c_str())<0)minimum=id;if(std::strcmp(id.c_str(),maximum.c_str())>0)maximum=id;}
        const int against=std::strcmp(id.c_str(),request.id.c_str());
        if(request.action==DatasetAction::Open && against<0)return;
        if(request.action==DatasetAction::Newer && against<=0)return;
        if(reverse && against>=0)return;
        unsigned position=0;
        while(position<view.count && std::strcmp(view.rows[position].id.c_str(),id.c_str())<0)++position;
        const bool full=view.count==config::DatasetPageRows;
        if(full && ((!reverse && position==view.count) || (reverse && !position)))return;
        if(!full)++view.count;
        else if(reverse) {
            for(unsigned i=1;i<view.count;++i)view.rows[i-1]=view.rows[i];
            --position;
        }
        for(unsigned i=view.count-1;i>position;--i)view.rows[i]=view.rows[i-1];
        view.rows[position]={};view.rows[position].id=id;
    };
    if(request.purpose==DatasetPurpose::Select)consider(DatasetId{});
    {
        SdAccessScope access(observer_);
        errno=0;DIR* directory_handle=opendir(TemporaryPath(root_).c_str());
        if(!directory_handle) {if(errno!=ENOENT)view.error=DatasetError::Data;return;}
        int error=0;
        for(;;) {
            errno=0;auto* entry=readdir(directory_handle);
            if(!entry){error=errno;break;}
            const size_t size=strnlen(entry->d_name,DatasetId::Capacity);
            if(size>=DatasetId::Capacity || !DatasetId::valid({entry->d_name,size}))continue;
            DatasetId id{};id.assign({entry->d_name,size});
            struct stat status{};
            if(stat(directory(id.view()).c_str(),&status) || !S_ISDIR(status.st_mode))continue;
            consider(id);
        }
        closedir(directory_handle);
        if(error){view.error=DatasetError::Data;return;}
    }
    if(view.count) {
        view.has_older=std::strcmp(minimum.c_str(),view.rows[0].id.c_str())<0;
        view.has_newer=std::strcmp(maximum.c_str(),view.rows[view.count-1].id.c_str())>0;
        view.selected=reverse?view.count-1:0;
    }
    for(unsigned i=0;i<view.count;++i) {
        auto& row=view.rows[i];if(!row.id)continue;
        DatasetMetadata metadata;
        readDatasetMetadataInto(directory(row.id.view()).view(),ui,"",metadata,observer_);
        row.error=metadata.error;
    }
    return;
}

void DatasetCatalog::readDetailInto(const DatasetRequest& request,UiLanguage ui,DatasetView& view) const {
    view.detail=true;view.info={};view.id=request.id;
    if(!request.id.valid()) {view.error=DatasetError::Missing;return;}
    DatasetMetadata metadata;
    DatasetLanguages wordbook_types;
    readDatasetMetadataInto(directory(request.id.view()).view(),ui,request.language,metadata,observer_,
        {view.info.title,sizeof(view.info.title),view.info.description,sizeof(view.info.description)},&wordbook_types);
    if(metadata.error!=DatasetError::None) {view.error=metadata.error;return;}
    datasetCopyText(view.info.language,sizeof(view.info.language),metadata.language);
    view.info.language_count=metadata.language_count;view.info.language_index=metadata.language_index;
    for(unsigned i=0;i<wordbook_types.count;++i)
        std::memcpy(view.info.wordbook_types[i],wordbook_types.ids[i].data(),config::DatasetLanguageBytes);
    const auto map_path=TemporaryPath({directory(request.id.view()).view(),"/",metadata.map});
    {
        SdAccessScope access(observer_); struct stat st{};
        if (stat(map_path.c_str(),&st) || !S_ISREG(st.st_mode) ||
            stat(TemporaryPath({directory(request.id.view()).view(),"/",metadata.wordbook}).c_str(),&st) || !S_ISREG(st.st_mode)) {
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
    reader.inspectWordBook(TemporaryPath({directory(request.id.view()).view(),"/",metadata.wordbook}).view());
}

} // namespace geoputer

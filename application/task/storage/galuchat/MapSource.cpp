#include "MapSource.h"
#include "../../../map/DecimatedViewport.h"
#include "../../../utils/HeapDiagnostics.h"
#include "DiskIndexedMapReader.h"
#include "DiskIndexedWordBookReader.h"
#include "ObservedFileReader.h"
#include "TextEncoding.h"
#include "../dataset/DatasetMetadata.h"
#include "../../../Config.h"
#include <stdexcept>

namespace geoputer {
MapSourceResult readBoundaryMap(const std::string& directory,const std::string& language,
                                const map::Request& request,uint8_t drive,StorageObserver* observer) {
    MapSourceResult result;result.info.request=request;
    try {
        const auto metadata=readDatasetMetadata(directory,UiLanguage::Japanese,language.c_str(),observer);
        if(metadata.error!=DatasetError::None) {result.info.error=map::Error::ReadError;return result;}
        const auto path=directory+"/_mapindex.dat",map_path=directory+"/"+metadata.map;
        DatasetWordBookIndex book;
        DatasetIndex index(drive,observer);
        const auto info=index.loadMap(path,map_path,nullptr,nullptr,&book,metadata.language);
        auto index_factory=std::make_shared<ObservedFileReaderFactory>(path,observer,nullptr,FileReadOptions{config::IndexReadBufferBytes,0},drive);
        {
            DiskIndexedMapReader reader(info,std::make_shared<ObservedFileReaderFactory>(map_path,observer,nullptr,
                FileReadOptions{config::MapRectReadBufferBytes,config::FileReadThroughSkipBytes},drive),index_factory);
            map::DecimatedViewport viewport;
            result.info.error=map::planDecimatedViewport(request.latitude,request.longitude,reader.unitInvX(),reader.unitInvY(),
                config::MapViewCodeMaxBytes,request.scale,viewport);
            if(result.info.error!=map::Error::None)return result;
            result.plan=viewport.output;
            const auto& bounds=result.plan.source;
            result.pixels.reset(new map::RegionCode[size_t(bounds.width)*bounds.height]);
            reportMapHeap("source-allocated",request.token,result.view().count*sizeof(map::RegionCode));
            const auto& input=viewport.input;
            reader.readRectDecimated({input.x,input.y,input.width,input.height},request.scale,result.pixels.get());
            reportMapHeap("source-read",request.token,result.view().count*sizeof(map::RegionCode));
        }
        reportMapHeap("map-reader-closed",request.token,result.view().count*sizeof(map::RegionCode));
        result.info.width=result.plan.width;result.info.height=result.plan.height;
        result.info.meters_per_pixel=result.plan.meters_per_pixel;
        result.info.region=result.view().get(result.plan.center_x,result.plan.center_y);
        if(result.info.region) {
            const auto book_path=directory+"/"+metadata.wordbook;
            if(!wordBookUsesUtf8(index.checkWordBook(book,book_path)))throw std::runtime_error("unsupported map wordbook encoding");
            DiskIndexedWordBookReader reader(book,std::make_shared<ObservedFileReaderFactory>(book_path,observer,nullptr,
                FileReadOptions{config::WordBookReadBufferBytes,config::FileReadThroughSkipBytes},drive),index_factory);
            std::vector<std::string> names;
            if(!reader.readStringSetByCode(result.info.region,names))throw std::runtime_error("invalid map region");
            std::string name;
            if(names.size()>2)for(size_t i=2;i<names.size();++i)name+=names[i];
            else if(!names.empty())name=names.back();
            if(!validUtf8(name))throw std::runtime_error("invalid map name");
            datasetCopyText(result.info.name,sizeof(result.info.name),name);
            reportMapHeap("wordbook-read",request.token,result.view().count*sizeof(map::RegionCode));
        }
    }catch(const std::bad_alloc&) {result.info.error=map::Error::NoMemory;result.pixels.reset();}
    catch(const std::exception&) {result.info.error=map::Error::ReadError;result.pixels.reset();}
    reportMapHeap("source-ready",request.token,result.pixels?result.view().count*sizeof(map::RegionCode):0);
    return result;
}
} // namespace geoputer

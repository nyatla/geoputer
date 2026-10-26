#include "MapSource.h"
#include "../../../map/DecimatedViewport.h"
#include "../../../utils/HeapDiagnostics.h"
#include "DiskIndexedMapReader.h"
#include "ObservedFileReader.h"
#include "../dataset/DatasetMetadata.h"
#include "../../../Config.h"
#include <stdexcept>

namespace geoputer {
MapSourceResult readBoundaryMap(const std::string& directory,const std::string& language,
                                const map::Request& request,uint8_t drive,StorageObserver* observer,MapSourceResult reusable,BorrowedReadBuffer* index_buffer) {
    MapSourceResult result=std::move(reusable);result.invalidate(request);
    try {
        const auto metadata=readDatasetMetadata(directory,UiLanguage::Japanese,language.c_str(),observer);
        if(metadata.error!=DatasetError::None) {result.info.error=map::Error::ReadError;return result;}
        const auto path=directory+"/_mapindex.dat",map_path=directory+"/"+metadata.map;
        DatasetIndex index(drive,observer);
        const auto info=index.loadMap(path,map_path);
        auto index_factory=std::make_shared<ObservedFileReaderFactory>(path,observer,nullptr,FileReadOptions{config::IndexReadBufferBytes,0},drive,index_buffer);
        {
            DiskIndexedMapReader reader(info,std::make_shared<ObservedFileReaderFactory>(map_path,observer,nullptr,
                FileReadOptions{config::MapRectReadBufferBytes,config::FileReadThroughSkipBytes},drive),index_factory);
            map::DecimatedViewport viewport;
            result.info.error=map::planDecimatedViewport(request.latitude,request.longitude,reader.unitInvX(),reader.unitInvY(),
                config::MapViewCodeMaxBytes,request.scale,viewport);
            if(result.info.error!=map::Error::None)return result;
            result.plan=viewport.output;
            const auto& bounds=result.plan.source;
            const bool reused=result.pixels && size_t(bounds.width)*bounds.height<=result.capacity;
            result.reserve(size_t(bounds.width)*bounds.height);
            reportMapHeap(reused?"source-reused":"source-allocated",request.token,result.view().count*sizeof(map::RegionCode));
            const auto& input=viewport.input;
            reader.readRectDecimated({input.x,input.y,input.width,input.height},request.scale,result.pixels.get());
            reportMapHeap("source-read",request.token,result.view().count*sizeof(map::RegionCode));
        }
        reportMapHeap("map-reader-closed",request.token,result.view().count*sizeof(map::RegionCode));
        result.info.width=result.plan.width;result.info.height=result.plan.height;
        result.info.meters_per_pixel=result.plan.meters_per_pixel;
        result.info.region=request.region_known?request.region:result.view().get(result.plan.center_x,result.plan.center_y);
        result.valid=true;
    }catch(const std::bad_alloc&) {result.info.error=map::Error::NoMemory;result.pixels.reset();result.capacity=0;}
    catch(const std::exception&) {result.info.error=map::Error::ReadError;result.pixels.reset();result.capacity=0;}
    reportMapHeap("source-ready",request.token,result.pixels?result.view().count*sizeof(map::RegionCode):0);
    return result;
}
} // namespace geoputer

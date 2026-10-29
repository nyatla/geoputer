#include "MapSource.h"
#include "../../../map/DecimatedViewport.h"
#include "../../../utils/HeapDiagnostics.h"
#include "DiskIndexedMapReader.h"
#include "ObservedFileReader.h"
#include "../../../Config.h"
#include <stdexcept>

namespace geoputer {
MapSourceResult readBoundaryMap(const SelectedDataset& selected,
                                const map::Request& request,uint8_t drive,StorageObserver* observer,MapSourceResult reusable,BorrowedReadBuffer* work_buffer,ReadFileSlot* file_slot,std::string_view root) {
    MapSourceResult result=std::move(reusable);result.invalidate(request);
    try {
        if(!selected.ready()) {result.info.error=map::Error::NoDataset;return result;}
        if(request.dataset_generation!=selected.info.generation) {result.info.error=map::Error::Stale;return result;}
        FileReadSource index_source(selected.file(DatasetFile::Index,root),observer,nullptr,
            FileReadOptions{config::IndexReadBufferBytes,0},drive,file_slot);
        {
            FileReadSource map_source(selected.file(DatasetFile::Map,root),observer,nullptr,
                FileReadOptions{config::MapRectReadBufferBytes,config::FileReadThroughSkipBytes},drive,file_slot);
            DiskIndexedMapReader reader(selected.map_index,map_source,index_source);
            map::DecimatedViewport viewport;
            result.info.error=map::planDecimatedViewport(request.latitude,request.longitude,reader.unitInvX(),reader.unitInvY(),
                config::MapViewCodeMaxBytes,request.scale,viewport);
            if(result.info.error!=map::Error::None)return result;
            result.plan=viewport.output;
            const auto& bounds=result.plan.source;
            result.reserve(size_t(bounds.width)*bounds.height);
            reportMapHeap("source-fixed",request.token,result.view().count*sizeof(map::RegionCode));
            const auto& input=viewport.input;
            reader.readRectDecimated({input.x,input.y,input.width,input.height},request.scale,result.pixels,work_buffer);
            reportMapHeap("source-read",request.token,result.view().count*sizeof(map::RegionCode));
        }
        reportMapHeap("map-reader-closed",request.token,result.view().count*sizeof(map::RegionCode));
        result.info.width=result.plan.width;result.info.height=result.plan.height;
        result.info.meters_per_pixel=result.plan.meters_per_pixel;
        result.info.region=request.region_known?request.region:result.view().get(result.plan.center_x,result.plan.center_y);
        result.valid=true;
    }catch(const std::bad_alloc&) {result.info.error=map::Error::NoMemory;result.valid=false;result.plan={};}
    catch(const std::exception&) {result.info.error=map::Error::ReadError;result.valid=false;result.plan={};}
    reportMapHeap("source-ready",request.token,result.pixels?result.view().count*sizeof(map::RegionCode):0);
    return result;
}
} // namespace geoputer

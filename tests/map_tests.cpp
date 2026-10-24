#include "map/BoundaryExtractor.h"
#include "map/DecimatedViewport.h"
#include "map/MapFrameReceiver.h"
#include "task/storage/MapBuildJob.h"
#include "task/ui/UiModel.h"
#include "task/ui/KeyBindings.h"
#include "task/ui/ScreenNavigation.h"
#include <stdexcept>
#include <vector>
#include <cstring>
#include <limits>

using namespace geoputer;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
// Compare with coordinate-based sampling, including cropped source buffers,
// repeated/skipped source rows, partial updates, and the last row/column.
void testBoundarySampling() {
    for (double latitude : {0.0, 35.68, -35.68, 70.0})
    for (int unit_y : {10000, 20000})
    for (int width : {1, 5, 240})
    for (int height : {1, 3, 101}) {
        map::ViewportPlan plan;
        check(map::planViewport(latitude, -140.0, 10000, unit_y, SIZE_MAX, plan, width, height) == map::Error::None,
              "boundary sampling test plan succeeds");
        auto bounds = plan.source;
        bounds.x -= 2; bounds.y -= 2; bounds.width += 4; bounds.height += 4;
        std::vector<uint32_t> pixels(size_t(bounds.width) * bounds.height);
        uint32_t random = 12345;
        for (auto& pixel : pixels) {
            random = random * 1664525u + 1013904223u;
            pixel = (random >> 24) % 4; // Include zero and multiple region codes.
        }
        const map::RasterView view{pixels.data(), bounds, pixels.size()};
        for (map::RegionCode current : {0u, 1u, 9u}) {
            map::BoundaryImage expected, actual;
            check(expected.resize(width,height) && actual.resize(width,height), "sampling images allocated");
            for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
                const auto a=view.get(plan.sampleX(x),plan.sampleY(y));
                const auto b=x+1<width?view.get(plan.sampleX(x+1),plan.sampleY(y)):a;
                const auto c=y+1<height?view.get(plan.sampleX(x),plan.sampleY(y+1)):a;
                expected.set(x,y,a!=b || a!=c ? current && (a==current || b==current || c==current)?2:1 : 0);
                actual.set(x,y,3); // Each requested row must overwrite its old pixels.
            }
            unsigned first=0;
            for (unsigned count : {0u,1u,2u,8u,90u}) {
                count=std::min(count,unsigned(height)-first);
                check(map::buildBoundaryRows(view,plan,current,first,count,actual)==map::Error::None,
                      "row sampling accepts partial updates");
                first+=count;
                for (int y=0;y<height;++y) for (int x=0;x<width;++x)
                    check(actual.get(x,y)==(unsigned(y)<first?expected.get(x,y):3),
                          "row sampling preserves the reference pixels and untouched rows");
            }
            check(first==unsigned(height) && !std::memcmp(actual.data(),expected.data(),actual.bytes()),
                  "packed boundary bytes match coordinate sampling including padding");
            check(map::buildBoundaryRows(view,plan,current,height,0,actual)==map::Error::None,
                  "empty update after final row is accepted");
        }
    }
}
void testDecimatedViewport() {
    for(double latitude:{-70.0,-35.68,0.0,35.68,70.0})for(int uy:{10000,20000})
    for(int width:{1,5,240})for(int height:{1,3,101})for(unsigned scale:{1u,2u,4u}) {
        map::DecimatedViewport p;
        check(map::planDecimatedViewport(latitude,-140,10000,uy,SIZE_MAX,scale,p,width,height)==map::Error::None,
              "sampled viewport accepts supported scales and dimensions");
        const auto& v=p.output;
        check(p.input.width==(v.source.width-1)*int(scale)+1 && p.input.height==(v.source.height-1)*int(scale)+1,
              "input includes exactly the selected sample lattice");
        const int center_column=v.center_x-v.source.x,center_row=v.center_y-v.source.y;
        check(p.input.x+center_column*int(scale)==v.center_x && p.input.y+center_row*int(scale)==v.center_y,
              "decimation includes the exact current-location source cell");
        check(v.sampleX(width/2)==v.center_x && v.sampleY(height/2)==v.center_y,
              "current-location marker remains at screen center");
        map::ViewportPlan normal;
        map::planViewport(latitude,-140,10000,uy,SIZE_MAX,normal,width,height);
        check(v.source.x==normal.source.x && v.source.y==normal.source.y &&
              v.source.width==normal.source.width && v.source.height==normal.source.height &&
              v.meters_per_pixel==normal.meters_per_pixel*scale,"output storage stays constant while scale bar grows");
        const auto bytes=size_t(v.source.width)*v.source.height*4;
        check(map::planDecimatedViewport(latitude,-140,10000,uy,bytes-1,scale,p,width,height)==map::Error::TooLarge,
              "sampled viewport enforces the stored-image memory budget");
    }
    map::DecimatedViewport p;
    check(map::planDecimatedViewport(0,0,10000,10000,SIZE_MAX,3,p)==map::Error::InvalidInput,
          "unsupported viewport decimation rejected");
    check(map::planDecimatedViewport(0,180,11930463,10000,SIZE_MAX,4,p)==map::Error::TooLarge,
          "expanded source coordinate overflow rejected");

}
void testMapScaleKeys() {
    UiData data{};data.settings=defaultSettings();data.dataset.generation=1;
    data.recording.state=RecordingState::Recording;
    UiModel model;model.setScreen({ScreenId::Map,DisplayMode::View,7,data,0,ActionResult::None});
    model.keyPressed(UiKey::Up,1);
    check(model.snapshot().action.kind==ActionKind::None,"map scale must stop at native resolution");
    model.keyPressed(mapPhysicalKey('.',false),2);
    const auto first=model.snapshot().action;
    check(first.kind==ActionKind::MapScale && first.map_scale==2 && first.screen_generation==7,
          "plain Down requests half-resolution through App");
    check(model.data().map.scale==1,"UI must not commit a scale before App acknowledgement");
    model.keyPressed(UiKey::Down,3);
    check(model.snapshot().action.action_id==first.action_id && model.snapshot().action.map_scale==2,
          "pending scale action is retained without duplicate or overwrite");
    data.revision=1;model.updateContent({DisplayMode::View,7,data,first.action_id,ActionResult::Rejected});
    check(model.data().map.scale==1 && model.snapshot().action.kind==ActionKind::None,"rejected zoom preserves the selected scale");
    for(unsigned target:{2u,4u,2u,1u}) {
        const bool up=target<model.data().map.scale;
        model.keyPressed(mapPhysicalKey(up?';':'.',true),4+target);
        const auto action=model.snapshot().action;
        check(action.kind==ActionKind::MapScale && action.map_scale==target,"map zoom order differs for Fn/alias keys");
        data.map.scale=target;data.map.busy=true;++data.revision;
        model.updateContent({DisplayMode::View,7,data,action.action_id,ActionResult::Applied});
        check(model.data().map.scale==target && model.data().recording.state==RecordingState::Recording,
              "zoom acknowledgement changes scale without changing GPS recording");
    }
    data.map.scale=4;++data.revision;model.updateContent({DisplayMode::View,7,data,0,ActionResult::None});
    model.keyPressed(UiKey::Down,20);model.keyPressed(UiKey::Left,21);model.keyPressed(UiKey::Right,22);
    check(model.snapshot().action.kind==ActionKind::None,"map scale must stop at quarter resolution and ignore Left/Right");
    model.setScreen({ScreenId::Map,DisplayMode::Background,8,data,0,ActionResult::None});model.keyPressed(UiKey::Up,23);
    check(model.snapshot().action.kind==ActionKind::Wake && model.data().map.scale==4,"first key while dark only wakes the display");
}
MapSourceResult source(const map::Request& request) {
    MapSourceResult result;result.info.request=request;
    check(map::planViewport(35.681641,140.027194,10000,10000,96*1024,result.plan)==map::Error::None,"map plan succeeds within independent memory budget");
    result.info.width=result.plan.width;result.info.height=result.plan.height;result.info.meters_per_pixel=result.plan.meters_per_pixel;
    result.info.region=11;std::strcpy(result.info.name,"Test");
    const auto& b=result.plan.source;result.pixels.reset(new uint32_t[size_t(b.width)*b.height]);
    for(int y=0;y<b.height;++y)for(int x=0;x<b.width;++x)result.pixels[size_t(y)*b.width+x]=x<120?11:22;
    return result;
}
}
void testMaps() {
    testBoundarySampling();testDecimatedViewport();testMapScaleKeys();
    map::BoundaryImage image;check(!image.resize(0,1) && !image.resize(241,101),"image rejects zero/oversized dimensions");
    check(image.resize(5,3) && image.bytes()==6,"odd image widths have padded rows");
    image.set(0,0,1);image.set(1,0,2);image.set(4,0,2);
    check(image.data()[0]==0x60 && image.data()[1]==0x80 && image.get(4,0)==2,"image packing is stable and high-bit first");
    map::ViewportPlan invalid;
    check(map::planViewport(std::numeric_limits<double>::quiet_NaN(),0,10000,10000,100000,invalid)==map::Error::InvalidInput,"NaN rejected");
    check(map::planViewport(90,0,10000,10000,100000,invalid)==map::Error::InvalidInput,"unsupported pole rejected");
    check(map::planViewport(35,140,10000,10000,1,invalid)==map::Error::TooLarge,"independent work memory limit enforced");
    map::Request request{};request.token=9;request.screen_generation=7;request.dataset_generation=1;
    auto input=source(request);const auto& plan=input.plan;
    check(plan.sampleX(120)==plan.center_x && plan.sampleY(50)==plan.center_y,"marker matches the plan center");
    check(plan.sampleY(0)>plan.sampleY(100) && plan.source.height<101,"north-up sampling corrects local latitude aspect");
    map::BoundaryImage all,split;all.resize(240,101);split.resize(240,101);
    check(map::buildBoundaryRows(input.view(),plan,11,0,101,all)==map::Error::None,"whole boundary image generated");
    for(unsigned row=0;row<101;row+=8)check(map::buildBoundaryRows(input.view(),plan,11,row,std::min(8u,101-row),split)==map::Error::None,"split conversion succeeds");
    check(!std::memcmp(all.data(),split.data(),all.bytes()),"split rows preserve every boundary pixel");
    check(all.get(119,10)==2 && all.get(118,10)==0 && all.get(239,10)==0,"current boundary is highlighted without artificial outer border");
    map::BoundaryImage other;other.resize(240,101);map::buildBoundaryRows(input.view(),plan,0,0,101,other);
    check(other.get(119,10)==1,"zero current region never highlights boundaries");
    auto short_view=input.view();--short_view.count;
    check(map::buildBoundaryRows(short_view,plan,11,0,8,split)==map::Error::InvalidInput,"short source capacity is rejected");
    MapBuildJob job;check(job.start(request) && !job.start(request),"only one accepted map result is retained");
    job.loaded(std::move(input));for(int n=0;n<13;++n)job.step();
    check(job.complete() && job.notificationPending(),"asynchronous map result ready after bounded render steps");
    job.notificationSent();check(!job.notificationPending(),"completion notification does not repeat after delivery");
    map::MapFrameReceiver receiver;check(receiver.begin(job.info()),"UI back buffer accepted");
    map::Transfer transfer;auto wrong=request;++wrong.token;
    check(!job.read(wrong,transfer) && !job.release(wrong),"another token cannot read or release the result");
    check(!receiver.commit(job.info()) && !receiver.image().bytes(),"incomplete image is never displayed");
    for(unsigned row=0;row<101;row+=8) {
        request.row=row;check(job.read(request,transfer),"completed rows copied through transfer payload");
        auto bad=transfer;++bad.row;check(!receiver.write(bad),"out-of-order rows rejected");
        check(receiver.write(transfer),"ordered rows accepted");
    }
    check(receiver.commit(job.info()) && receiver.image().bytes()==6060,"complete image swapped atomically");
    check(receiver.begin(job.info()) && !receiver.commit(job.info()) && receiver.image().get(119,10)==2,"updating retains the previous complete frame");
    request.row=0;job.read(request,transfer);transfer.pixels[0]=0xc0;check(!receiver.write(transfer),"reserved pixel value rejected");
    check(job.release(request) && !job.occupied() && !job.read(request,transfer),"release invalidates the completed source frame");
    check(job.start(request),"a new map can start after release");job.fail(map::Error::NoMemory);
    check(job.complete() && job.info().error==map::Error::NoMemory && job.notificationPending(),"allocation failure becomes a retained async result");job.release(request);
    check(job.start(request) && job.release(request) && !job.notificationPending(),"cancel frees an accepted unfinished job");

    UiData data{};data.settings=defaultSettings();data.settings.dataset=1;data.storage.card_state=CardState::Mounted;
    data.gps.fix.valid=true;data.gps.reception=GpsReception::Fix;data.recording.state=RecordingState::Recording;
    const ScreenId roots[]{ScreenId::LivePlace,ScreenId::GpsLogger,ScreenId::Logs,ScreenId::Settings,ScreenId::Map};
    for(auto from:roots)for(bool fn:{false,true}) {
        UiModel model;model.setScreen({from,DisplayMode::View,7,data,0,ActionResult::None});
        model.keyPressed(mapPhysicalKey('4',fn),1);
        const auto action=model.snapshot().action;
        check(from==ScreenId::Map?action.kind==ActionKind::None:action.kind==ActionKind::ChangeScreen && action.target==ScreenId::Map,"4 opens Map from every root with or without Fn");
        check(model.data().recording.state==RecordingState::Recording,"opening Map never changes logging");
    }
    UiModel child;child.setScreen({ScreenId::Settings,DisplayMode::View,7,data,0,ActionResult::None});child.keyPressed(UiKey::Enter,1);child.keyPressed(UiKey::OpenMap,2);
    check(child.snapshot().action.kind==ActionKind::None,"settings children ignore map shortcut");
    UiModel modal;modal.setScreen({ScreenId::LivePlace,DisplayMode::View,7,data,0,ActionResult::None});modal.keyPressed(UiKey::PauseLog,1);modal.keyPressed(UiKey::OpenMap,2);
    check(modal.confirming() && modal.snapshot().action.kind==ActionKind::None,"map cannot bypass a confirmation");
    UiModel model;model.setScreen({ScreenId::Map,DisplayMode::View,7,data,0,ActionResult::None});
    job.start(request);job.loaded(source(request));for(int n=0;n<13;++n)job.step();
    transfer={};transfer.info=job.info();check(model.beginMapFrame(transfer)==OperationStatus::Ok,"model accepts matching map frame");
    for(unsigned row=0;row<101;row+=8){request.row=row;job.read(request,transfer);check(model.writeMapRows(transfer)==OperationStatus::Ok,"model copies map rows");}
    check(model.commitMapFrame(transfer)==OperationStatus::Ok && model.mapFrame().image().bytes()==6060,"model commits complete independent frame");
    data.map.scale=2;++data.revision;model.updateContent({DisplayMode::View,7,data,0,ActionResult::None});
    check(model.beginMapFrame(transfer)==OperationStatus::StaleContent &&
          model.writeMapRows(transfer)==OperationStatus::StaleContent && model.commitMapFrame(transfer)==OperationStatus::StaleContent &&
          model.mapFrame().image().bytes()==6060 && model.mapFrame().info().request.scale==1,
          "scale switch must reject late old-scale images without erasing the shown frame");
    data.map.scale=1;++data.revision;model.updateContent({DisplayMode::View,7,data,0,ActionResult::None});
    auto stale=transfer;++stale.info.request.dataset_generation;
    check(model.beginMapFrame(stale)==OperationStatus::StaleContent,"old dataset result cannot overwrite Map");
    model.setScreen({ScreenId::Logs,DisplayMode::View,8,data,0,ActionResult::None});
    check(!model.mapFrame().image().bytes() && model.writeMapRows(transfer)==OperationStatus::StaleContent,"screen exit frees UI buffers and rejects delayed rows");
}

#include "map/BoundaryExtractor.h"
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
    auto stale=transfer;++stale.info.request.dataset_generation;
    check(model.beginMapFrame(stale)==OperationStatus::StaleContent,"old dataset result cannot overwrite Map");
    model.setScreen({ScreenId::Logs,DisplayMode::View,8,data,0,ActionResult::None});
    check(!model.mapFrame().image().bytes() && model.writeMapRows(transfer)==OperationStatus::StaleContent,"screen exit frees UI buffers and rejects delayed rows");
}

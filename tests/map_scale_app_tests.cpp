// Compile the production MapView.cpp; hardware and synchronous peers are adapters.
#include "Application.h"
#include "utils/Console.h"
#include <vector>
#include <stdexcept>
#include <cstring>
#include <cstdio>
using namespace geoputer;
namespace {
MapBuildJob backend;
std::vector<MessageKind> commands;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
MapSourceResult source(const map::Request& request) {
    MapSourceResult out;out.info.request=request;
    check(map::planViewport(request.latitude,request.longitude,10000,10000,96*1024,out.plan)==map::Error::None,"fixture viewport");
    out.info.width=240;out.info.height=101;out.info.region=11;std::strcpy(out.info.name,"Test");
    out.info.meters_per_pixel=out.plan.meters_per_pixel*request.scale;
    out.pixels.reset(new uint32_t[size_t(out.plan.source.width)*out.plan.source.height]);
    std::fill_n(out.pixels.get(),out.view().count,11u);return out;
}
void acknowledgeScale(Application& app) {
    auto& ui=app.ui_.model_;
    check(ui.updateContent({DisplayMode::View,app.generation_,app.data_,0,ActionResult::None})==OperationStatus::Ok,"publish selected scale");
}
void finish(Application& app) {
    backend.loaded(source(backend.info().request));while(!backend.complete())backend.step();app.completeMap(backend.info());
}
void deliver(Application& app) {
    for(unsigned i=0;i<20 && app.map_request_.token;++i)app.processMap();
    check(!app.map_request_.token && !backend.occupied(),"finished map was not released");
}
}
namespace geoputer {
USBCDC Console;
void UiTask::run(){} void GpsTask::run(){} void StorageTask::run(){}
void StorageTask::sdBegin(){} void StorageTask::sdEnd(){} void StorageTask::cacheState(RgCacheState,bool){}
bool Application::callUi(UiRequest& request,SyncReturn& result) {
    result.status=OperationStatus::InvalidRequest;
    switch(request.kind) {
        case MessageKind::BeginMapFrame:result.status=ui_.model_.beginMapFrame(request.payload.map);break;
        case MessageKind::WriteMapRows:result.status=ui_.model_.writeMapRows(request.payload.map);break;
        case MessageKind::CommitMapFrame:result.status=ui_.model_.commitMapFrame(request.payload.map);break;
        default:break;
    }
    return true;
}
bool Application::awaitSyncResult(SyncSendResult sent,SyncReturn& result) {
    check(sent==SyncSendResult::AwaitingResult,"synchronous map request not sent");
    StorageRequest request{};check(storage_.inbox().receive(request),"map request missing from peer queue");
    commands.push_back(request.kind);ReturnMessage reply{};
    prepareSyncReturn(reply,TaskId::Storage,request,1);auto& status=reply.payload.result.status;
    switch(request.kind) {
        case MessageKind::RequestMap:status=backend.start(request.payload.map)?OperationStatus::Ok:OperationStatus::Unavailable;break;
        case MessageKind::CancelMap:case MessageKind::ReleaseMap:status=backend.release(request.payload.map)?OperationStatus::Ok:OperationStatus::InvalidRequest;break;
        case MessageKind::ReadMapRows:status=backend.read(request.payload.map,reply.payload.result.map)?OperationStatus::Ok:OperationStatus::InvalidRequest;break;
        default:break;
    }
    check(results_.send(reply),"map peer result queue full");
    check(sync_.receiveSyncResult(result,0)==SyncReceiveResult::Returned,"map result not consumed");return true;
}
}
int main() {
    Application app;check(app.results_.initialize() && app.storage_.initialize(),"initialize host queues");
    app.screen_=ScreenId::Map;app.generation_=7;app.data_.dataset.generation=1;app.data_.settings.dataset=1;
    app.data_.storage.card_state=CardState::Mounted;app.data_.gps.reception=GpsReception::Fix;
    app.data_.gps.fix={35.68,140.02,1000,true};app.data_.gps.sample_seq=1;
    app.ui_.model_.setScreen({ScreenId::Map,DisplayMode::View,7,app.data_,0,ActionResult::None});
    app.processMap();check(app.map_request_.scale==1,"initial map scale");finish(app);deliver(app);
    const auto original=app.ui_.model_.mapFrame().info().request.token;
    check(app.changeMapScale(2),"select half scale");acknowledgeScale(app);app.processMap();
    check(app.map_request_.scale==2 && backend.reading(),"zoom must request immediately with unchanged GPS sample and no update-period delay");
    check(app.changeMapScale(4),"select quarter while half is reading");acknowledgeScale(app);
    finish(app);check(!app.map_ready_,"completion of the old scale was accepted");
    const auto before=commands.size();app.processMap();
    check(commands.size()==before+2 && commands[before]==MessageKind::CancelMap && commands[before+1]==MessageKind::RequestMap &&
          app.map_request_.scale==4 && app.data_.map.scale==4,"cancel old job before starting the latest scale");
    finish(app);app.processMap();app.processMap();
    check(app.map_begun_ && app.map_row_>0,"quarter-scale transfer did not start");
    check(app.ui_.model_.mapFrame().info().request.token==original,"incomplete zoom replaced the visible old frame");
    check(app.changeMapScale(2),"change scale during row transfer");acknowledgeScale(app);app.processMap();finish(app);deliver(app);
    check(app.ui_.model_.mapFrame().info().request.scale==2 && app.data_.map.scale==2 && !app.data_.map.busy,
          "latest scale must commit atomically and survive busy-state updates");
    app.processMap();check(!app.map_request_.token,"unchanged GPS immediately restarted a completed map");
    const auto revision=app.data_.revision;
    check(app.changeMapScale(2) && app.data_.revision==revision,"same scale triggers redundant regeneration");
    for(uint8_t scale:{0,3,8})check(!app.changeMapScale(scale) && app.data_.map.scale==2,"invalid scale accepted");
    app.data_.gps.fix.valid=false;check(app.changeMapScale(4),"zoom unavailable map");acknowledgeScale(app);app.processMap();
    check(!app.map_request_.token && app.data_.map.error==map::Error::NoFix && app.data_.map.scale==4,"zoom without fix lost selection or issued a read");
    app.data_.gps.fix.valid=true;app.processMap();check(app.map_request_.scale==4,"fix recovery lost chosen scale");finish(app);deliver(app);
    app.screen_=ScreenId::LivePlace;check(!app.changeMapScale(1),"other screens can change map scale");
    app.screen_=ScreenId::Map;app.mode_=DisplayMode::Background;check(!app.changeMapScale(1),"background changes map scale");
    std::puts("PASS: real App zoom cancellation, stale completions, mid-transfer changes, unchanged GPS, no-fix recovery and atomic frame commit");
}

// Exercise production App dispatch and logging against queued GPS/UI and a real LogWriter.
#include "Application.h"
#include "utils/Console.h"
#include <stdexcept>
#include <cstring>
#include <cstdio>
#include <vector>
#include <filesystem>
using namespace geoputer;
namespace {
Application* app=nullptr;
std::vector<MessageKind> calls;
bool fail_open=false,hold_place_queries=false;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void route(QueueHandle_t queue) {
    ReturnMessage reply{};
    if(queue==app->gps_.inbox().handle_) {
        GpsRequest command{};app->gps_.inbox().receive(command);calls.push_back(command.kind);
        app->gps_.handle(command);app->gps_.sendReturn();return;
    }
    if(queue==app->ui_.inbox().handle_) {
        UiRequest command{};app->ui_.inbox().receive(command);
        prepareSyncReturn(reply,TaskId::UI,command,1);auto& result=reply.payload.result;
        if(command.kind==MessageKind::GetUiContent){result.content=app->ui_.model_.snapshot();result.status=OperationStatus::Ok;}
        else if(command.kind==MessageKind::UpdateUiContent)result.status=app->ui_.model_.updateContent(command.payload.update);
        else if(command.kind==MessageKind::SetUiScreen)result.status=app->ui_.model_.setScreen(command.payload.screen);
        else throw std::runtime_error("unexpected UI request");
        check(app->results_.send(reply),"UI return queue full");return;
    }
    if(queue==app->storage_.inbox().handle_) {
        if(hold_place_queries)return;
        StorageRequest command{};app->storage_.inbox().receive(command);calls.push_back(command.kind);
        prepareSyncReturn(reply,TaskId::Storage,command,1);auto& result=reply.payload.result;
        const auto& log=command.payload.log;auto& writer=app->storage_.log_writer_;
        const auto error=command.kind==MessageKind::OpenLogSession ? (fail_open?LogError::Io:writer.open(log.session_id,log.created_utc_ms)) :
            writer.requestFlush(log.session_id,log.sample_seq,command.kind==MessageKind::CloseLogSession);
        result.status=error==LogError::None?OperationStatus::Ok:OperationStatus::Unavailable;
        result.storage=app->data_.storage;++result.storage.revision;result.storage.log=writer.snapshot();result.storage.log_result=error;
        check(app->results_.send(reply),"storage return queue full");
    }
}
void operation(LogAction operation) {
    auto& ui=app->ui_.model_;ui.keyPressed(UiKey::GpsControl,host_tick);
    // Select the requested operation from the production default (Pause/Resume).
    if(operation==LogAction::Stop)ui.keyPressed(UiKey::Left,host_tick);
    ui.keyPressed(UiKey::Enter,host_tick);app->readUiContent();app->processLog();
    check(ui.snapshot().action.kind==ActionKind::None,"logging action was not acknowledged");
}
void sample(unsigned second) {
    char body[120];std::snprintf(body,sizeof(body),"GNRMC,0100%02u.000,A,3540.98460,N,14001.63164,E,2.0,140.0,251026,,,A",second);
    unsigned crc=0;for(const char* p=body;*p;++p)crc^=static_cast<unsigned char>(*p);
    char sentence[140];std::snprintf(sentence,sizeof(sentence),"$%s*%02X\r\n",body,crc);
    host_tick+=1000;for(const char* p=sentence;*p;++p)app->gps_.receiver_.feed(*p,host_tick);
    host_tick+=100;app->gps_.receiver_.poll(host_tick);app->gps_.notify();app->drainNotifications();app->publishUiContent();
}
}
namespace geoputer {
USBCDC Console;
void UiTask::run(){} void StorageTask::run(){}
void StorageTask::sdBegin(){} void StorageTask::sdEnd(){} void StorageTask::cacheState(RgCacheState,bool){}
LogError StorageTask::tryAppendLog(const LogCommand& command,LogSnapshot& snapshot){const auto error=log_writer_.append(command,taskTimeMs());snapshot=log_writer_.snapshot();return error;}
// Unrelated App routes are peer boundaries in this test; reaching them is a failure.
void Application::prepareLogScreen(ScreenId,ScreenId){}
bool Application::operateBrowser(const UiAction&){throw std::runtime_error("unexpected browser operation");}
bool Application::operateDataset(const UiAction&){throw std::runtime_error("unexpected dataset operation");}
bool Application::requestDataset(DatasetRequest){throw std::runtime_error("unexpected dataset request");}
bool Application::operateBootMode(const UiAction&){throw std::runtime_error("unexpected boot mode operation");}
bool Application::operateUsbExport(const UiAction&){throw std::runtime_error("unexpected export operation");}
bool Application::datasetChangeReady()const{return loggerStopped(data_.recording,data_.storage.log);}
void Application::completeDataset(const DatasetView&){} void Application::completeBrowser(const LogReadReply&){} void Application::completeMap(const map::FrameInfo&){}
bool Application::changeMapScale(uint8_t){return false;}
}
int main(){
    char directory[]="/tmp/rgp-app-log-XXXXXX";check(mkdtemp(directory)!=nullptr,"temp directory");
    Application instance;app=&instance;app->storage_.log_writer_.directory_=std::string(directory)+"/logs";
    check(app->results_.initialize() && app->notifications_.initialize() && app->gps_.initialize() && app->ui_.initialize() && app->storage_.initialize(),"queues");
    hostQueueHook=route;app->screen_=ScreenId::GpsLogger;app->generation_=1;app->data_.settings=defaultSettings();app->data_.settings.log_interval=0;
    app->data_.storage.card_state=CardState::Mounted;app->data_.storage.revision=1;
    app->ui_.model_.setScreen({app->screen_,DisplayMode::View,1,app->data_,0,ActionResult::None});
    operation(LogAction::Start);check(app->data_.recording.state==RecordingState::Starting && app->data_.storage.log.open,"Start opens session");
    check(calls.size()==2 && calls[0]==MessageKind::StartGpsReception && calls[1]==MessageKind::OpenLogSession,"GPS starts before session");
    sample(1);check(app->data_.recording.state==RecordingState::Recording && app->data_.storage.log.accepted==1,"next fixed sample records");
    const auto session=app->data_.storage.log.session_id;
    operation(LogAction::Pause);check(app->data_.recording.state==RecordingState::Paused && app->gps_.ready_ && calls.back()==MessageKind::FlushLog,"pause flushes but keeps GPS");
    check(app->storage_.log_writer_.flush(host_tick),"flush pause");app->data_.storage.log=app->storage_.log_writer_.snapshot();app->publishUiContent();
    check(app->data_.storage.log.name[0] && app->data_.storage.log.saved==1,"published name and saved count");
    sample(2);check(app->data_.storage.log.accepted==1,"paused samples are not logged");
    operation(LogAction::Resume);sample(3);check(app->data_.recording.state==RecordingState::Recording && app->data_.storage.log.accepted==2 && app->data_.storage.log.session_id==session,"resume same session");
    operation(LogAction::Stop);check(app->data_.recording.state==RecordingState::Stopped && app->data_.storage.log.closing && app->data_.gps.fix.valid,"stop closes asynchronously and preserves GPS fix");
    check(calls.back()==MessageKind::CloseLogSession && app->gps_.serial_.sent.empty(),"log stop leaves GPS continuously receiving without standby TX");
    check(app->storage_.log_writer_.flush(host_tick),"finish close");app->data_.storage.log=app->storage_.log_writer_.snapshot();app->publishUiContent();
    // Stale notifications cannot replace the newer receiver snapshot.
    AppNotification old{};old.source=TaskId::GPS;old.kind=MessageKind::GpsSampleReady;old.payload.gps.revision=1;old.payload.gps.fix.valid=false;
    app->handleNotification(old);check(app->data_.gps.fix.valid,"late notification replaced current fix");
    operation(LogAction::Start);check(app->data_.recording.state==RecordingState::Starting && app->data_.gps.fix.valid,"restart keeps continuously received fix");
    sample(4);
    check(app->data_.recording.state==RecordingState::Recording && app->data_.storage.log.accepted==1 && app->data_.storage.log.session_id!=session,"restart uses fresh session and sequence");
    operation(LogAction::Stop);app->storage_.log_writer_.flush(host_tick);app->data_.storage.log=app->storage_.log_writer_.snapshot();app->publishUiContent();
    fail_open=true;operation(LogAction::Start);check(app->data_.recording.state==RecordingState::Stopped && !app->log_accepting_,"failed session start cannot enable recording");
    // Display sleep/wake never changes task, record or current screen state.
    app->ui_.model_.keyPressed(UiKey::ScreenOff,host_tick);app->readUiContent();check(app->mode_==DisplayMode::Background,"S sleep");
    app->ui_.model_.keyPressed(UiKey::OpenMap,host_tick);app->readUiContent();check(app->mode_==DisplayMode::View && app->screen_==ScreenId::GpsLogger,"wake preserves screen without shortcut execution");
    // Map tracking does not gate current-place searches. Feed real App dispatch
    // two completed name results while the boundary image's anchor stays fixed.
    hold_place_queries=true;
    app->screen_=ScreenId::Map;app->mode_=DisplayMode::View;app->data_.boot.maintenance=false;
    app->data_.settings.dataset=1;app->data_.storage.busy=false;
    app->data_.storage.card_state=CardState::Mounted;
    app->data_.dataset={PlaceState::Ready,1,1500,42,10000,10000};
    app->data_.gps.fix={35.68,140.02,1000,true};app->data_.gps.reception=GpsReception::Fix;
    app->data_.gps.sample_seq=500;app->data_.map.scale=1;app->place_flow_.refresh();
    check(app->setScreen(ScreenId::Map),"enter map through the production generation transition");
    map::Transfer shown{};shown.info.width=240;shown.info.height=101;shown.info.region=17;shown.info.meters_per_pixel=9;
    shown.info.request.token=77;shown.info.request.screen_generation=app->generation_;
    shown.info.request.dataset_generation=app->data_.dataset.generation;shown.info.request.latitude=35.68;shown.info.request.longitude=140.02;
    check(app->ui_.model_.beginMapFrame(shown)==OperationStatus::Ok,"map fixture begins");
    for(unsigned row=0;row<101;row+=8){shown.row=row;shown.rows=std::min(8u,101-row);check(app->ui_.model_.writeMapRows(shown)==OperationStatus::Ok,"map fixture rows");}
    check(app->ui_.model_.commitMapFrame(shown)==OperationStatus::Ok,"map fixture commits");
    auto lookup=[&](const char* name,bool hidden=false) {
        app->requestPlace();StorageRequest query{};
        check(app->storage_.inbox().receive(query) && query.kind==MessageKind::SearchPlace,"map queues shared place lookup");
        AppNotification event{};event.source=TaskId::Storage;event.kind=MessageKind::ReadCompleted;
        auto& reply=event.payload.storage_completion;reply.request_id=query.request_id;reply.read_kind=StorageReadKind::Place;
        reply.snapshot=app->data_.storage;reply.snapshot.settings=app->data_.settings;++reply.snapshot.revision;
        reply.place.request=query.payload.place_request;reply.place.state=PlaceState::Ready;reply.place.dataset=app->data_.dataset;
        reply.place.region_code=42;std::strcpy(reply.place.lines[0],name);
        if(hidden)app->mode_=DisplayMode::Background;
        app->handleNotification(event);app->publishUiContent();
        if(hidden) {
            check(app->place_flow_.waitMs(host_tick,app->data_.gps.sample_seq,app->data_.gps.fix,10000,10000)==0,
                  "hidden completion must not suppress the lookup after wake");
            app->mode_=DisplayMode::View;
        } else check(!std::strcmp(app->ui_.model_.data().place[0],name),"map accepts current-place result without rebuilding image");
        check(app->ui_.model_.mapFrame().info().request.token==77 && app->ui_.model_.mapFrame().image().bytes()==6060,
              "place result changed the displayed boundary image");
    };
    lookup("FIRST AREA");
    app->data_.gps.fix.longitude+=0.00001;++app->data_.gps.sample_seq;app->requestPlace();
    StorageRequest no_query{};check(!app->storage_.inbox().receive(no_query),"same lattice cell sends no storage query");
    app->data_.gps.fix.longitude+=0.00006;++app->data_.gps.sample_seq;lookup("SECOND AREA");
    app->map_request_.token=123;++app->data_.gps.sample_seq;app->data_.gps.fix.longitude+=0.0001;app->requestPlace();
    check(!app->storage_.inbox().receive(no_query),"map job ownership prevents a silently discarded search request");
    app->map_request_={};app->mode_=DisplayMode::Background;app->requestPlace();
    check(!app->storage_.inbox().receive(no_query),"background mode does not start place IO");
    app->mode_=DisplayMode::View;lookup("HIDDEN",true);lookup("THIRD AREA");
    std::puts("PASS: shared current-place lookup on map, changed-cell dispatch, same-cell suppression and map-job exclusion");
    hostQueueHook=nullptr;std::filesystem::remove_all(directory);
    std::puts("PASS: production App logging, GPS pause/stop/restart, fresh epochs, async close, filename, failures and wake");
}

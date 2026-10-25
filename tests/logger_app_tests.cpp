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
AppNotification late_dataset{};bool deliver_dataset_on_ui_send=false;
StorageRequest last_read{};
MapBuildJob map_backend;
bool reject_screen=false;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void route(QueueHandle_t queue) {
    ReturnMessage reply{};
    if(queue==app->gps_.inbox().handle_) {
        GpsRequest command{};app->gps_.inbox().receive(command);calls.push_back(command.kind);
        app->gps_.handle(command);app->gps_.sendReturn();return;
    }
    if(queue==app->ui_.inbox().handle_) {
        UiRequest command{};app->ui_.inbox().receive(command);
        if(deliver_dataset_on_ui_send) {
            deliver_dataset_on_ui_send=false;
            app->handleNotification(late_dataset);
        }
        prepareSyncReturn(reply,TaskId::UI,command,1);auto& result=reply.payload.result;
        if(command.kind==MessageKind::GetUiContent){result.content=app->ui_.model_.snapshot();result.status=OperationStatus::Ok;}
        else if(command.kind==MessageKind::UpdateUiContent)result.status=app->ui_.model_.updateContent(command.payload.update);
        else if(command.kind==MessageKind::SetUiScreen) {
            result.status=reject_screen?OperationStatus::Unavailable:app->ui_.model_.setScreen(command.payload.screen);
            reject_screen=false;
        }
        else throw std::runtime_error("unexpected UI request");
        check(app->results_.send(reply),"UI return queue full");return;
    }
    if(queue==app->storage_.inbox().handle_) {
        if(hold_place_queries)return;
        StorageRequest command{};app->storage_.inbox().receive(command);calls.push_back(command.kind);
        prepareSyncReturn(reply,TaskId::Storage,command,1);auto& result=reply.payload.result;
        if(command.kind==MessageKind::CancelLogRead)return;
        if(command.kind==MessageKind::RequestMap || command.kind==MessageKind::CancelMap || command.kind==MessageKind::ReleaseMap) {
            const bool ok=command.kind==MessageKind::RequestMap?map_backend.start(command.payload.map):map_backend.release(command.payload.map);
            result.status=ok?OperationStatus::Ok:OperationStatus::Unavailable;
            check(app->results_.send(reply),"map job return queue full");return;
        }
        if(command.kind==MessageKind::DatasetOperation || command.kind==MessageKind::ListLogs ||
           command.kind==MessageKind::ReadLogSummary || command.kind==MessageKind::ReadLogRecords) {
            last_read=command;result.status=OperationStatus::Ok;
            result.storage=app->data_.storage;++result.storage.revision;
            result.storage.busy=command.kind==MessageKind::DatasetOperation;
            check(app->results_.send(reply),"read request return queue full");return;
        }
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




bool Application::operateBootMode(const UiAction&){throw std::runtime_error("unexpected boot mode operation");}
bool Application::operateUsbExport(const UiAction&){throw std::runtime_error("unexpected export operation");}



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
            check(app->place_flow_.waitMs(app->data_.gps.sample_seq,app->data_.gps.fix,10000,10000)==0,
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
    // A delayed catalog response must not overwrite the Logs page even when it
    // arrives inside the synchronous SetUiScreen call, before generation changes.
    hold_place_queries=false;app->data_.storage.busy=false;
    app->data_.recording={};app->data_.storage.log={};app->data_.storage.settings_state=SettingsState::Loaded;
    check(app->setScreen(ScreenId::Settings),"enter settings before catalog read");
    DatasetRequest catalog{};catalog.action=DatasetAction::Open;
    check(app->requestDataset(catalog) && app->data_.hasDatasets() && app->data_.datasets().loading,"catalog accepted");
    late_dataset={};late_dataset.source=TaskId::Storage;late_dataset.kind=MessageKind::ReadCompleted;
    auto& catalog_reply=late_dataset.payload.storage_completion;
    catalog_reply.read_kind=StorageReadKind::DatasetBrowser;
    catalog_reply.snapshot=app->data_.storage;++catalog_reply.snapshot.revision;catalog_reply.snapshot.busy=false;
    catalog_reply.snapshot.settings=app->data_.settings;
    catalog_reply.dataset_view.revision=app->dataset_waiting_;catalog_reply.dataset_view.count=1;
    std::strcpy(catalog_reply.dataset_view.rows[0].title,"DELAYED DATASET");
    deliver_dataset_on_ui_send=true;
    check(app->setScreen(ScreenId::Logs),"shortcut from settings to logs while catalog is pending");
    check(!app->dataset_waiting_ && app->data_.hasBrowse() && app->data_.browse().loading &&
          !app->data_.browse().deleting && !app->data_.browse().names[0][0],"late catalog must release token without overwriting browser");
    check(app->ui_.model_.data().hasBrowse(),"UI receives browser payload after the synchronous shortcut");
    // Conversely, a late log result cannot replace the dataset payload.
    app->processBrowser();check(last_read.kind==MessageKind::ListLogs,"browser queues a real read request");
    const auto log_request=last_read.payload.log_read;
    check(app->setScreen(ScreenId::Settings),"leave log read for settings");
    check(app->requestDataset(catalog),"new catalog request selects dataset payload");
    const auto catalog_token=app->dataset_waiting_;
    LogReadReply late_log{};late_log.request=log_request;late_log.count=1;
    std::strcpy(late_log.data.names[0],"must-not-overwrite-dataset.gps.dat");
    app->completeBrowser(late_log);
    check(app->data_.hasDatasets() && app->data_.datasets().loading && !app->browse_waiting_,
          "late log releases its token but leaves dataset payload intact");
    DatasetView ready{};ready.revision=catalog_token;ready.count=1;ready.rows[0].number=2;
    std::strcpy(ready.rows[0].title,"CURRENT DATASET");app->completeDataset(ready);
    check(app->data_.hasDatasets() && !app->data_.datasets().loading &&
          !std::strcmp(app->data_.datasets().rows[0].title,"CURRENT DATASET"),"current catalog still completes normally");
    // Preserve list names and selection through detail/point/back navigation.
    check(app->setScreen(ScreenId::Logs),"reopen browser after dataset use");
    auto& list=app->data_.editBrowse();list.loading=false;list.count=2;list.selected=1;++list.revision;
    std::strcpy(list.names[0],"20261025_120000_0.gps.dat");std::strcpy(list.names[1],"20261025_130000_0.gps.dat");
    UiAction open{};open.browse_action=BrowseAction::OpenFile;open.browse_index=1;open.browse_revision=list.revision;
    check(app->operateBrowser(open) && app->screen_==ScreenId::LogDetail,"open detail with retained list");
    app->data_.editBrowse().summary.records=20;
    check(app->setScreen(ScreenId::LogPoint) && app->setScreen(ScreenId::LogDetail) && app->setScreen(ScreenId::Logs),"point and back navigation");
    check(app->data_.hasBrowse() && app->data_.browse().selected==1 && app->data_.browse().count==2 &&
          !std::strcmp(app->data_.browse().names[1],"20261025_130000_0.gps.dat"),"browser hierarchy retains list content and selection");
    std::puts("PASS: exclusive UI page payloads, delayed dataset/log responses and retained browser hierarchy");
    // Map work may still be owned by Storage when its display is left. App
    // releases its own union; late map results and cancellation must spare Logs.
    app->data_.storage.busy=false;app->data_.settings.dataset=1;app->place_flow_.refresh();
    app->data_.gps.fix.valid=true;app->data_.gps.reception=GpsReception::Fix;
    check(app->setScreen(ScreenId::Map),"enter map resource group");
    app->processMap();check(map_backend.occupied() && app->map_request_.token,"map job accepted by Storage peer");
    const auto old_map=map_backend.info();
    check(app->setScreen(ScreenId::Logs) && app->screen_resources_.hasLogs() && app->map_cancel_,"Map -> Logs releases App map work and retains cancellation identity");
    auto& log_work=app->screen_resources_.logs();log_work.cache_count=1;log_work.records[0][0]=0x5a;
    app->completeMap(old_map);
    check(log_work.cache_count==1 && log_work.records[0][0]==0x5a,"late map completion overwrote log work");
    app->processMap();check(!map_backend.occupied() && !app->map_request_.token && log_work.records[0][0]==0x5a,
          "off-screen cancellation must release Storage map job without reading App map union");
    // A record request can finish after the work union has become a map again.
    auto& point_view=app->data_.editBrowse();point_view.summary.session_id=81;point_view.summary.records=20;
    check(app->setScreen(ScreenId::LogPoint),"open point resource group");
    app->processBrowser();check(last_read.kind==MessageKind::ReadLogRecords,"record request accepted");
    const auto stale_records=last_read.payload.log_read;
    check(app->setScreen(ScreenId::Map) && app->screen_resources_.hasMap(),"Logs -> Map replaces log work");
    auto& map_work=app->screen_resources_.map();map_work.transfer.pixels[0]=0xa5;map_work.row=17;
    app->processMap();check(!app->map_request_.token,"new map request must wait for the old read's terminal result");
    app->processBrowser(); // Send pending cancellation even without log work.
    LogReadReply stale{};stale.request=stale_records;stale.count=1;
    std::memset(stale.data.records,0xee,sizeof(stale.data.records));app->completeBrowser(stale);
    check(!app->browse_waiting_ && app->screen_resources_.hasMap() && map_work.transfer.pixels[0]==0xa5 && map_work.row==17,
          "late records must consume their token without touching the active map work");
    // Failed screen acceptance leaves the original resources and display intact.
    reject_screen=true;const auto before_generation=app->generation_;
    check(!app->setScreen(ScreenId::Logs) && app->generation_==before_generation && app->screen_==ScreenId::Map &&
          app->screen_resources_.hasMap() && map_work.transfer.pixels[0]==0xa5,"rejected screen transition released current resources");
    for(unsigned n=0;n<30;++n) {
        check(app->setScreen(ScreenId::Logs) && app->screen_resources_.hasLogs() && !app->screen_resources_.logs().cache_count,
              "fresh Logs work contains an old map or record cache");
        check(app->setScreen(ScreenId::Map) && app->screen_resources_.hasMap() && !app->screen_resources_.map().transfer.pixels[0],
              "fresh Map work contains the previous log cache");
    }
    std::puts("PASS: screen resource handoff, worker cancellation after leave, stale map/record results, rejected transition and repeated union reuse");
    hostQueueHook=nullptr;std::filesystem::remove_all(directory);
    std::puts("PASS: production App logging, GPS pause/stop/restart, fresh epochs, async close, filename, failures and wake");
}

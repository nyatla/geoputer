// Exercise production App dispatch and logging against queued GPS/UI and a real LogWriter.
#include "GpsApplication.h"
#include "LogReplay.h"
#include "task/storage/dataset/DatasetReadMemory.h"
#include "utils/Console.h"
#include "task/storage/log/LogFormat.h"
#include "task/ui/KeyBindings.h"
#include <stdexcept>
#include <cstring>
#include <cstdio>
#include <vector>
#include <filesystem>
using namespace geoputer;
namespace {
GpsApplication* app=nullptr;
std::vector<MessageKind> calls;
bool fail_open=false,hold_place_queries=false;
unsigned audio_starts=0,audio_stops=0;alarm::AudioCommand last_audio{};
AppNotification late_dataset{};bool deliver_dataset_on_ui_send=false;
StorageRequest last_read{};
ReadFileSlot map_backend_file;MapBuildJob map_backend(map_backend_file);map::CodeImageBuffer shared_map_pixels{};map::MapSession live_session(shared_map_pixels),log_session(shared_map_pixels);
bool reject_screen=false,reject_place_return=false,reject_dataset_return=false,reject_dataset_once=false;
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
        else if(command.kind==MessageKind::ControlAudio) {
            if(command.payload.audio.play)++audio_starts;else ++audio_stops;
            last_audio=command.payload.audio;app->ui_.model_.setAudioPreview(command.payload.audio.preview);
            app->ui_.model_.setAudioActive(command.payload.audio.play);result.status=OperationStatus::Ok;
        }
        else if(command.kind==MessageKind::BeginDatasetRead)result.status=app->ui_.model_.beginDatasetRead(command.payload.dataset.request,result.map_buffer);
        else if(command.kind==MessageKind::CommitDatasetRead)result.status=app->ui_.model_.commitDatasetRead(command.payload.dataset);
        else if(command.kind==MessageKind::DiscardDatasetRead) {
            if(reject_dataset_return){reject_dataset_return=false;result.status=OperationStatus::Unavailable;}
            else result.status=app->ui_.model_.discardDatasetRead(command.payload.dataset);
        }
        else if(command.kind==MessageKind::DatasetReadError)result.status=app->ui_.model_.datasetReadError(command.payload.dataset);
        else if(command.kind==MessageKind::UpdateDatasetProgress)result.status=app->ui_.model_.updateDatasetProgress(command.payload.dataset_progress);
        else if(command.kind==MessageKind::BeginPlaceSearch)result.status=app->ui_.model_.beginPlaceSearch(command.payload.place,result.map_buffer);
        else if(command.kind==MessageKind::CommitPlaceSearch)result.status=app->ui_.model_.commitPlaceSearch(command.payload.place);
        else if(command.kind==MessageKind::DiscardPlaceSearch) {
            if(reject_place_return){reject_place_return=false;result.status=OperationStatus::Unavailable;}
            else result.status=app->ui_.model_.discardPlaceSearch(command.payload.place);
        }
        else if(command.kind==MessageKind::BeginMapFrame)result.status=app->ui_.model_.beginMapFrame(command.payload.map.request,result.map_buffer);
        else if(command.kind==MessageKind::DiscardMapFrame)result.status=app->ui_.model_.discardMapFrame(command.payload.map.request);
        else if(command.kind==MessageKind::CommitMapFrame)result.status=app->ui_.model_.commitMapFrame(command.payload.map);
        else throw std::runtime_error("unexpected UI request");
        check(app->results_.send(reply),"UI return queue full");return;
    }
    if(queue==app->storage_.inbox().handle_) {
        if(hold_place_queries)return;
        StorageRequest command{};app->storage_.inbox().receive(command);calls.push_back(command.kind);
        prepareSyncReturn(reply,TaskId::Storage,command,1);auto& result=reply.payload.result;
        if(command.kind==MessageKind::CancelLogRead)return;
        if(command.kind==MessageKind::ReleaseLogBrowse)return; // One-way, like cancellation.
        if(command.kind==MessageKind::CloseMapSession) {
            auto& session=command.payload.map.session==map::SessionId::Log?log_session:live_session;
            if(session.belongs(command.payload.map))session.clear();
            result.status=OperationStatus::Ok;check(app->results_.send(reply),"close map session return");return;
        }
        if(command.kind==MessageKind::RequestMap || command.kind==MessageKind::CancelMap || command.kind==MessageKind::ReleaseMap) {
            const bool ok=command.kind==MessageKind::RequestMap?map_backend.start(command.payload.map,command.payload.map.session==map::SessionId::Log?log_session:live_session):map_backend.release(command.payload.map);
            result.status=ok?OperationStatus::Ok:OperationStatus::Unavailable;
            check(app->results_.send(reply),"map job return queue full");return;
        }
        if(command.kind==MessageKind::DatasetOperation || command.kind==MessageKind::ListLogs ||
           command.kind==MessageKind::ReadLogSummary || command.kind==MessageKind::ReadLogRecords || command.kind==MessageKind::ReadLogStatistics) {
            last_read=command;result.status=OperationStatus::Ok;
            if(command.kind==MessageKind::DatasetOperation && reject_dataset_once){reject_dataset_once=false;result.status=OperationStatus::Unavailable;}
            result.storage=app->data_.storage;++result.storage.revision;
            result.storage.busy=command.kind==MessageKind::DatasetOperation;
            check(app->results_.send(reply),"read request return queue full");return;
        }
        const auto& log=command.payload.log;auto& writer=app->storage_.gpsState().log_writer_;
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
void GpsUiTask::run(){} void GpsStorageTask::run(){}
void GpsStorageTask::sdBegin(){} void GpsStorageTask::sdEnd(){} void GpsStorageTask::cacheState(RgCacheState,bool){}
LogError GpsStorageTask::tryAppendLog(const LogCommand& command,LogSnapshot& snapshot){const auto error=gpsState().log_writer_.append(command,taskTimeMs());snapshot=gpsState().log_writer_.snapshot();return error;}
// Unrelated App routes are peer boundaries in this test; reaching them is a failure.




bool GpsApplication::operateBootMode(const UiAction&){throw std::runtime_error("unexpected boot mode operation");}



}
int main(){
    char directory[]="/tmp/rgp-app-log-XXXXXX";check(mkdtemp(directory)!=nullptr,"temp directory");
    const std::string log_directory=std::string(directory)+"/logs";
    GpsApplication instance;app=&instance;check(&app->storage_.gpsState().selected_dataset_==&app->storage_.gpsState().place_lookup_.selected_,
        "Storage owns one session borrowed by lookup; App stores no second session");app->storage_.gpsState().log_writer_.directory_=LogDirectory(log_directory);
    check(app->results_.initialize() && app->notifications_.initialize() && app->gps_.initialize() && app->ui_.initialize() && app->storage_.initialize(),"queues");
    hostQueueHook=route;app->screen_=ScreenId::GpsLogger;app->generation_=1;app->data_.settings=defaultSettings();app->data_.settings.log_interval=0;
    app->data_.storage.card_state=CardState::Mounted;app->data_.storage.revision=1;
    app->ui_.model_.setScreen({app->screen_,DisplayMode::View,1,app->data_,0,ActionResult::None});
    operation(LogAction::Start);check(app->data_.recording.state==RecordingState::Starting && app->data_.storage.log.open,"Start opens session");
    check(calls.size()==2 && calls[0]==MessageKind::StartGpsReception && calls[1]==MessageKind::OpenLogSession,"GPS starts before session");
    sample(1);check(app->data_.recording.state==RecordingState::Recording && app->data_.storage.log.accepted==1,"next fixed sample records");
    const auto session=app->data_.storage.log.session_id;
    operation(LogAction::Pause);check(app->data_.recording.state==RecordingState::Paused && app->gps_.ready_ && calls.back()==MessageKind::FlushLog,"pause flushes but keeps GPS");
    check(app->storage_.gpsState().log_writer_.flush(host_tick),"flush pause");app->data_.storage.log=app->storage_.gpsState().log_writer_.snapshot();app->publishUiContent();
    check(app->data_.storage.log.name[0] && app->data_.storage.log.saved==1,"published name and saved count");
    sample(2);check(app->data_.storage.log.accepted==1,"paused samples are not logged");
    operation(LogAction::Resume);sample(3);check(app->data_.recording.state==RecordingState::Recording && app->data_.storage.log.accepted==2 && app->data_.storage.log.session_id==session,"resume same session");
    operation(LogAction::Stop);check(app->data_.recording.state==RecordingState::Stopped && app->data_.storage.log.closing && app->data_.gps.fix.valid,"stop closes asynchronously and preserves GPS fix");
    check(calls.back()==MessageKind::CloseLogSession && app->gps_.serial_.sent.empty(),"log stop leaves GPS continuously receiving without standby TX");
    check(app->storage_.gpsState().log_writer_.flush(host_tick),"finish close");app->data_.storage.log=app->storage_.gpsState().log_writer_.snapshot();app->publishUiContent();
    // Stale notifications cannot replace the newer receiver snapshot.
    AppNotification old{};old.source=TaskId::GPS;old.kind=MessageKind::GpsSampleReady;old.payload.gps.revision=1;old.payload.gps.fix.valid=false;
    app->handleNotification(old);check(app->data_.gps.fix.valid,"late notification replaced current fix");
    operation(LogAction::Start);check(app->data_.recording.state==RecordingState::Starting && app->data_.gps.fix.valid,"restart keeps continuously received fix");
    sample(4);
    check(app->data_.recording.state==RecordingState::Recording && app->data_.storage.log.accepted==1 && app->data_.storage.log.session_id!=session,"restart uses fresh session and sequence");
    operation(LogAction::Stop);app->storage_.gpsState().log_writer_.flush(host_tick);app->data_.storage.log=app->storage_.gpsState().log_writer_.snapshot();app->publishUiContent();
    fail_open=true;operation(LogAction::Start);check(app->data_.recording.state==RecordingState::Stopped && !app->gpsState().log_accepting_,"failed session start cannot enable recording");
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
    app->data_.gps.sample_seq=500;app->data_.map.scale=1;app->gpsState().place_flow_.refresh();
    check(app->setScreen(ScreenId::Map),"enter map through the production generation transition");
    map::FrameInfo shown{};shown.width=240;shown.height=101;shown.region=17;shown.meters_per_pixel=9;
    shown.request.token=77;shown.request.screen_generation=app->generation_;
    shown.request.dataset_generation=app->data_.dataset.generation;shown.request.latitude=35.68;shown.request.longitude=140.02;
    check(app->ui_.model_.beginMapFrame(shown.request,shown.request.buffer)==OperationStatus::Ok,"map fixture begins");
    std::memset(shown.request.buffer.data,0,map::BoundaryBytes);
    check(app->ui_.model_.commitMapFrame(shown)==OperationStatus::Ok,"map fixture commits");
    auto lookup=[&](const char* name,bool hidden=false) {
        app->requestPlace();StorageRequest query{};
        check(app->storage_.inbox().receive(query) && query.kind==MessageKind::SearchPlace,"map queues shared place lookup");
        AppNotification event{};event.source=TaskId::Storage;event.kind=MessageKind::ReadCompleted;
        auto& reply=event.payload.storage_completion;reply.request_id=query.request_id;reply.read_kind=StorageReadKind::Place;
        reply.snapshot=app->data_.storage;reply.snapshot.settings=app->data_.settings;++reply.snapshot.revision;
        auto& output=*placeResultMemory(query.payload.place_request);
        output={};output.request=query.payload.place_request;output.state=PlaceState::Ready;
        output.dataset=app->data_.dataset;output.region_code=42;std::strcpy(output.lines[0],name);
        reply.place=placeCompletion(output);
        if(hidden)app->mode_=DisplayMode::Background;
        app->handleNotification(event);
        check(app->gpsState().place_flow_.pending(),"notification handler must defer UI sync until the main loop");
        app->processPlace();app->publishUiContent();
        if(hidden) {
            check(app->gpsState().place_flow_.waitMs(app->data_.gps.sample_seq,app->data_.gps.fix,10000,10000)==0,
                  "hidden completion must not suppress the lookup after wake");
            app->mode_=DisplayMode::View;app->publishUiContent();
        } else check(!std::strcmp(app->ui_.model_.data().place[0],name),"map accepts current-place result without rebuilding image");
        check(app->ui_.model_.mapFrame().info().request.token==77 && app->ui_.model_.mapFrame().image().bytes()==6060,
              "place result changed the displayed boundary image");
    };
    lookup("FIRST AREA");
    app->data_.gps.fix.longitude+=0.00001;++app->data_.gps.sample_seq;app->requestPlace();
    StorageRequest no_query{};check(!app->storage_.inbox().receive(no_query),"same lattice cell sends no storage query");
    app->data_.gps.fix.longitude+=0.00006;++app->data_.gps.sample_seq;lookup("SECOND AREA");
    app->gpsState().map_request_.token=123;++app->data_.gps.sample_seq;app->data_.gps.fix.longitude+=0.0001;app->requestPlace();
    check(!app->storage_.inbox().receive(no_query),"map job ownership prevents a silently discarded search request");
    app->gpsState().map_request_={};app->mode_=DisplayMode::Background;app->requestPlace();
    check(!app->storage_.inbox().receive(no_query),"background mode does not start place IO");
    app->mode_=DisplayMode::View;lookup("HIDDEN",true);lookup("THIRD AREA");
    // UI lending may succeed while Storage's queue is full. Retain and return
    // the unsubmitted loan even if the first UI return itself is rejected.
    app->gpsState().place_flow_.refresh();StorageRequest blocker{};blocker.kind=MessageKind::GetStorageStatus;
    check(app->storage_.inbox().send(blocker),"fill Storage before place query");
    app->requestPlace();check(app->gpsState().place_borrow_.token && !app->gpsState().place_queued_ && !app->gpsState().place_flow_.pending(),"queue-full search must retain an unsubmitted loan");
    reject_place_return=true;app->processPlace();check(app->gpsState().place_borrow_.token && app->ui_.model_.mapFrame().leased(),"failed UI return keeps memory unavailable");
    app->processPlace();check(!app->gpsState().place_borrow_.token && !app->ui_.model_.mapFrame().leased(),"unsubmitted loan returns on retry");
    check(app->storage_.inbox().receive(blocker) && blocker.kind==MessageKind::GetStorageStatus,"failed search did not replace queued Storage work");
    // The writer may finish after both App and UI replaced their screen data.
    app->requestPlace();StorageRequest departing{};check(app->storage_.inbox().receive(departing),"place pending before screen switch");
    check(app->setScreen(ScreenId::Settings) && app->ui_.model_.mapFrame().leased(),"screen switch preserves place workspace");
    auto* old_result=placeResultMemory(departing.payload.place_request);
    old_result->request=departing.payload.place_request;old_result->state=PlaceState::Ready;old_result->dataset=app->data_.dataset;
    std::strcpy(old_result->lines[0],"LATE SCREEN NAME");
    AppNotification finished{};finished.source=TaskId::Storage;finished.kind=MessageKind::ReadCompleted;
    auto& completion=finished.payload.storage_completion;completion.request_id=departing.request_id;
    completion.read_kind=StorageReadKind::Place;completion.snapshot=app->data_.storage;completion.snapshot.settings=app->data_.settings;
    completion.place.request=departing.payload.place_request;
    app->handleNotification(finished);check(app->gpsState().place_flow_.pending(),"late notification only stages completion");
    reject_place_return=true;app->processPlace();check(app->gpsState().place_borrow_.token,"stale result return can be retried");
    app->processPlace();check(!app->gpsState().place_borrow_.token && !app->gpsState().place_flow_.pending() &&
        std::strcmp(app->data_.place[0],"LATE SCREEN NAME"),"old screen result is returned without publication");
    check(app->setScreen(ScreenId::Map),"restore map after place-lifetime scenarios");
    std::puts("PASS: shared current-place lookup on map, changed-cell dispatch, same-cell suppression and map-job exclusion");
    // Dataset text stays in the UI back loan, including a screen switch during a sync wait.
    hold_place_queries=false;app->data_.storage.busy=false;
    app->data_.recording={};app->data_.storage.log={};app->data_.storage.settings_state=SettingsState::Loaded;
    check(app->setScreen(ScreenId::Settings),"enter settings before catalog read");
    DatasetRequest catalog{};catalog.action=DatasetAction::Open;
    check(app->requestDataset(catalog) && !app->data_.hasDatasets() && app->ui_.model_.data().datasets().loading,"UI owns pending catalog, App holds control only");
    auto request=app->gpsState().dataset_borrow_;check(validDatasetMemory(request) && app->ui_.model_.mapFrame().leased(),"catalog borrows the fixed back image");
    auto makeCatalog=[&](const DatasetRequest& req,const char* title) {
        auto& view=*datasetResultMemory(req);view={};view.revision=req.token;view.count=1;view.rows[0].number=2;
        std::snprintf(view.rows[0].title,sizeof(view.rows[0].title),"%s",title);
        AppNotification event{};event.source=TaskId::Storage;event.kind=MessageKind::ReadCompleted;
        auto& done=event.payload.storage_completion;done.read_kind=StorageReadKind::DatasetBrowser;
        done.snapshot=app->data_.storage;++done.snapshot.revision;done.snapshot.busy=false;
        done.dataset={req,DatasetError::None};return event;
    };
    late_dataset=makeCatalog(request,"DELAYED DATASET");deliver_dataset_on_ui_send=true;
    check(app->setScreen(ScreenId::Logs),"shortcut to logs during catalog write");
    check(app->gpsState().dataset_waiting_ && app->ui_.model_.mapFrame().leased(),"notification cannot return a loan from inside a sync wait");
    app->processDataset();
    check(!app->gpsState().dataset_waiting_ && !app->ui_.model_.mapFrame().leased() && app->data_.hasBrowse() && !app->data_.browse().names[0][0],"late catalog returns loan without replacing Logs");
    app->processBrowser();check(last_read.kind==MessageKind::ListLogs,"browser queues read");
    const auto log_request=last_read.payload.log_read;
    check(app->setScreen(ScreenId::Settings) && app->requestDataset(catalog),"catalog intent can wait for an old log read");
    LogReadReply late_log{};late_log.request=log_request;late_log.count=1;std::strcpy(late_log.data.list.names[0],"must-not-overwrite-dataset.gps.dat");
    app->completeBrowser(late_log);app->processDataset();
    check(app->gpsState().dataset_waiting_ && !app->data_.hasDatasets(),"App never stores catalog result");
    auto current=makeCatalog(app->gpsState().dataset_borrow_,"CURRENT DATASET");app->handleNotification(current);app->processDataset();
    check(!app->gpsState().dataset_waiting_ && !std::strcmp(app->ui_.model_.data().datasets().rows[0].title,"CURRENT DATASET"),"current catalog publishes in UI");
    app->publishUiContent();
    check(!std::strcmp(app->ui_.model_.data().datasets().rows[0].title,"CURRENT DATASET"),"general App updates cannot overwrite the UI-owned page");
    // Superseding read, failed return and resubmission preserve the old lease.
    app->data_.storage.busy=false;check(app->requestDataset(catalog),"first superseded read");request=app->gpsState().dataset_borrow_;
    catalog.number=2;check(app->requestDataset(catalog) && app->gpsState().dataset_borrow_.token==request.token,"new intent cannot revoke Storage loan");
    auto superseded=makeCatalog(request,"SUPERSEDED");app->handleNotification(superseded);reject_dataset_return=true;app->processDataset();
    check(app->gpsState().dataset_borrow_.token==request.token,"failed return retains owner");app->processDataset();
    check(app->gpsState().dataset_borrow_.token && app->gpsState().dataset_borrow_.token!=request.token,"next read begins after old loan returns");
    current=makeCatalog(app->gpsState().dataset_borrow_,"LATEST");app->handleNotification(current);app->processDataset();
    check(!std::strcmp(app->ui_.model_.data().datasets().rows[0].title,"LATEST"),"superseded text never publishes");
    reject_dataset_once=true;check(app->requestDataset(catalog) && app->gpsState().dataset_ready_,"rejected Storage submission schedules loan return");
    app->processDataset();check(app->gpsState().dataset_borrow_.token && app->gpsState().dataset_queued_,"rejected request retries after returning UI back");
    current=makeCatalog(app->gpsState().dataset_borrow_,"RETRIED");app->handleNotification(current);app->processDataset();
    DatasetRequest verify{};verify.action=DatasetAction::VerifyIndex;verify.purpose=DatasetPurpose::Verify;verify.number=app->data_.settings.dataset;
    check(app->requestDataset(verify) && app->gpsState().dataset_applying_,"maintenance enters busy state without an App result payload");
    AppNotification progress_event{};progress_event.source=TaskId::Storage;progress_event.kind=MessageKind::DatasetProgress;
    progress_event.payload.index_progress.token=app->gpsState().dataset_waiting_;progress_event.payload.index_progress.percent=55;
    app->handleNotification(progress_event);app->processDataset();
    check(app->ui_.model_.data().datasets().progress.percent==55,"App forwards progress without copying display text");
    current=makeCatalog(app->gpsState().dataset_borrow_,"UNUSED MAINTENANCE TEXT");app->handleNotification(current);app->processDataset();
    check(app->gpsState().dataset_check_needed_ && !app->gpsState().dataset_applying_ && !app->ui_.model_.data().datasets().applying,"maintenance completion schedules dataset check and releases UI loan");
    app->gpsState().dataset_check_needed_=false;
    // Scratch contention during initialization is retryable and keeps the selected dataset.
    const auto selected=app->data_.settings.dataset;app->gpsState().dataset_check_waiting_=123;
    AppNotification busy{};busy.source=TaskId::Storage;busy.kind=MessageKind::ReadCompleted;
    auto& blocked=busy.payload.storage_completion;blocked.read_kind=StorageReadKind::Dataset;blocked.request_id=123;
    blocked.snapshot=app->data_.storage;++blocked.snapshot.revision;blocked.place.state=PlaceState::Waiting;
    app->handleNotification(busy);
    check(!app->gpsState().dataset_check_waiting_ && app->gpsState().dataset_check_needed_ && app->data_.settings.dataset==selected,"initialization scratch contention retries without unselecting dataset");
    app->gpsState().dataset_check_needed_=false;
    std::puts("PASS: UI dataset loans, App control-only state, stale/superseded results, retryable returns and init scratch contention");
    // Preserve list names and selection through detail/point/back navigation.
    check(app->setScreen(ScreenId::Logs),"reopen browser after dataset use");
    auto& list=app->data_.editBrowse();list.loading=false;list.count=2;list.selected=1;++list.revision;
    std::strcpy(list.names[0],"20261025_120000_0.gps.dat");std::strcpy(list.names[1],"20261025_130000_0.gps.dat");
    UiAction open{};open.browse_action=BrowseAction::OpenFile;open.browse_index=1;open.browse_revision=list.revision;
    check(app->operateBrowser(open) && app->screen_==ScreenId::LogProperties,"open text directly with retained list");
    app->data_.editBrowse().summary.records=20;
    check(app->setScreen(ScreenId::Logs),"point and back navigation");
    check(app->data_.hasBrowse() && app->data_.browse().selected==1 && app->data_.browse().count==2 &&
          !std::strcmp(app->data_.browse().names[1],"20261025_130000_0.gps.dat"),"browser hierarchy retains list content and selection");
    std::puts("PASS: exclusive UI page payloads, delayed dataset/log responses and retained browser hierarchy");
    // Map work may still be owned by Storage when its display is left. App
    // releases its own union; late map results and cancellation must spare Logs.
    app->data_.storage.busy=false;app->data_.settings.dataset=1;app->gpsState().place_flow_.refresh();
    app->data_.gps.fix.valid=true;app->data_.gps.reception=GpsReception::Fix;
    check(app->setScreen(ScreenId::Map),"enter map resource group");
    app->processMap();check(map_backend.occupied() && app->gpsState().map_request_.token,"map job accepted by Storage peer");
    const auto old_map=map_backend.info();
    check(app->setScreen(ScreenId::Logs) && app->screenResources().hasLogs() && app->gpsState().map_cancel_,"Map -> Logs releases App map work and retains cancellation identity");
    auto& log_work=app->screenResources().logs();log_work.cache_count=1;log_work.records[0][0]=0x5a;
    app->completeMap(old_map);
    check(log_work.cache_count==1 && log_work.records[0][0]==0x5a,"late map completion overwrote log work");
    app->processMap();check(!map_backend.occupied() && !app->gpsState().map_request_.token && log_work.records[0][0]==0x5a,
          "off-screen cancellation must release Storage map job without reading App map union");
    // A record request can finish after the work union has become a map again.
    auto& point_view=app->data_.editBrowse();point_view.summary.session_id=81;point_view.summary.records=20;
    check(app->setScreen(ScreenId::LogProperties),"open properties resource group");
    app->processBrowser();check(last_read.kind==MessageKind::ReadLogSummary,"point entry first requests the file bounds");
    LogReadReply point_bounds{};point_bounds.request=last_read.payload.log_read;point_bounds.summary.session_id=81;point_bounds.summary.records=20;
    app->completeBrowser(point_bounds);
    UiAction enter_point{};enter_point.browse_action=BrowseAction::OpenPoint;check(app->operateBrowser(enter_point),"Enter opens point after properties");
    app->processBrowser();check(last_read.kind==MessageKind::ReadLogRecords,"record request accepted");
    const auto stale_records=last_read.payload.log_read;
    check(app->setScreen(ScreenId::Map) && app->screenResources().hasMap(),"Logs -> Map replaces log work");
    auto& map_work=app->screenResources().map();map_work.info.region=0xa5;map_work.retry_at=17;
    app->processMap();check(!app->gpsState().map_request_.token,"new map request must wait for the old read's terminal result");
    app->processBrowser(); // Send pending cancellation even without log work.
    LogReadReply stale{};stale.request=stale_records;stale.count=1;
    std::memset(stale.data.records,0xee,sizeof(stale.data.records));app->completeBrowser(stale);
    check(!app->browse_waiting_ && app->screenResources().hasMap() && map_work.info.region==0xa5 && map_work.retry_at==17,
          "late records must consume their token without touching the active map work");
    check(app->browse_release_generation_ && !app->browse_waiting_,"screen exit retains its release generation after late completion");
    hold_place_queries=true;StorageRequest blocking{};blocking.kind=MessageKind::GetStorageStatus;
    check(app->storage_.inbox().send(blocking),"fill queue before scope release");
    app->processBrowser();check(app->browse_release_generation_,"full queue retains release for retry without blocking");
    check(app->storage_.inbox().receive(blocking),"drain blocked queue");hold_place_queries=false;
    const auto calls_before_release=calls.size();app->processBrowser();
    check(!app->browse_release_generation_ && calls.size()==calls_before_release+1 && calls.back()==MessageKind::ReleaseLogBrowse,
          "finished browse is released without waiting for a synchronous return or log screen resources");
    // Failed screen acceptance leaves the original resources and display intact.
    reject_screen=true;const auto before_generation=app->generation_;
    check(!app->setScreen(ScreenId::Logs) && app->generation_==before_generation && app->screen_==ScreenId::Map &&
          app->screenResources().hasMap() && map_work.info.region==0xa5,"rejected screen transition released current resources");
    for(unsigned n=0;n<30;++n) {
        check(app->setScreen(ScreenId::Logs) && app->screenResources().hasLogs() && !app->screenResources().logs().cache_count,
              "fresh Logs work contains an old map or record cache");
        check(app->setScreen(ScreenId::Map) && app->screenResources().hasMap() && !app->screenResources().map().info.region,
              "fresh Map work contains the previous log cache");
    }
    std::puts("PASS: screen resource handoff, worker cancellation after leave, stale map/record results, rejected transition and repeated union reuse");

    // Exercise the complete log viewer using historical points far from live GPS.
    app->data_.dataset.state=PlaceState::Ready;app->data_.dataset.unit_inv_x=app->data_.dataset.unit_inv_y=10000;
    app->data_.settings.dataset=1;app->data_.storage.busy=false;
    app->data_.gps.fix={40.,145.,0,true};app->data_.gps.reception=GpsReception::Fix;
    check(app->setScreen(ScreenId::Logs),"viewer list");
    auto& browser=app->data_.editBrowse();browser.loading=false;browser.count=1;browser.point.valid=true;browser.point.latitude=80.;++browser.revision;
    std::strcpy(browser.names[0],"20261025_120000_0.gps.dat");
    open.browse_index=0;open.browse_revision=browser.revision;
    check(app->operateBrowser(open) && !app->data_.browse().point.valid,"new file must not expose the previous file's point");
    check(app->screen_==ScreenId::LogProperties,"file opens property-only screen");
    app->processBrowser();check(last_read.payload.log_read.kind==LogReadKind::Summary,"direct view first requests validated file bounds");
    LogReadReply opened{};opened.request=last_read.payload.log_read;opened.summary.session_id=91;opened.summary.records=25;
    app->completeBrowser(opened);
    auto& v=app->data_.editBrowse();check(v.replay_speed==config::LogReplayDefaultSpeed && !v.replay_reverse,"new file resets replay preferences");check(!v.loading && !app->screenResources().logs().need,"properties never trigger full scan or point reads");
    UiAction enter{};enter.browse_action=BrowseAction::Map;check(app->operateBrowser(enter) && v.map_mode,"2 opens historical map directly from properties");
    UiAction choose{};
    choose.browse_action=BrowseAction::Playback;
    check(app->operateBrowser(choose) && v.playing,"P starts map replay");
    app->processReplay();check(v.playing,"initial point loading must not abort replay");
    app->processBrowser();const auto historical_request=last_read.payload.log_read;
    LogReadReply points{};points.request=historical_request;points.count=10;
    for(unsigned n=0;n<10;++n) {
        GpsFix historical{35.68+n*.00001,140.02,0,true};historical.fields=GpsTime;historical.utc_epoch_ms=1789309921000ull+n*1000;
        check(encodeLogRecord(historical,points.data.records[n]),"historical record encoding");
    }
    app->completeBrowser(points);check(v.point_loaded && v.point.latitude==35.68 && v.index==0,"historical point loaded");
    hold_place_queries=true;app->requestPlace();StorageRequest historical_place{};
    check(app->storage_.inbox().receive(historical_place) && historical_place.kind==MessageKind::SearchPlace &&
          historical_place.payload.place_request.fix.latitude==v.point.latitude,"reverse geocode must use log coordinates");
    hold_place_queries=false;app->publishUiContent(); // Publish the loaded point as runOnce() does.
    AppNotification name{};name.kind=MessageKind::ReadCompleted;name.source=TaskId::Storage;
    auto& c=name.payload.storage_completion;c.read_kind=StorageReadKind::Place;c.request_id=historical_place.request_id;
    c.snapshot=app->data_.storage;
    auto& historical_result=*placeResultMemory(historical_place.payload.place_request);
    historical_result={};historical_result.request=historical_place.payload.place_request;
    historical_result.state=PlaceState::Ready;historical_result.dataset=app->data_.dataset;
    std::strcpy(historical_result.lines[0],"HISTORICAL REGION");c.place=placeCompletion(historical_result);
    app->handleNotification(name);app->processPlace();
    check(!std::strcmp(app->data_.place[0],"HISTORICAL REGION") && app->data_.gps.fix.latitude==40.,"historical lookup leaves live GPS intact");
    app->data_.gps.fix.valid=false;app->data_.gps.reception=GpsReception::NoFix;
    app->publishUiContent();app->processMap();
    check(app->gpsState().map_request_.token && app->gpsState().map_request_.latitude==v.point.latitude,"historical map uses saved coordinates without live GPS fix");
    MapSourceResult source(shared_map_pixels);source.info.request=map_backend.info().request;
    check(map::planViewport(v.point.latitude,v.point.longitude,10000,10000,96*1024,source.plan)==map::Error::None,"historical viewport");
    source.info.width=240;source.info.height=101;source.info.meters_per_pixel=source.plan.meters_per_pixel;
    source.reserve(source.view().count);std::fill_n(source.pixels,source.view().count,11u);
    map_backend.loaded(std::move(source));while(!map_backend.complete())map_backend.step();app->completeMap(map_backend.info());
    for(unsigned n=0;n<20 && app->gpsState().map_request_.token;++n)app->processMap();
    check(!app->gpsState().map_request_.token && app->ui_.model_.mapFrame().image().bytes(),"historical map reaches UI atomically");
    const auto anchor=app->screenResources().map().anchor;
    host_tick+=999;app->processReplay();check(v.index==0,"replay waits a full second");
    host_tick+=1;app->processReplay();check(v.index==1 && v.playing,"replay advances one point from cache");
    app->requestPlace();check(!app->gpsState().place_flow_.pending() && app->data_.place_fix.latitude==v.point.latitude,"same cell reuses shared place lookup");
    app->processMap();check(!app->gpsState().map_request_.token && app->screenResources().map().anchor.request.token==anchor.request.token,
          "nearby historical point moves cursor without rebuilding map");
    app->publishUiContent();check(app->ui_.model_.mapCursor().valid && std::abs(app->ui_.model_.mapCursor().y)>.01,"log cursor follows historical point");
    app->publishUiContent();
    auto& ui=app->ui_.model_;
    ui.keyPressed(UiKey::Up,host_tick);app->readUiContent();
    check(v.playing && v.index==1 && v.replay_speed==3 && replay::intervalMs(v)==500,"PLAY Up accelerates without moving or pausing");
    ui.keyPressed(UiKey::Down,host_tick);app->readUiContent();
    check(v.playing && v.index==1 && v.replay_speed==2,"PLAY Down decelerates without moving");
    UiAction rate{};rate.browse_action=BrowseAction::ReplayFaster;
    for(unsigned i=0;i<10;++i)check(app->operateBrowser(rate),"rate increase handled");
    check(v.replay_speed==replay::SpeedCount-1 && replay::intervalMs(v)==250,"rate clamps at four-times");
    rate.browse_action=BrowseAction::ReplaySlower;
    for(unsigned i=0;i<10;++i)check(app->operateBrowser(rate),"rate decrease handled");
    check(!v.replay_speed && replay::intervalMs(v)==4000,"rate clamps at quarter-times");
    rate.browse_action=BrowseAction::ReplayFaster;
    check(app->operateBrowser(rate) && app->operateBrowser(rate),"restore default pace");
    // Control messages do not replace an already accepted record request.
    v.loading=true;const auto pending_read=app->screenResources().logs().request;
    check(app->operateBrowser(rate) && v.loading && v.index==1 &&
        !std::memcmp(&pending_read,&app->screenResources().logs().request,sizeof(pending_read)),"rate adjustment preserves in-flight read");
    host_tick+=1000;app->processReplay();check(v.index==1 && v.playing && app->screenResources().logs().next_at==host_tick+500,"loading retains the new hold interval without catch-up");
    v.loading=false;rate.browse_action=BrowseAction::ReplaySlower;check(app->operateBrowser(rate),"restore normal interval after loading");
    app->publishUiContent();ui.keyPressed(UiKey::Left,host_tick);app->readUiContent();
    check(v.replay_reverse && v.playing && v.index==1,"PLAY Left selects reverse without navigating");
    host_tick+=999;app->processReplay();check(v.index==1,"reverse holds the point for the chosen interval");
    ++host_tick;app->processReplay();check(v.index==0 && v.playing,"reverse advances to previous cached point");
    host_tick+=1000;app->processReplay();check(v.index==0 && !v.playing,"reverse pauses at the first point without underflow");
    check(!app->operateBrowser(rate),"rate-only request is rejected after pause");
    app->publishUiContent();ui.keyPressed(UiKey::Playback,host_tick);app->readUiContent();
    ui.keyPressed(UiKey::Right,host_tick);app->readUiContent();
    check(v.playing && !v.replay_reverse && v.index==0,"PLAY Right selects forward at first point");
    host_tick+=1000;app->processReplay();check(v.index==1 && v.playing,"forward resumes from current point");
    const auto local_generation=app->generation_;
    app->publishUiContent();ui.keyPressed(UiKey::OpenLivePlace,host_tick);app->readUiContent();
    check(!v.map_mode && !v.playing && v.index==1 && app->generation_==local_generation,"1 directly selects text, pauses and preserves point/generation");
    ui.keyPressed(UiKey::Tab,host_tick);app->readUiContent();
    check(v.map_mode && !v.playing && v.index==1 && app->generation_==local_generation,"Tab directly selects historical map without reentering screen");
    ui.keyPressed(UiKey::Playback,host_tick);app->readUiContent();
    ui.keyPressed(UiKey::OpenMap,host_tick);app->readUiContent();
    check(v.map_mode && v.playing && app->generation_==local_generation,"current view shortcut keeps replay and generation");
    ui.keyPressed(UiKey::OpenLivePlace,host_tick);app->readUiContent();
    check(!v.map_mode && !v.playing && v.index==1,"switching away from PLAY retains selected point");
    ui.keyPressed(UiKey::Down,host_tick);app->readUiContent();check(v.target==11 && v.loading,"down skips ten points");
    app->processBrowser();points.request=last_read.payload.log_read;points.count=10;
    for(unsigned n=0;n<10;++n) {
        GpsFix f{35.69+n*.00001,140.03,0,true};check(encodeLogRecord(f,points.data.records[n]),"second page encoding");
    }
    app->completeBrowser(points);check(v.index==11 && !v.loading,"page read fulfills current skip target");
    check(!std::strcmp(app->data_.place[0],"HISTORICAL REGION") && app->data_.place_state==PlaceState::Waiting &&
          app->data_.place_fix.latitude!=v.point.latitude,"new-cell read keeps the completed label and its old coordinates while lookup waits");
    app->publishUiContent();
    check(!std::strcmp(ui.data().place[0],"HISTORICAL REGION"),"UI snapshot never publishes a blank label between log points");
    hold_place_queries=true;app->requestPlace();StorageRequest stale_name{};
    check(app->storage_.inbox().receive(stale_name),"historical name pending");hold_place_queries=false;
    UiAction move{};move.browse_action=BrowseAction::Point;move.browse_index=12;check(app->operateBrowser(move),"move during lookup");
    c.request_id=stale_name.request_id;c.place.request=stale_name.payload.place_request;
    auto& stale_result=*placeResultMemory(stale_name.payload.place_request);
    stale_result={};stale_result.request=stale_name.payload.place_request;
    stale_result.state=PlaceState::Ready;stale_result.dataset=app->data_.dataset;
    std::strcpy(stale_result.lines[0],"STALE POINT NAME");c.place=placeCompletion(stale_result);
    app->handleNotification(name);app->processPlace();
    check(!app->gpsState().place_flow_.pending() && !std::strcmp(app->data_.place[0],"HISTORICAL REGION"),
          "late point result is rejected while the last completed label remains visible");
    app->publishUiContent();
    check(!std::strcmp(ui.data().place[0],"HISTORICAL REGION"),"discarding a stale lookup does not blank the UI label");
    hold_place_queries=true;app->requestPlace();StorageRequest current_name{};
    check(app->storage_.inbox().receive(current_name) && current_name.kind==MessageKind::SearchPlace,"retry names for the current point");
    hold_place_queries=false;app->publishUiContent();
    check(!std::strcmp(ui.data().place[0],"HISTORICAL REGION") && ui.data().place_state==PlaceState::Searching,
          "in-flight lookup retains completed labels in UI");
    auto& current_result=*placeResultMemory(current_name.payload.place_request);
    current_result={};current_result.request=current_name.payload.place_request;
    current_result.state=PlaceState::Ready;current_result.dataset=app->data_.dataset;current_result.region_code=22;
    std::strcpy(current_result.lines[0],"CURRENT REGION");std::strcpy(current_result.lines[1],"CURRENT DISTRICT");
    c.request_id=current_name.request_id;c.snapshot=app->data_.storage;c.place=placeCompletion(current_result);
    app->handleNotification(name);app->processPlace();
    check(!std::strcmp(app->data_.place[0],"CURRENT REGION") && !std::strcmp(app->data_.place[1],"CURRENT DISTRICT") &&
          app->data_.place_fix.latitude==v.point.latitude && app->data_.place_region==22,"accepted completion replaces both label rows and their coordinate/region together");
    check(!std::strcmp(ui.data().place[0],"CURRENT REGION") && !std::strcmp(ui.data().place[1],"CURRENT DISTRICT"),
          "UI commits the new labels without publishing the rejected or blank label");
    move.browse_index=11;check(app->operateBrowser(move),"restore point for return test");
    choose.browse_action=BrowseAction::Map;
    check(app->operateBrowser(choose) && v.index==11 && !v.loading && !v.playing,"2 retains the selected point without an intermediate screen");
    app->publishUiContent();ui.keyPressed(mapPhysicalKey('-',false),host_tick);app->readUiContent();
    check(app->data_.map.scale==2 && v.index==11,"minus changes historical map scale without navigating");
    ui.keyPressed(mapPhysicalKey('+',false),host_tick);app->readUiContent();
    check(app->data_.map.scale==1 && v.index==11,"plus restores historical map scale without navigating");
    ui.keyPressed(mapPhysicalKey('p',false),host_tick);app->readUiContent();check(v.playing,"P resumes");
    ui.keyPressed(UiKey::GpsControl,host_tick);app->readUiContent();check(!v.playing && ui.confirming(),"GPS modal pauses historical playback");
    ui.keyPressed(UiKey::Escape,host_tick);ui.keyPressed(mapPhysicalKey('p',false),host_tick);app->readUiContent();
    ui.keyPressed(UiKey::ScreenOff,host_tick);app->readUiContent();check(app->mode_==DisplayMode::Background && !v.playing,"screen off pauses replay");
    ui.keyPressed(UiKey::Other,host_tick);app->readUiContent();
    v.point_loaded=true;v.loading=false;v.point.valid=true;v.index=v.target=24;v.playing=true;
    app->data_.map.error=map::Error::None;app->data_.map.busy=false;app->data_.place_state=PlaceState::Ready;
    app->screenResources().logs().next_at=host_tick;app->processReplay();check(!v.playing && v.index==24,"EOF pauses without wrapping");
    // Reverse traversal crosses the ten-record cache boundary through normal IO.
    v.replay_reverse=true;v.index=v.target=10;v.playing=true;
    app->screenResources().logs().next_at=host_tick;app->processReplay();
    check(v.target==9 && v.loading && v.playing && app->screenResources().logs().need,"reverse crosses cache boundary without skipping points");
    app->processBrowser();check(last_read.payload.log_read.start==0 && last_read.payload.log_read.count==10,"reverse uses the previous aligned record batch");
    points.request=last_read.payload.log_read;points.count=10;
    for(unsigned n=0;n<10;++n) {GpsFix fix{35.68+n*.00001,140.02,0,true};check(encodeLogRecord(fix,points.data.records[n]),"reverse batch fixture");}
    app->completeBrowser(points);check(v.index==9 && !v.loading && v.playing,"reverse IO publishes the requested point and retains play");
    v.playing=true;v.error=LogReadError::Corrupt;app->processReplay();check(!v.playing,"bad record pauses replay");
    app->data_.settings.dataset=0;v.error=LogReadError::None;app->publishUiContent();ui.keyPressed(UiKey::OpenLivePlace,host_tick);app->readUiContent();
    check(!v.map_mode,"text remains available without dataset");
    ui.keyPressed(UiKey::OpenMap,host_tick);app->readUiContent();check(!v.map_mode,"2 map rejected without dataset");
    v.error=LogReadError::None;v.loading=false;
    UiAction stats_action{};stats_action.browse_action=BrowseAction::Statistics;
    app->publishUiContent();ui.keyPressed(UiKey::Tab,host_tick);app->readUiContent();
    check(app->screen_==ScreenId::LogStatistics,"Tab skips unavailable map and selects log statistics instead of live GPS");
    app->processBrowser();check(last_read.kind==MessageKind::ReadLogStatistics && app->browse_waiting_,"statistics accepted without waiting for complete file scan");
    const auto stats_request=last_read.payload.log_read;
    AppNotification progress{};progress.source=TaskId::Storage;progress.kind=MessageKind::LogStatisticsProgress;
    progress.payload.log_progress={stats_request.token,stats_request.screen_generation,8,25,200};app->handleNotification(progress);
    check(v.statistics.processed==8 && v.loading,"statistics progress reaches the screen through App");
    LogReadReply stats_done{};stats_done.request=stats_request;stats_done.summary=v.summary;
    stats_done.data.statistics.processed=25;stats_done.data.statistics.distance_m=12.5;
    app->completeBrowser(stats_done);check(v.statistics_ready && !v.loading && v.statistics.distance_m==12.5,"complete statistics held as one cached result");
    app->publishUiContent();ui.keyPressed(UiKey::Tab,host_tick);app->readUiContent();
    check(app->screen_==ScreenId::LogPoint && !v.map_mode && v.index==9 && v.point_loaded,"1 returns from statistics to retained text point");
    app->data_.settings.dataset=1;app->publishUiContent();ui.keyPressed(UiKey::OpenMap,host_tick);app->readUiContent();
    check(app->screen_==ScreenId::LogPoint && v.map_mode && v.index==9,"2 returns to historical map with retained point");
    check(app->operateBrowser(stats_action) && !app->screenResources().logs().need && v.statistics_ready,"reentry uses cached statistics without SD scan");
    stats_action.browse_action=BrowseAction::Recalculate;check(app->operateBrowser(stats_action) && !v.statistics_ready && v.loading,"R explicitly requests fresh statistics");
    app->processBrowser();const auto cancelled_stats=last_read.payload.log_read;
    check(app->setScreen(ScreenId::Map) && app->browse_cancel_,"leaving statistics retains cancellation outside screen union");
    LogReadReply late_stats{};late_stats.request=cancelled_stats;late_stats.data.statistics.processed=0xffffffff;
    app->completeBrowser(late_stats);check(!app->browse_waiting_ && !app->data_.hasBrowse(),"late statistics cannot overwrite a different screen resource");
    // Restore historical display for the independent recording regression below.
    check(app->setScreen(ScreenId::LogPoint),"restore point after stale result test");
    auto& restored=app->data_.editBrowse();restored.point.valid=true;restored.point.latitude=35.68;restored.index=11;restored.loading=false;
    // Live reception/recording remains independent of the currently displayed historical point.
    fail_open=false;const auto displayed_latitude=v.point.latitude;const auto displayed_index=v.index;
    operation(LogAction::Start);sample(5);
    check(app->data_.recording.state==RecordingState::Recording && app->data_.storage.log.accepted==1 &&
          v.index==displayed_index && v.point.latitude==displayed_latitude,"live recording replaced a historical point or stopped during viewing");
    operation(LogAction::Stop);check(app->storage_.gpsState().log_writer_.flush(host_tick),"finish log recorded while viewing");
    app->data_.storage.log=app->storage_.gpsState().log_writer_.snapshot();
    std::puts("PASS: log viewer modes, historical map and names, cursor tracking, skips, cache boundaries, replay timing, end/error/modal/screen-off pause and dataset constraints");
    // Direct entry also terminates cleanly for empty or invalid files.
    check(app->setScreen(ScreenId::Logs),"return to the retained file list");
    auto& final_list=app->data_.editBrowse();final_list.loading=false;final_list.count=1;final_list.total_files=1;
    std::strcpy(final_list.names[0],"20261025_120000_0.gps.dat");++final_list.revision;
    open.browse_index=0;open.browse_revision=final_list.revision;
    check(app->operateBrowser(open) && app->screen_==ScreenId::LogProperties,"empty-file direct entry");
    app->processBrowser();LogReadReply empty{};empty.request=last_read.payload.log_read;empty.summary.session_id=91;
    app->completeBrowser(empty);
    check(!app->data_.browse().loading && !app->data_.browse().point_loaded && !app->screenResources().logs().need,"empty summary must not schedule a record read or wait forever");
    app->publishUiContent();ui.keyPressed(UiKey::Escape,host_tick);app->readUiContent();
    check(app->screen_==ScreenId::Logs && !std::strcmp(app->data_.browse().names[0],"20261025_120000_0.gps.dat"),"Esc returns to the retained full filename");
    open.browse_revision=app->data_.browse().revision;
    check(app->operateBrowser(open),"invalid-file direct entry");app->processBrowser();
    LogReadReply invalid{};invalid.request=last_read.payload.log_read;invalid.error=LogReadError::Header;
    app->completeBrowser(invalid);
    check(!app->data_.browse().loading && app->data_.browse().error==LogReadError::Header && !app->screenResources().logs().need,"invalid headers retain a visible error without entering an action menu");
    std::puts("PASS: direct text entry, automatic initial batch, empty/header-error completion and Esc-to-list");
    // A fresh App exercises live alerts while a historical point is displayed
    // and the display is dark. No test substitutes the App scheduling logic.
    static GpsApplication alarm_instance;app=&alarm_instance;
    check(app->results_.initialize() && app->notifications_.initialize() && app->ui_.initialize() && app->gps_.initialize() && app->storage_.initialize(),"alarm App queues");
    app->screen_=ScreenId::LogPoint;app->generation_=1;app->mode_=DisplayMode::Background;
    app->data_.settings=defaultSettings();app->data_.settings.dataset=1;
    app->data_.dataset.state=PlaceState::Ready;app->data_.storage.card_state=CardState::Mounted;
    app->data_.gps.fix={35.68,140.02,0,true};app->data_.gps.reception=GpsReception::Fix;app->data_.gps.sample_seq=100;
    auto& alarm_profile=app->data_.storage.alarm;alarm_profile.dataset=1;alarm_profile.depth=2;alarm_profile.identity=77;
    alarm_profile.revision=1;alarm_profile.state=alarm::State::Ready;alarm_profile.settings.enabled=true;alarm_profile.settings.duration=3;
    auto& history=app->data_.editBrowse();history.summary.records=20;history.point_loaded=true;history.point={80.,12.,0,true};
    std::strcpy(app->data_.place[0],"HISTORY LABEL");
    auto& alarm_ui=app->ui_.model_;alarm_ui.setScreen({app->screen_,DisplayMode::Background,1,app->data_,0,ActionResult::None});
    auto alarmSample=[&](uint32_t region,unsigned agreeing) {
        hold_place_queries=true;app->processAlarm();StorageRequest query{};
        check(app->storage_.inbox().receive(query) && query.kind==MessageKind::SearchPlace && query.payload.place_request.alarm_sample &&
              query.payload.place_request.fix.latitude==app->data_.gps.fix.latitude,"background alarm samples actual GPS instead of replay point");
        hold_place_queries=false;
        auto& result=*placeResultMemory(query.payload.place_request);result={};result.request=query.payload.place_request;
        result.state=PlaceState::Ready;result.dataset=app->data_.dataset;result.alarm.center.depth=1;
        result.alarm.center.path[0]=region;result.alarm.valid_points=9;result.alarm.agreeing=agreeing;
        AppNotification completed{};completed.source=TaskId::Storage;completed.kind=MessageKind::ReadCompleted;
        auto& body=completed.payload.storage_completion;body.request_id=query.request_id;body.snapshot=app->data_.storage;
        body.snapshot.revision=app->data_.storage.revision+1;body.read_kind=StorageReadKind::Place;body.place=placeCompletion(result);
        app->handleNotification(completed);check(app->gpsState().place_completion_ready_,"alarm async result is staged without a display search token");
        app->processPlace();check(!app->gpsState().place_borrow_.token && !std::strcmp(app->data_.place[0],"HISTORY LABEL"),"alarm returns UI loan without replacing replay labels");
    };
    alarmSample(1,9);check(!audio_starts,"first live position establishes quiet alarm baseline");
    ++app->data_.gps.sample_seq;alarmSample(2,6);check(!audio_starts,"standard waits for seven adjacent-cell votes");
    ++app->data_.gps.sample_seq;alarmSample(2,7);check(audio_starts==1 && app->gpsState().alarm_audio_owned_,"standard crossing starts audio on the qualifying sample in dark-screen mode");
    alarm_ui.keyPressed(UiKey::OpenMap,host_tick);app->readUiContent();
    check(!app->gpsState().alarm_audio_owned_ && audio_stops==1 && app->mode_==DisplayMode::View && app->screen_==ScreenId::LogPoint,"first key silences and wakes without executing map shortcut");
    ++app->data_.gps.sample_seq;alarmSample(1,1);check(audio_starts==1,"silence preserves accepted region and the return threshold");
    ++app->data_.gps.sample_seq;alarmSample(3,7);check(audio_starts==2,"third region can trigger another live alarm");
    app->data_.storage.card_state=CardState::NoCard;app->processAlarm();check(!app->gpsState().alarm_audio_owned_ && audio_stops==2,"SD unavailable stops sounding alarm");
    app->data_.storage.card_state=CardState::Mounted;alarm_profile.settings.enabled=false;++alarm_profile.revision;app->processAlarm();
    check(app->playAlarm(alarm_profile.settings,true),"audition works while notifications are OFF");
    host_tick+=alarm::SoundSession::cycleMs(alarm_profile.settings.tone)+1;app->processAlarm();
    check(!app->gpsState().alarm_audio_owned_,"audition ends after one pattern");
    app->data_.settings.power_level=power::Level::Normal;
    check(app->playAlarm(alarm_profile.settings,true),"managed audition accepted");
    host_tick+=alarm::SoundSession::cycleMs(alarm_profile.settings.tone)+1;app->processAlarm();
    check(app->gpsState().alarm_audio_owned_,"App preserves managed one-shot during bounded preparation budget");
    host_tick+=power::AudioPreparationBudgetMs;app->processAlarm();
    check(!app->gpsState().alarm_audio_owned_,"managed audition watchdog ends after pattern and preparation budget");
    check(app->setScreen(ScreenId::Settings),"enter settings for tone-switch collaboration");
    for(unsigned i=0;i<settingsRootIndex(SettingsRootItem::Alarm);++i)alarm_ui.keyPressed(UiKey::Down,host_tick);
    alarm_ui.keyPressed(UiKey::Enter,host_tick);for(unsigned i=0;i<3;++i)alarm_ui.keyPressed(UiKey::Down,host_tick);
    alarm_ui.keyPressed(UiKey::Right,host_tick);const auto before_preview=audio_starts;app->readUiContent();
    check(audio_starts==before_preview+1 && last_audio.preview && last_audio.tone==alarm::Tone::Rising && last_audio.duration==0,
          "UI candidate goes through App to audio once, even with notifications OFF");
    check(app->data_.storage.alarm.settings.tone==alarm::Tone::Double && alarm_ui.editDirty(),"preview does not persist tone");
    alarm_ui.keyPressed(UiKey::Right,host_tick);app->readUiContent();
    check(last_audio.tone==alarm::Tone::Falling && alarm_ui.snapshot().action.kind==ActionKind::None,"next tone replaces active preview and is acknowledged");
    app->stopAlarm();
    std::puts("PASS: production App alarms use live GPS on dark replay screen, async ownership, quiet baseline, cell thresholds/mute/wake, SD failure and audition");
    hostQueueHook=nullptr;std::filesystem::remove_all(directory);
    std::puts("PASS: production App logging, GPS pause/stop/restart, fresh epochs, async close, filename, failures and wake");
}

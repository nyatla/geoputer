#include "task/ui/GpsUiModel.h"
#include "task/storage/dataset/DatasetReadMemory.h"
#include <cstring>
#include <stdexcept>
using namespace geoputer;
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
}
void testDatasetBorrowing() {
    UiData common{};common.settings=defaultSettings();common.storage.card_state=CardState::Mounted;common.storage.settings_state=SettingsState::Loaded;
    common.editDatasets().count=1;common.editDatasets().rows[0].id.assign("test-old");std::strcpy(common.editDatasets().rows[0].title,"OLD");
    GpsUiModel ui;check(ui.setScreen({ScreenId::Settings,DisplayMode::View,1,common,0,ActionResult::None})==OperationStatus::Ok,"dataset test enters settings");
    DatasetRequest request{};request.token=10;request.screen_generation=1;map::BufferLease buffer{};
    check(ui.beginDatasetRead(request,buffer)==OperationStatus::Ok,"dataset borrows fixed UI back");request.buffer=buffer;
    check(validDatasetMemory(request) && buffer.capacity==6060,"dataset typed layout fits existing image");
    DatasetRequest other{};other.token=11;other.screen_generation=1;
    map::BufferLease second{};check(ui.beginDatasetRead(other,second)==OperationStatus::Unavailable,"only one back operation may own bytes");
    auto& result=*datasetResultMemory(request);result={};result.revision=request.token;result.count=1;result.rows[0].id.assign("test-new");std::strcpy(result.rows[0].title,"NEW");
    std::strcpy(common.editDatasets().rows[0].title,"STALE APP");++common.revision;
    check(ui.updateContent({DisplayMode::View,1,common,0,ActionResult::None})==OperationStatus::Ok &&
          !std::strcmp(ui.data().datasets().rows[0].title,"OLD") && ui.data().datasets().loading,"UI cannot display partial Storage writes or stale App payload");
    DatasetCompletion done{request,DatasetError::None};
    auto wrong=done;++wrong.request.buffer.id;
    check(ui.commitDatasetRead(wrong)==OperationStatus::StaleContent && ui.mapFrame().leased(),"wrong lease cannot publish or return current loan");
    check(ui.commitDatasetRead(done)==OperationStatus::Ok && !ui.mapFrame().leased() && !std::strcmp(ui.data().datasets().rows[0].title,"NEW"),"UI publishes then releases result memory");
    ++common.revision;ui.updateContent({DisplayMode::View,1,common,0,ActionResult::None});
    check(!std::strcmp(ui.data().datasets().rows[0].title,"NEW"),"common state update retains UI-owned dataset");
    check(ui.commitDatasetRead(done)==OperationStatus::StaleContent,"duplicate completion cannot republish reused bytes");
    request={};request.token=12;request.screen_generation=1;buffer={};
    check(ui.beginDatasetRead(request,buffer)==OperationStatus::Ok,"start read before leaving settings");request.buffer=buffer;
    UiData logs=common;logs.editBrowse()={};
    check(ui.setScreen({ScreenId::Logs,DisplayMode::View,2,logs,0,ActionResult::None})==OperationStatus::Ok && ui.mapFrame().leased(),"screen replacement cannot revoke Storage writer");
    auto& late=*datasetResultMemory(request);late={};late.revision=request.token;late.count=1;std::strcpy(late.rows[0].title,"LATE");done={request,DatasetError::None};
    check(ui.commitDatasetRead(done)==OperationStatus::StaleContent && ui.data().hasBrowse(),"old page cannot overwrite Logs");
    check(ui.discardDatasetRead(done)==OperationStatus::Ok && !ui.mapFrame().leased(),"stale completion returns writer only after completion");
    ui.setScreen({ScreenId::Settings,DisplayMode::View,3,common,0,ActionResult::None});
    request={};request.token=13;request.screen_generation=3;request.action=DatasetAction::VerifyIndex;request.purpose=DatasetPurpose::Verify;request.id.assign("test-new");buffer={};
    check(ui.beginDatasetRead(request,buffer)==OperationStatus::Ok,"maintenance borrows the same fixed back");request.buffer=buffer;
    IndexProgress progress{};progress.token=13;progress.phase=IndexPhase::TM;progress.percent=42;progress.verifying=true;
    check(ui.updateDatasetProgress(progress)==OperationStatus::Ok && ui.data().datasets().applying && ui.data().datasets().progress.percent==42,
          "UI owns progress separately from Storage output under construction");
    auto& failed=*datasetResultMemory(request);failed={};failed.revision=13;failed.error=DatasetError::IndexMismatch;
    done={request,DatasetError::IndexMismatch};
    check(ui.commitDatasetRead(done)==OperationStatus::Ok && !ui.data().datasets().applying && ui.data().datasets().error==DatasetError::IndexMismatch,
          "maintenance failure publishes control status then returns the loan");
    // UI creates requests from its selected row, so App needs no title/list copy.
    common.editDatasets().loading=false;common.editDatasets().count=1;common.editDatasets().rows[0].id.assign("test-seven");common.editDatasets().revision=20;
    ui.setScreen({ScreenId::Settings,DisplayMode::View,4,common,0,ActionResult::None});
    ui.keyPressed(UiKey::Enter,1); // Operation
    while(ui.snapshot().selected_setting!=static_cast<unsigned>(SettingId::Dataset))ui.keyPressed(UiKey::Down,2);
    ui.keyPressed(UiKey::Enter,3);ui.keyPressed(UiKey::Enter,4); // Dataset menu -> list
    auto action=ui.snapshot().action;
    ui.updateContent({DisplayMode::View,4,common,action.action_id,ActionResult::Applied});
    ui.keyPressed(UiKey::Enter,5);action=ui.snapshot().action;
    check(action.dataset_action==DatasetAction::Detail && action.dataset_id.view()=="test-seven","UI resolves row ID before notifying App");
}

#include <cstring>
#include <stdexcept>
#include "task/storage/DatasetSelection.h"
#include "task/ui/PlacePresentation.h"
#include "task/ui/GpsUiModel.h"

using namespace geoputer;
namespace {
void check(bool value,const char* text) {if(!value)throw std::runtime_error(text);}
Settings selectedSettings() {
    auto value=defaultSettings();value.dataset=1;value.log_interval=2;value.brightness=4;
    std::strcpy(value.wordbook_language,"en-us");return value;
}
void transitions() {
    Preferences::records.clear();Preferences::fail_write=Preferences::fail_open=false;
    Preferences::records["other/component"]={1,2,3};
    SettingsStore store;check(store.begin(),"selection test NVS opens");
    const auto selected=selectedSettings();
    for(const auto reason:{PlaceState::NoCard,PlaceState::DatasetMissing,PlaceState::MetadataError,
                          PlaceState::VersionError,PlaceState::DataError,PlaceState::EncodingError,PlaceState::TooManyChunks,PlaceState::OutOfMemory}) {
        StorageSnapshot state{};state.settings=selected;state.settings_state=SettingsState::Loaded;
        state.log.open=true;state.log.buffered=64;state.log.session_id=123;
        DatasetId id{};id.assign("jp-n03-y2026-1");check(store.save(selected,id),"selected configuration saved before the failure");
        const auto writes=Preferences::write_calls;
        check(releaseUnusableDataset(store,state,reason),"active failure is released");
        auto expected=selected;setSettingValue(expected,static_cast<unsigned>(SettingId::Dataset),0);
        check(sameSettings(state.settings,expected) && state.settings_state==SettingsState::Saved && !state.nvs_error,
              "release clears selection and language while preserving other settings");
        check(state.dataset_failure.reason==reason && !state.dataset_failure.save_failed,
              "diagnostic preserves reason independently of selection");
        check(state.log.open && state.log.buffered==64 && state.log.session_id==123,"automatic GIS release does not stop or discard GPS logging");
        SettingsStore reboot;Settings restored{};
        check(reboot.begin() && reboot.load(restored)==SettingsState::Loaded && sameSettings(restored,expected),"next boot restores None rather than the failed dataset");
        const auto revision=state.dataset_failure.revision;
        check(!releaseUnusableDataset(store,state,reason) && state.dataset_failure.revision==revision && Preferences::write_calls==writes+1,
              "subsequent events do not rewrite NVS or repeatedly notify the same failed selection");
        char message[40];formatDatasetRelease(state.dataset_failure,message,sizeof(message));
        check(std::strlen(message)<=29 && std::strstr(message,"DATASET") && std::strstr(message,"NONE"),"failure notification fits native display");
    }
    for(const auto state_value:{PlaceState::Ready,PlaceState::NoFix,PlaceState::NotFound,PlaceState::Searching,PlaceState::Waiting,PlaceState::Unselected}) {
        StorageSnapshot state{};state.settings=selected;const auto writes=Preferences::write_calls;
        check(!releaseUnusableDataset(store,state,state_value) && sameSettings(state.settings,selected) && Preferences::write_calls==writes,
              "GPS loss, an empty region, and nonfailure states never release selection");
    }
    check(Preferences::records["other/component"]==std::vector<unsigned char>({1,2,3}),"release does not erase other NVS namespaces");
}
void failedPersistence() {
    SettingsStore store;check(store.begin(),"NVS opens for failure test");
    StorageSnapshot state{};state.settings=selectedSettings();DatasetId id{};id.assign("jp-n03-y2026-1");check(store.save(state.settings,id),"old selection persists");
    Preferences::fail_write=true;
    check(releaseUnusableDataset(store,state,PlaceState::DataError) && !state.settings.dataset && !state.settings.wordbook_language[0] &&
          state.dataset_failure.save_failed && state.settings_state==SettingsState::Failed && state.nvs_error!=0,
          "NVS error still releases RAM selection and exposes persistence failure");
    Settings restored{};check(store.load(restored)==SettingsState::Loaded && restored.dataset==1,"failed save is never reported as persisted None");
    const auto writes=Preferences::write_calls;
    check(!releaseUnusableDataset(store,state,PlaceState::DataError) && Preferences::write_calls==writes,"failed persistence does not create a write retry loop");
    Preferences::fail_write=false;
}
void presentation() {
    UiData data{};data.settings=defaultSettings();data.settings.screen_timeout=4;
    data.storage.card_state=CardState::Mounted;data.gps.reception=GpsReception::Fix;data.gps.fix.valid=true;
    data.dataset.state=PlaceState::Unselected;
    data.storage.dataset_failure={12,PlaceState::DatasetMissing,false};
    GpsUiModel model;model.tick(1000);
    check(model.setScreen({ScreenId::LivePlace,DisplayMode::View,1,data,0,ActionResult::None})==OperationStatus::Ok,"notice screen initialized");
    check(model.datasetFailureVisible() && placeIssue(model.data())==PlaceState::Unselected && mainError(data)==nullptr,
          "error notification is separate from steady None state");
    model.tick(5999);check(model.datasetFailureVisible(),"notice remains for five seconds");
    model.takeRedraw();model.tick(6000);check(!model.datasetFailureVisible() && model.takeRedraw(),"notice expiration redraws normal None display");
    data.storage.dataset_failure.revision=2;data.revision++;
    model.updateContent({DisplayMode::Background,1,data,0,ActionResult::None});model.tick(100000);
    model.updateContent({DisplayMode::View,1,data,0,ActionResult::None});model.tick(100001);
    check(model.datasetFailureVisible(),"a failure while display is off is visible after wake");
    data.storage.dataset_failure.save_failed=true;data.storage.card_state=CardState::NoCard;
    check(!std::strcmp(mainError(data),"DATASET CLEAR SAVE FAILED"),"NVS release failure remains visible even with SD absent");
    data.storage.dataset_failure.save_failed=false;check(!std::strcmp(mainError(data),"SD NOT READY"),"SD fault remains independently visible");
}
}
void testDatasetSelection() {transitions();failedPersistence();presentation();}

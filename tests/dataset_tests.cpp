#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unistd.h>
#include "task/storage/dataset/DatasetMetadata.h"
#include "task/storage/SettingsStore.h"
#include "task/ui/UiModel.h"
#include "task/ui/DatasetPresentation.h"
using namespace geoputer;
namespace {
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
const std::string common="[dataset]\nversion=1\nmap=area.glc\ndefault_language=ja\n";
const std::string ja="[ja]\ntitle=日本\ndescription=日本の地名\nwordbook=ja.giswordbook\n";
const std::string en="[en]\ntitle=Japan\nwordbook=en.giswordbook\n";
struct Observer : StorageObserver {
    unsigned depth=0, accesses=0;
    void sdBegin() override {++depth;++accesses;}
    void sdEnd() override {check(depth>0,"SD access scopes balance");--depth;}
    void cacheState(RgCacheState,bool) override {}
};
void metadata() {
    char pattern[]="/tmp/geoputer-metadata-XXXXXX";
    const auto directory=::mkdtemp(pattern);check(directory,"temp directory");
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::filesystem::remove_all(path);}} cleanup{directory};
    auto write=[&](const std::string& text){std::ofstream file(std::string(directory)+"/dataset.ini",std::ios::binary);file<<text;};
    auto read=[&](const std::string& text){write(text);return readDatasetMetadata(directory,UiLanguage::Japanese);};
    check(readDatasetMetadata(directory,UiLanguage::Japanese).error==DatasetError::Missing,"missing metadata detected without file guessing");
    write(ja+common+en);
    auto result=readDatasetMetadata(directory,UiLanguage::English,"ja");
    check(result.error==DatasetError::None && result.title=="Japan" && result.language=="ja" && result.wordbook=="ja.giswordbook", "UI translation and Wordbook language are independent, regardless of section order");
    check(result.languages.size()==2,"language choices retained without expanding translations");
    check(readDatasetMetadata(directory,UiLanguage::English,"fr").error==DatasetError::Missing,"a saved language must never silently fall back");
    result=read(common+ja);
    check(result.error==DatasetError::None && result.language=="ja", "empty language resolves dataset default");
    check(readDatasetMetadata(directory,UiLanguage::English).title=="日本","missing UI translation falls back to dataset default");
    Observer observer;
    check(readDatasetMetadata(directory,UiLanguage::Japanese,"",&observer).error==DatasetError::None && observer.accesses && !observer.depth,"metadata observes file I/O and closes every access scope");
    check(read(common+ja+ja).error==DatasetError::Metadata,"duplicate section rejected");
    check(read(common+ja+"title=duplicate\n").error==DatasetError::Metadata,"duplicate known key rejected");
    check(readDatasetMetadata(directory,UiLanguage::Japanese,"",&observer).error==DatasetError::Metadata && !observer.depth,"invalid metadata also closes SD access scopes");
    check(read(common+"[ja]\ntitle=Name\n").error==DatasetError::Metadata,"missing Wordbook rejected");
    check(read("[dataset]\nversion=2\nmap=a.glc\ndefault_language=ja\n"+ja).error==DatasetError::Version,"unsupported version distinguished");
    check(read("[dataset]\nversion=1\nmap=../area.glc\ndefault_language=ja\n"+ja).error==DatasetError::Metadata,"directory traversal rejected");
    check(read(common+"[ja]\ntitle=Name\nwordbook=/names.giswordbook\n").error==DatasetError::Metadata,"absolute Wordbook path rejected");
    check(read(common+ja+"unknown=value\n# comment\n").error==DatasetError::None,"unknown keys and full-line comments accepted");
    check(read(common+"[ja]\ntitle=A=B # literal\nwordbook=a.giswordbook").title=="A=B # literal","first equal splits value; no inline comments; final newline optional");
    const auto longline=common+"[ja]\ntitle=T\nwordbook=ja.giswordbook\ndescription=";
    check(read(longline+std::string(243,'x')).error==DatasetError::None,"255 payload bytes accepted");
    check(read(longline+std::string(244,'x')).error==DatasetError::Metadata,"256 bytes rejected instead of truncating");
    check(read(common+ja+std::string("description=\0x",14)).error==DatasetError::Metadata,"embedded NUL rejected");
    check(read(common+"[ja]\ntitle=\xc0\x80\nwordbook=ja.giswordbook\n").error==DatasetError::Metadata,"overlong UTF-8 rejected");
    std::string crlf;for(char c:common+ja) {if(c=='\n')crlf+='\r';crlf+=c;}
    check(read(std::string("\xef\xbb\xbf")+crlf).error==DatasetError::None,"BOM and CRLF supported");
    check(read(common+"[ja]\ntitle=\xe6\x97\nwordbook=ja.giswordbook\n").error==DatasetError::Metadata,"truncated UTF-8 rejected");
}
void savedSelection() {
    Preferences::records.clear();Preferences::fail_write=false;
    SettingsStore store;check(store.begin(),"NVS initialized");
    Settings selected=defaultSettings();selected.dataset=99;std::strcpy(selected.wordbook_language,"en-us");
    check(store.save(selected),"number and language commit together");
    SettingsStore reboot;Settings restored{};
    check(reboot.begin() && reboot.load(restored)==SettingsState::Loaded && sameSettings(selected,restored),"reboot restores exact number/language pair");
    Preferences::fail_write=true;selected.dataset=2;std::strcpy(selected.wordbook_language,"ja");
    check(!store.save(selected),"save error returned");Preferences::fail_write=false;
    check(reboot.load(restored)==SettingsState::Loaded && restored.dataset==99 && !std::strcmp(restored.wordbook_language,"en-us"),"failed save preserves old persisted pair");
    const uint8_t legacy[]={0x47,0x50,0x02,0x07,0,2,1,1,2,0,1,0,0,0,0,0};
    uint8_t bytes[16];std::memcpy(bytes,legacy,16);uint32_t hash=2166136261u;for(unsigned i=0;i<12;++i)hash=(hash^bytes[i])*16777619u;for(unsigned i=0;i<4;++i)bytes[12+i]=hash>>(8*i);
    check(decodeSettings(bytes,16,restored) && restored.dataset==1 && !restored.wordbook_language[0] && restored.language==UiLanguage::English,"old unused mode index migrates to 01/default, preserving UI language");
    check(!validDatasetLanguage("en--us") && !validDatasetLanguage("dataset") && !validDatasetLanguage("EN") && !validDatasetLanguage(std::string(32,'a').c_str()),"bounded identifiers validated");
}
void navigationAndGate() {
    RecordingSnapshot rec{};LogSnapshot log{};
    check(datasetLogStopped(rec,log),"clean STOPPED permits changing");
    rec.state=RecordingState::Paused;check(!datasetLogStopped(rec,log),"PAUSED is not stopped");
    rec.state=RecordingState::Stopped;
    log.closing=true;check(!datasetLogStopped(rec,log),"close acknowledgement must complete");log.closing=false;
    log.buffered=1;check(!datasetLogStopped(rec,log),"unsaved records block reboot");log.buffered=0;
    rec.pending=true;check(!datasetLogStopped(rec,log),"App pending sample blocks reboot");rec.pending=false;
    UiModel model;UiData data{};data.settings=defaultSettings();
    check(model.setScreen({ScreenId::Settings,DisplayMode::View,1,data,0,ActionResult::None})==OperationStatus::Ok,"settings opens");
    model.keyPressed(UiKey::Enter,1);for(unsigned i=0;i<5;++i)model.keyPressed(UiKey::Down,2+i);model.keyPressed(UiKey::Enter,8);
    check(model.snapshot().action.kind==ActionKind::Dataset && model.snapshot().action.dataset_action==DatasetAction::Open,"Dataset opens list rather than editing an old choice index");
    auto publish=[&](){const auto action=model.snapshot().action;data.revision++;data.datasets.revision=model.data().datasets.revision+1;model.updateContent({DisplayMode::View,1,data,action.action_id,ActionResult::Applied});};
    data.datasets.count=1;data.datasets.rows[0].number=2;std::strcpy(data.datasets.rows[0].title,"Tokyo");publish();
    model.keyPressed(UiKey::Enter,9);check(model.snapshot().action.dataset_action==DatasetAction::Detail,"Enter opens details");
    data.datasets={};data.datasets.detail=true;data.datasets.number=2;std::strcpy(data.datasets.info.language,"ja");std::strcpy(data.datasets.info.description,"説明文");publish();
    const auto count=datasetDetailRows(data.datasets,true);
    for(unsigned i=0;i<count-2;++i)model.keyPressed(UiKey::Down,10+i);
    model.keyPressed(UiKey::Enter,40);model.keyPressed(UiKey::Right,41);
    check(model.snapshot().action.dataset_action==DatasetAction::Language && model.editing(),"language edits use L/R within settings");
    data.datasets.loading=true;publish();
    model.keyPressed(UiKey::Escape,42);
    check(model.snapshot().action.dataset_action==DatasetAction::CancelLanguage && !std::strcmp(model.snapshot().action.settings.wordbook_language,"ja"),"Esc restores the pre-edit language draft without saving");
    data.datasets.loading=false;std::strcpy(data.datasets.info.language,"ja");publish();model.keyPressed(UiKey::Down,43);model.keyPressed(UiKey::Enter,44);
    check(model.snapshot().action.dataset_action==DatasetAction::Apply,"last detail row requests validated apply/restart");
    data.datasets.loading=data.datasets.applying=true;publish();model.keyPressed(UiKey::Escape,45);model.tick(100000);
    check(model.snapshot().page==SettingsPage::DatasetDetail && model.snapshot().action.kind==ActionKind::None,"committed apply cannot be cancelled or put to sleep");
    UiModel boot;UiData fatal{};fatal.settings=defaultSettings();fatal.boot.complete=true;fatal.boot.fatal_dataset=true;
    boot.setScreen({ScreenId::Boot,DisplayMode::View,1,fatal,0,ActionResult::None});
    for(auto key:{UiKey::Enter,UiKey::Escape,UiKey::OpenSettings,UiKey::Up,UiKey::Other}) {boot.keyPressed(key,1000);boot.tick(100000);}
    check(boot.snapshot().action.kind==ActionKind::None && boot.mode()==DisplayMode::View,"fatal missing-dataset screen never exits or sleeps");
}
}
void testDatasets() {metadata();savedSelection();navigationAndGate();}

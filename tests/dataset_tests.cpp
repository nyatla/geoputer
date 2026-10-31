#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unistd.h>
#include "task/storage/dataset/DatasetMetadata.h"
#include "task/storage/SettingsStore.h"
#include "task/ui/GpsUiModel.h"
#include "task/storage/dataset/SelectedDataset.h"
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
// The test snapshot deliberately uses owning strings, outside the parser.
struct MetadataSnapshot {
    std::string map,wordbook,title,description,language;
    DatasetLanguages languages;
    PlaceDisplayFormat display;
    DatasetError error;
};
MetadataSnapshot readMetadata(std::string_view directory,UiLanguage ui,const char* language="",StorageObserver* observer=nullptr) {
    DatasetMetadata fixed;MetadataSnapshot snapshot;char title[256]{},description[256]{};
    readDatasetMetadataInto(directory,ui,language,fixed,observer,{title,sizeof(title),description,sizeof(description)},0,&snapshot.languages);
    snapshot.map=fixed.map;snapshot.wordbook=fixed.wordbook;snapshot.language=fixed.language;
    snapshot.title=title;snapshot.description=description;snapshot.display=fixed.display;snapshot.error=fixed.error;
    return snapshot;
}
void metadata() {
    char pattern[]="/tmp/geoputer-metadata-XXXXXX";
    const auto directory=::mkdtemp(pattern);check(directory,"temp directory");
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::filesystem::remove_all(path);}} cleanup{directory};
    auto write=[&](const std::string& text){std::ofstream file(std::string(directory)+"/dataset.ini",std::ios::binary);file<<text;};
    auto read=[&](const std::string& text){write(text);return readMetadata(directory,UiLanguage::Japanese);};
    check(readMetadata(directory,UiLanguage::Japanese).error==DatasetError::Missing,"missing metadata detected without file guessing");
    const std::string map127=std::string(123,'m')+".glc",book127=std::string(115,'w')+".giswordbook";
    auto named=[&](const std::string& map,const std::string& book) {
        return read("[dataset]\nversion=1\nmap="+map+"\ndefault_language=ja\n[ja]\ntitle=Test\nwordbook="+book+"\n");
    };
    check(named(map127,book127).error==DatasetError::None,"127-byte filenames accepted including extension");
    check(named("x"+map127,book127).error==DatasetError::Metadata && named(map127,"x"+book127).error==DatasetError::Metadata,
          "128-byte map/book filenames rejected without truncation");
    const auto unicode127=std::string(120,'m')+"日.glc";
    check(named(unicode127,book127).error==DatasetError::None && named("x"+unicode127,book127).error==DatasetError::Metadata,
          "filename limit counts UTF-8 bytes without splitting characters");
    check(read(common+ja+"[en]\ntitle=Test\nwordbook=x"+book127+"\n").error==DatasetError::Metadata,
          "unselected language filenames obey the same capacity");
    SelectedDataset session;session.number=1;
    check(session.setNames(map127,book127) && !std::strcmp(session.map_name,map127.c_str()) && !std::strcmp(session.wordbook_name,book127.c_str()),
          "Storage session owns two terminated fixed filenames");
    check(!session.setNames("x"+map127,book127) && !std::strcmp(session.map_name,map127.c_str()),"rejected names leave the session unchanged");
    check(session.file(DatasetFile::Map).resolve().view()=="/sd/dataset/01/"+map127 &&
          session.file(DatasetFile::Index).resolve().view()=="/sd/dataset/01/_mapindex.dat","paths derive from integer ID and immutable root");
    session.number=99;check(session.file(DatasetFile::Wordbook).resolve().view()=="/sd/dataset/99/"+book127,"last supported ID retains two digits");
    session.clearPrepared();check(!session.map_name[0] && !session.wordbook_name[0] && session.number==99,"reset clears files while preserving selection ID");
    write(ja+common+en);
    auto result=readMetadata(directory,UiLanguage::English,"ja");
    check(result.error==DatasetError::None && result.title=="Japan" && result.language=="ja" && result.wordbook=="ja.giswordbook", "UI translation and Wordbook language are independent, regardless of section order");
    check(result.languages.size()==2,"language choices retained without expanding translations");
    check(readMetadata(directory,UiLanguage::English,"fr").error==DatasetError::Missing,"a saved language must never silently fall back");
    result=read(common+ja);
    check(result.error==DatasetError::None && result.language=="ja", "empty language resolves dataset default");
    check(readMetadata(directory,UiLanguage::English).title=="日本","missing UI translation falls back to dataset default");
    const std::string long_language(31,'a');
    write("[dataset]\nversion=1\nmap=area.glc\ndefault_language="+long_language+
          "\n["+long_language+"]\ntitle=Long language\nwordbook=names.giswordbook\n");
    const auto long_metadata=readMetadata(directory,UiLanguage::English);
    check(long_metadata.error==DatasetError::None && long_metadata.title=="Long language" &&
          long_metadata.language==long_language, "display-language view retains a long fallback across all parsed lines");
    check(read("[dataset]\nversion=1\nmap=area.glc\ndefault_language=ja\n["+std::string(32,'a')+"]\ntitle=Invalid\nwordbook=bad.giswordbook\n"+ja).error==DatasetError::Metadata,
          "32-byte section ID is rejected before copying to stack storage");
    check(read(common+"[ja]suffix\ntitle=Invalid\nwordbook=bad.giswordbook\n").error==DatasetError::Metadata,
          "section closing delimiter remains required");
    check(!result.display.specified, "omitted display formats retain the compatibility layout");
    write(common+ja+"display_line1=%0 %1%2%3\ndisplay_line2=%4%5\n"+en+
          "display_line1=%2, %0 %%\ndisplay_line2=\n");
    result=readMetadata(directory,UiLanguage::English,"ja");
    check(result.error==DatasetError::None && result.title=="Japan" && result.display.specified &&
          result.display.line(0)=="%0 %1%2%3" && result.display.line(1)=="%4%5",
          "format follows Wordbook language independently of the translated UI title");
    result=readMetadata(directory,UiLanguage::Japanese,"en");
    check(result.display.line(0)=="%2, %0 %%" && result.display.line(1).empty(), "selected language supplies its own format");
    result=read(common+ja+"display_line2=%0\n");
    check(result.error==DatasetError::None && result.display.specified && result.display.line(0).empty() && result.display.line(1)=="%0",
          "an omitted row is empty when the other format is explicit");
    check(read(common+ja+"display_line1=\ndisplay_line2=\n").display.specified, "two explicit empty rows do not request the default layout");
    static_assert(sizeof(PlaceDisplayFormat::lines[0])==128,"format includes its terminator");
    const std::string format127=std::string(125,'x')+"%0";
    result=read(common+ja+"display_line1="+format127+"\ndisplay_line2="+format127+"\n");
    check(result.error==DatasetError::None && result.display.line(0)==format127 && result.display.line(1)==format127 &&
          result.display.lines[0][127]=='\0' && result.display.lines[1][127]=='\0',"both 127-byte formats fit with separate terminators");
    for(const char* key:{"display_line1=","display_line2="}) {
        check(read(common+ja+key+"x"+format127+"\n").error==DatasetError::Metadata,"128-byte format rejected instead of truncated");
        check(read(common+ja+en+key+"x"+format127+"\n").error==DatasetError::Metadata,"unselected language also obeys format capacity");
    }
    const std::string utf127=std::string(124,'x')+"日";
    check(read(common+ja+"display_line1="+utf127+"\n").error==DatasetError::None &&
          read(common+ja+"display_line1=x"+utf127+"\n").error==DatasetError::Metadata,"UTF-8 format limit counts bytes without truncation");
    PlaceDisplayFormat fixed;
    check(fixed.setLine(0,format127) && !fixed.setLine(0,"x"+format127) && fixed.line(0)==format127 &&
          !fixed.setLine(2,"") && !fixed.setLine(1,std::string_view("a\0b",3)),"fixed output rejects invalid assignments without overwriting retained contents");
    session.display=fixed;
    session.clearPrepared();
    check(session.display.line(0).empty() && session.display.line(1).empty() && !session.display.specified,
          "reset removes prior fixed formats and explicit-layout flag");
    check(session.setLanguage(long_language) && std::string_view(session.language)==long_language && session.language[31]=='\0' &&
          !session.setLanguage(std::string(32,'a')) && std::string_view(session.language)==long_language,
          "selected language is fixed, terminated and never silently truncated");
    check(read(common+ja+"display_line1=%A\n").error==DatasetError::Metadata, "unsupported placeholder rejected during dataset validation");
    check(read(common+ja+en+"display_line2=end%\n").error==DatasetError::Metadata, "malformed unselected language format also rejected");
    check(read(common+ja+"display_line1=%0\ndisplay_line1=%1\n").error==DatasetError::Metadata, "duplicate format key rejected");
    write(common+ja);
    Observer observer;
    check(readMetadata(directory,UiLanguage::Japanese,"",&observer).error==DatasetError::None && observer.accesses && !observer.depth,"metadata observes file I/O and closes every access scope");
    check(read(common+ja+ja).error==DatasetError::Metadata,"duplicate section rejected");
    check(read(common+ja+"title=duplicate\n").error==DatasetError::Metadata,"duplicate known key rejected");
    check(readMetadata(directory,UiLanguage::Japanese,"",&observer).error==DatasetError::Metadata && !observer.depth,"invalid metadata also closes SD access scopes");
    check(read(common+"[ja]\ntitle=Name\n").error==DatasetError::Metadata,"missing Wordbook rejected");
    check(read("[dataset]\nversion=2\nmap=a.glc\ndefault_language=ja\n"+ja).error==DatasetError::Version,"unsupported version distinguished");
    check(read("[dataset]\nversion=1\nmap=../area.glc\ndefault_language=ja\n"+ja).error==DatasetError::Metadata,"directory traversal rejected");
    check(read(common+"[ja]\ntitle=Name\nwordbook=/names.giswordbook\n").error==DatasetError::Metadata,"absolute Wordbook path rejected");
    check(read(common+ja+"unknown=value\n# comment\n").error==DatasetError::None,"unknown keys and full-line comments accepted");
    result=read(" \t[dataset] \t\n version \t= 1 \t\n map = area.glc \t\n default_language = ja \t\n"
                " \t[ja] \t\n title = 日本 \t\n description = " + std::string(200,'x') +
                " \t\n wordbook = ja.giswordbook \t\n display_line1 = %0 %1 \t\n display_line2 = %2 \t\n" + en);
    check(result.error==DatasetError::None && result.title=="日本" && result.map=="area.glc" &&
          result.description==std::string(200,'x') && result.wordbook=="ja.giswordbook" &&
          result.display.line(0)=="%0 %1" && result.display.line(1)=="%2",
          "trimmed line views are copied before the shared buffer is reused or the parser returns");
    check(validDatasetLanguage(std::string_view("ja=ignored",2)) &&
          !validDatasetLanguage(std::string_view("ja\0en",5)),
          "bounded language views need no terminator and reject embedded NUL");
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
    check(store.save(defaultSettings()),"clearing selection persists without an SD card");
    check(reboot.load(restored)==SettingsState::Loaded && !restored.dataset && !restored.wordbook_language[0],"unselected survives reboot with an empty language");
    selected=defaultSettings();std::strcpy(selected.wordbook_language,"ja");
    check(!validSettings(selected) && !store.save(selected),"unselected cannot retain a hidden language");
    uint8_t inconsistent[SettingsRecordSize];encodeSettings(selected,inconsistent);
    check(!decodeSettings(inconsistent,sizeof(inconsistent),restored),"even a checksummed unselected record with a language is rejected");
    selected.dataset=1;check(setSettingValue(selected,static_cast<unsigned>(SettingId::Dataset),0) && !selected.wordbook_language[0],"clearing the dataset also clears its language");
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
    GpsUiModel model;UiData data{};data.settings=defaultSettings();data.storage.card_state=CardState::Mounted;data.storage.settings_state=SettingsState::Loaded;
    check(model.setScreen({ScreenId::Settings,DisplayMode::View,1,data,0,ActionResult::None})==OperationStatus::Ok,"settings opens");
    model.keyPressed(UiKey::Enter,1);for(unsigned i=0;i<5;++i)model.keyPressed(UiKey::Down,2+i);model.keyPressed(UiKey::Enter,8);
    check(model.snapshot().page==SettingsPage::DatasetMenu && model.snapshot().action.kind==ActionKind::None,"Dataset opens its menu without reading or validating files");
    model.keyPressed(UiKey::Enter,8);
    check(model.snapshot().action.kind==ActionKind::Dataset && model.snapshot().action.dataset_action==DatasetAction::Open,"Select dataset opens the existing list");
    auto publish=[&](){const auto action=model.snapshot().action;data.revision++;data.editDatasets().revision=model.data().datasets().revision+1;model.updateContent({DisplayMode::View,1,data,action.action_id,ActionResult::Applied});};
    data.editDatasets().count=1;data.editDatasets().rows[0].number=2;std::strcpy(data.editDatasets().rows[0].title,"Tokyo");publish();
    model.keyPressed(UiKey::Enter,9);check(model.snapshot().action.dataset_action==DatasetAction::Detail,"Enter opens details");
    data.editDatasets()={};data.editDatasets().detail=true;data.editDatasets().number=2;data.editDatasets().info.language_count=2;std::strcpy(data.editDatasets().info.language,"ja");std::strcpy(data.editDatasets().info.description,"説明文");publish();
    const auto count=datasetDetailRows(data.editDatasets(),true);
    for(unsigned i=0;i<count-2;++i)model.keyPressed(UiKey::Down,10+i);
    model.keyPressed(UiKey::Enter,40);model.keyPressed(UiKey::Right,41);
    check(model.snapshot().action.dataset_action==DatasetAction::Language && model.editing(),"language edits use L/R within settings");
    data.editDatasets().loading=true;publish();
    model.keyPressed(UiKey::Escape,42);
    check(model.snapshot().action.dataset_action==DatasetAction::CancelLanguage && !std::strcmp(model.snapshot().action.settings.wordbook_language,"ja"),"Esc restores the pre-edit language draft without saving");
    data.editDatasets().loading=false;std::strcpy(data.editDatasets().info.language,"ja");publish();
    model.keyPressed(UiKey::Right,43);
    check(model.snapshot().action.dataset_action==DatasetAction::Language && model.editing(),"Wordbook language changes without an entry Enter");
    std::strcpy(data.editDatasets().info.language,"en");publish();
    check(model.editDirty(),"completed language preview remains dirty until row confirmation");
    model.keyPressed(UiKey::Down,44);
    check(!model.editing() && model.snapshot().selected_setting==count-1 && model.snapshot().action.kind==ActionKind::None &&
          !std::strcmp(model.data().datasets().info.language,"en"),"row movement confirms the language candidate without applying or saving the dataset");
    model.keyPressed(UiKey::Enter,44);
    check(model.snapshot().action.dataset_action==DatasetAction::Apply,"last detail row requests validated apply/restart");
    data.editDatasets().loading=data.editDatasets().applying=true;publish();model.keyPressed(UiKey::Escape,45);model.tick(100000);
    check(model.snapshot().page==SettingsPage::DatasetDetail && model.snapshot().action.kind==ActionKind::None,"committed apply cannot be cancelled or put to sleep");
    for (const auto error : {DatasetError::IndexFailed,DatasetError::IndexRestoreFailed,DatasetError::SaveFailed}) {
        data.editDatasets().loading=data.editDatasets().applying=false;data.editDatasets().error=error;publish();
        model.keyPressed(UiKey::Enter,100001);
        check(model.snapshot().action.dataset_action==DatasetAction::Apply,"index or selection-save failure allows explicit retry");
    }
    GpsUiModel boot;UiData missing{};missing.settings=defaultSettings();missing.boot.complete=true;
    missing.boot.dataset(PlaceState::DatasetMissing);
    boot.setScreen({ScreenId::Boot,DisplayMode::View,1,missing,0,ActionResult::None});
    boot.keyPressed(UiKey::Enter,1000);
    check(boot.snapshot().action.kind==ActionKind::ChangeScreen && boot.snapshot().action.target==ScreenId::LivePlace,
          "missing-dataset startup proceeds to normal Live Place");
}

void unselectedUi() {
    UiData data{};data.settings=defaultSettings();data.dataset.state=PlaceState::Unselected;
    data.storage.card_state=CardState::NoCard;data.storage.settings_state=SettingsState::Loaded;
    auto logger_data=data;logger_data.storage.card_state=CardState::Mounted;
    GpsUiModel logger;logger.setScreen({ScreenId::GpsLogger,DisplayMode::View,1,logger_data,0,ActionResult::None});
    logger.keyPressed(UiKey::Enter,1);logger.keyPressed(UiKey::Right,2);logger.keyPressed(UiKey::Enter,3);
    check(logger.snapshot().action.kind==ActionKind::Logging,"unselected retains logger controls");
    GpsUiModel model;model.setScreen({ScreenId::Settings,DisplayMode::View,1,data,0,ActionResult::None});
    uint32_t now=0;auto key=[&](UiKey k){model.keyPressed(k,++now);};
    auto publish=[&](){const auto action=model.snapshot().action;++data.revision;
        data.editDatasets().revision=model.data().datasets().revision+1;
        model.updateContent({DisplayMode::View,1,data,action.action_id,ActionResult::Applied});};
    key(UiKey::Enter);for(unsigned i=0;i<5;++i)key(UiKey::Down);key(UiKey::Enter);
    key(UiKey::Down);
    check(datasetMenuBlock(model.data(),1)==DatasetError::CardUnavailable,"cache menu knows SD is unavailable");
    key(UiKey::Enter);check(model.snapshot().page==SettingsPage::DatasetMenu && !model.confirming() && model.snapshot().action.kind==ActionKind::None,
        "disabled menu does not require trying a confirmation");
    key(UiKey::Up);key(UiKey::Enter);
    check(model.snapshot().action.dataset_action==DatasetAction::Open && model.snapshot().action.dataset_purpose==DatasetPurpose::Select,"offline ordinary selection still opens");
    data.editDatasets().count=1;publish();
    check(datasetRowBlock(model.data(),0)==DatasetError::AlreadyUnselected,"already unselected row is visibly disabled");
    key(UiKey::Enter);check(!model.confirming(),"disabled None row never opens a modal");
    data.settings.dataset=1;std::strcpy(data.settings.wordbook_language,"ja");data.dataset.state=PlaceState::DataError;publish();
    data.storage.settings_state=SettingsState::Unavailable;publish();key(UiKey::Enter);
    check(!model.confirming() && datasetRowBlock(model.data(),0)==DatasetError::SettingsUnavailable,"known NVS unavailability disables selection changes");
    data.storage.settings_state=SettingsState::Loaded;publish();
    key(UiKey::Enter);check(model.dialog().kind()==DialogKind::ClearDataset && model.dialog().actionEnabled(),"None is available without reading broken active data or SD");
    key(UiKey::Enter);check(model.snapshot().action.kind==ActionKind::None,"clear defaults to cancel");
    data.recording.state=RecordingState::Paused;publish();key(UiKey::Enter);
    check(!model.confirming() && datasetRowBlock(model.data(),0)==DatasetError::NotStopped,"paused logger disables None before trying");
    data.recording.state=RecordingState::Stopped;publish();key(UiKey::Enter);key(UiKey::Right);
    data.storage.busy=true;publish();key(UiKey::Enter);
    check(model.snapshot().action.kind==ActionKind::None,"snapshot changes invalidate an open clear confirmation");
    data.storage.busy=false;publish();key(UiKey::Enter);key(UiKey::Right);key(UiKey::Enter);
    check(model.snapshot().action.dataset_action==DatasetAction::Clear && model.snapshot().action.confirmed,"None emits only confirmed Clear");
    data.settings=defaultSettings();publish();key(UiKey::Escape);
    check(model.snapshot().page==SettingsPage::DatasetMenu,"None returns through selection list, with no independent clear menu");
    key(UiKey::Down);key(UiKey::Down);key(UiKey::Down);
    check(model.snapshot().selected_setting==2,"dataset menu has exactly three entries");
}
void cacheFlow(DatasetPurpose purpose) {
    UiData data{};data.settings=defaultSettings();data.dataset.state=PlaceState::Unselected;data.storage.card_state=CardState::Mounted;
    GpsUiModel model;model.setScreen({ScreenId::Settings,DisplayMode::View,1,data,0,ActionResult::None});
    uint32_t now=0;auto key=[&](UiKey k){model.keyPressed(k,++now);};
    auto publish=[&](){const auto action=model.snapshot().action;++data.revision;
        data.editDatasets().revision=model.data().datasets().revision+1;
        model.updateContent({DisplayMode::View,1,data,action.action_id,ActionResult::Applied});};
    key(UiKey::Enter);for(unsigned i=0;i<5;++i)key(UiKey::Down);key(UiKey::Enter);
    const auto entry=static_cast<unsigned>(purpose);for(unsigned i=0;i<entry;++i)key(UiKey::Down);
    check(datasetMenuBlock(data,entry)==DatasetError::None,"unselected active dataset does not disable cache maintenance");
    data.recording.state=RecordingState::Recording;publish();key(UiKey::Enter);
    check(model.snapshot().page==SettingsPage::DatasetMenu && model.snapshot().action.kind==ActionKind::None && datasetMenuBlock(model.data(),entry)==DatasetError::NotStopped,
        "recording grays cache entry and emits no request");
    data.recording.state=RecordingState::Stopped;publish();key(UiKey::Enter);
    check(model.snapshot().page==SettingsPage::DatasetList && model.snapshot().action.dataset_action==DatasetAction::Open &&
        model.snapshot().action.dataset_purpose==purpose,"maintenance opens a target list without opening a detail reader");
    data.editDatasets()={};data.editDatasets().purpose=purpose;data.editDatasets().count=2;
    data.editDatasets().rows[0].number=1;data.editDatasets().rows[0].error=DatasetError::Metadata;
    data.editDatasets().rows[1].number=2;std::strcpy(data.editDatasets().rows[1].title,"Candidate");publish();
    key(UiKey::Enter);check(!model.confirming() && model.snapshot().action.kind==ActionKind::None && datasetRowBlock(model.data(),0)==DatasetError::Metadata,
        "metadata failure known from listing is disabled with its reason");
    key(UiKey::Down);key(UiKey::Enter);
    check(model.confirming() && !model.confirmChoice() && model.datasetDialogNumber()==2 && model.snapshot().action.kind==ActionKind::None,
        "target selection opens Cancel-default confirmation without a Detail request");
    key(UiKey::Escape);check(model.snapshot().page==SettingsPage::DatasetList && model.snapshot().selected_setting==1,"cancel preserves the chosen target row");
    key(UiKey::Enter);key(UiKey::Right);key(UiKey::Enter);
    const auto action=model.snapshot().action;
    check(action.kind==ActionKind::Dataset && action.confirmed && action.dataset_action==(purpose==DatasetPurpose::Rebuild?DatasetAction::RebuildIndex:DatasetAction::VerifyIndex) &&
        action.dataset_purpose==purpose && action.browse_index==1 && action.browse_revision==data.editDatasets().revision,
        "confirmed maintenance identifies the exact list revision and target, for map and all languages");
    data.editDatasets().applying=data.editDatasets().loading=true;data.editDatasets().number=2;data.editDatasets().progress.phase=IndexPhase::TT;publish();
    key(UiKey::Escape);model.tick(1000000);
    check(model.snapshot().page==SettingsPage::DatasetIndex && model.snapshot().action.kind==ActionKind::None,"maintenance progress owns input and prevents sleep");
    data.editDatasets().applying=data.editDatasets().loading=false;data.editDatasets().error=DatasetError::IndexMismatch;data.editDatasets().progress.phase=IndexPhase::Failed;publish();
    check(model.snapshot().page==SettingsPage::DatasetIndex && !model.data().settings.dataset,"result does not select the maintenance target");
    key(UiKey::Enter);check(model.snapshot().page==SettingsPage::DatasetList && model.snapshot().action.dataset_action==DatasetAction::Open && model.snapshot().action.dataset_purpose==purpose,
        "result returns to the same kind of target list");
    data.editDatasets().error=DatasetError::None;publish();key(UiKey::Escape);
    check(model.snapshot().page==SettingsPage::DatasetMenu && model.snapshot().selected_setting==entry,"target list backs to its own menu entry");
    key(UiKey::Escape);check(model.snapshot().page==SettingsPage::Operation,"dataset menu backs to Operation");
}
}
void testDatasets() {
    metadata();savedSelection();navigationAndGate();unselectedUi();cacheFlow(DatasetPurpose::Rebuild);cacheFlow(DatasetPurpose::Verify);
}

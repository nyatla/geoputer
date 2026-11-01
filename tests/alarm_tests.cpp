#include "alarm/AlarmEngine.h"
#include "alarm/CellNeighborhood.h"
#include "task/storage/alarm/AlarmFile.h"
#include "task/ui/GpsUiModel.h"
#include "task/ui/AlarmText.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstdio>
using namespace geoputer;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
alarm::Observation observation(uint32_t code,unsigned agree=9) {return {code,uint8_t(agree),9};}
void engineTests() {
    alarm::Settings s;s.enabled=true;s.judgement=alarm::Judgement::Immediate;alarm::Engine engine;
    check(!engine.observe(observation(1),s),"first position quietly establishes baseline");
    check(engine.observe(observation(2,1),s) && engine.observe(observation(1,1),s),"immediate reports each center region crossing");
    check(!engine.observe(observation(1),s),"moving cells within the same region is not a crossing");
    check(!engine.observe(observation(0),s),"undefined code never becomes an alert destination");
    engine.reset();s.judgement=alarm::Judgement::Standard;engine.observe(observation(1),s);
    for(unsigned i=0;i<100;++i)check(!engine.observe(observation(i%2+1,6),s),"six votes and boundary oscillation never change the accepted region");
    check(engine.observe(observation(2,7),s),"standard changes at seven of nine cells without waiting for a timer");
    check(!engine.observe(observation(2),s) && !engine.observe(observation(1,6),s),"duplicate candidate and weak return crossing stay quiet");
    check(engine.observe(observation(1,7),s),"return crossing requires the same seven-cell threshold");
    check(engine.observe(observation(3,8),s),"third region uses the same threshold");
    check(!engine.observe({},s) && !engine.observe(observation(1,6),s),"loss does not fabricate a region or lower the threshold");
    check(engine.observe(observation(1,7),s),"recovered samples can establish a real crossing");
    engine.reset();s.judgement=alarm::Judgement::Confirmed;engine.observe(observation(1),s);
    check(!engine.observe(observation(2,8),s) && engine.observe(observation(2),s),"confirmed waits for all nine cells");
    auto bad=observation(3);bad.valid_points=8;check(!engine.observe(bad,s),"invalid vote totals cannot trigger");
    bad=observation(3,8);bad.valid_points=8;check(!engine.observe(bad,s),"missing cell does not reduce confirmed denominator");
    s.enabled=false;check(!engine.observe(observation(3),s),"disabled notifications stay silent");
    alarm::Cell cell;
    for(int unit:{512,4096,8192,100000})for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
        check(alarm::neighborCell(20*unit,70*unit,x,y,unit,unit*2,cell) && cell.x==20*unit+x && cell.y==70*unit+y,
              "neighbors are exactly one source cell for all resolutions and latitude");
    }
    check(alarm::neighborCell(1800,100,1,0,10,20,cell) && cell.x==-1799,"east neighbor wraps across date line");
    check(alarm::neighborCell(-1800,100,-1,0,10,20,cell) && cell.x==1799,"west neighbor wraps across date line");
    check(!alarm::neighborCell(0,1800,0,1,10,20,cell) && !alarm::neighborCell(0,-1800,0,-1,10,20,cell),"neighbors beyond either pole are unavailable");
    check(!alarm::neighborCell(0,0,1,0,0,20,cell),"invalid map resolution is rejected");
}
void soundTests() {
    alarm::SoundSession sound;sound.start(alarm::Tone::Double,3,1,0);
    check(sound.active(9999) && !sound.active(10000),"selected ten-second duration");
    sound.start(alarm::Tone::Double,3,1,20000);sound.start(alarm::Tone::Rising,3,1,28000);
    check(sound.active(37999) && !sound.active(38000),"re-alert extends selected duration");
    sound.stop();sound.start(alarm::Tone::Single,5,1,0);sound.start(alarm::Tone::Chime,3,1,59000);
    check(sound.active(59999) && !sound.active(60000),"re-alert never extends first-start hard minute cap");
    sound.start(alarm::Tone::Single,0,1,61000);check(sound.active(61001),"expired session starts a fresh cap");
    sound.stop();check(!sound.active(61002),"silence is immediate");
    sound.start(alarm::Tone::Single,3,1,0xfffffff0u);
    check(sound.active(uint32_t(0xfffffff0u+9999)) && !sound.active(uint32_t(0xfffffff0u+10000)),"audio timer handles tick wrap");
}
uint32_t fileCrc(const uint8_t* p,size_t count) {uint32_t c=~0u;while(count--){c^=*p++;for(int i=0;i<8;++i)c=(c>>1)^(0xedb88320u&uint32_t(-int32_t(c&1)));}return ~c;}
void checksum(uint8_t (&b)[AlarmFile::Bytes]) {const auto c=fileCrc(b,60);for(unsigned i=0;i<4;++i)b[60+i]=c>>(8*i);}
void fileTests(const char* root) {
    namespace fs=std::filesystem;fs::create_directories(fs::path(root)/"01");ReadFileSlot slot;
    AlarmFile file(0,slot,nullptr,root);auto p=file.load(1,4,123);
    check(p.state==alarm::State::Created && !p.settings.enabled && fs::file_size(fs::path(root)/"01/_alert.dat")==64,"missing profile creates fixed OFF file");
    p.settings.enabled=true;p.settings.duration=4;check(file.save(p),"profile transactional save");
    auto loaded=file.load(1,4,123);check(loaded.state==alarm::State::Ready && alarm::same(loaded.settings,p.settings),"profile round trip");
    uint8_t encoded[AlarmFile::Bytes];AlarmFile::encode(p,encoded);
    for(unsigned i=0;i<sizeof(encoded);++i) {encoded[i]^=1;auto bad=p;check(!AlarmFile::decode(encoded,bad),"any damaged byte fails checksum or header");encoded[i]^=1;}
    for(unsigned version:{1u,2u})for(unsigned judgement=0;judgement<3;++judgement) {
        AlarmFile::encode(p,encoded);encoded[4]=version;encoded[13]=3;encoded[14]=judgement;encoded[15]=version==1?5:0;checksum(encoded);
        {std::ofstream out(fs::path(root)/"01/_alert.dat",std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<char*>(encoded),sizeof(encoded));}
        auto legacy=file.load(1,4,123);
        check(legacy.state==alarm::State::Ready && legacy.settings.judgement==static_cast<alarm::Judgement>(judgement) &&
              legacy.settings.enabled==p.settings.enabled && legacy.settings.tone==p.settings.tone,"v1 preserves notification/tone and maps the three judgement choices");
        std::ifstream in(fs::path(root)/"01/_alert.dat",std::ios::binary);in.read(reinterpret_cast<char*>(encoded),sizeof(encoded));
        check(encoded[4]==3 && encoded[13]==0 && encoded[15]==0 && AlarmFile::decode(encoded,legacy),"legacy migration publishes valid version 3 with removed hierarchy and width fields zero");
    }
    encoded[15]=1;checksum(encoded);auto badReserved=p;check(!AlarmFile::decode(encoded,badReserved),"v3 rejects a nonzero removed-width field even with valid CRC");
    auto changed=file.load(1,3,456);check(changed.state==alarm::State::Repaired && !changed.settings.enabled,"replaced dataset or mismatched Wordbook metadata use the corrupt/OFF policy");
    {std::ofstream bad(fs::path(root)/"01/_alert.dat",std::ios::binary|std::ios::trunc);bad<<"broken";}
    auto repaired=file.load(1,3,456);check(repaired.state==alarm::State::Repaired && !repaired.settings.enabled,"truncated file reports repair and generates OFF");
    check(file.load(1,3,456).state==alarm::State::Ready,"repair leaves a valid new file");
    fs::create_directory(fs::path(root)/"01/_alert.tmp");
    repaired.settings.enabled=true;check(!file.save(repaired),"failed staging save is reported");
    fs::remove(fs::path(root)/"01/_alert.tmp");check(!file.load(1,3,456).settings.enabled,"failed save preserves previous OFF file");
    fs::remove(fs::path(root)/"01/_alert.dat");fs::create_directory(fs::path(root)/"01/_alert.dat");
    auto inaccessible=file.load(1,3,456);check(inaccessible.state==alarm::State::IoError && fs::is_directory(fs::path(root)/"01/_alert.dat"),"unreadable/non-file path is not deleted");
    check(file.load(2,3,456).state==alarm::State::IoError,"missing dataset directory is unavailable, not fabricated");
}
uint32_t hash(const uint8_t* p,size_t n){uint32_t v=2166136261u;for(size_t i=0;i<n;++i)v=(v^p[i])*16777619u;return v;}
void settingsTests() {
    auto s=defaultSettings();s.dataset=1;std::strcpy(s.wordbook_language,"ja");s.volume=4;
    uint8_t b[SettingsRecordSize];encodeSettings(s,b);Settings result{};
    check(decodeSettings(b,sizeof(b),result) && result.volume==4,"volume persists in NVS v5");
    for(auto level:{power::Level::None,power::Level::Normal,power::Level::Maximum}){
        s.power_level=level;encodeSettings(s,b);
        check(decodeSettings(b,sizeof(b),result) && result.power_level==level && result.volume==4,"power and volume share byte without changing record length");
    }
    s.power_level=power::Level::None;encodeSettings(s,b);
    b[2]=4;b[3]=8;auto v4sum=hash(b,sizeof(b)-4);for(unsigned i=0;i<4;++i)b[sizeof(b)-4+i]=v4sum>>(8*i);
    check(decodeSettings(b,sizeof(b),result) && result.power_level==power::Level::None && result.volume==4,"v4 migration preserves volume and defaults power to None");
    b[2]=5;b[3]=SettingCount;b[11]=0x18;auto invalid=hash(b,sizeof(b)-4);for(unsigned i=0;i<4;++i)b[sizeof(b)-4+i]=invalid>>(8*i);
    check(!decodeSettings(b,sizeof(b),result),"reject reserved power level");
    b[11]=0x20;invalid=hash(b,sizeof(b)-4);for(unsigned i=0;i<4;++i)b[sizeof(b)-4+i]=invalid>>(8*i);
    check(!decodeSettings(b,sizeof(b),result),"reject reserved upper bits");
    b[2]=3;b[3]=7;b[11]=0;auto crc=hash(b,sizeof(b)-4);for(unsigned i=0;i<4;++i)b[sizeof(b)-4+i]=crc>>(8*i);
    check(decodeSettings(b,sizeof(b),result) && result.volume==defaultSettings().volume && result.dataset==1,"v3 migration retains dataset/language and defaults volume");
}
void uiTests() {
    UiData data{};data.settings=defaultSettings();data.settings.dataset=1;data.dataset.state=PlaceState::Ready;
    data.storage.card_state=CardState::Mounted;data.storage.alarm.dataset=1;data.storage.alarm.depth=4;
    data.storage.alarm.identity=123;data.storage.alarm.state=alarm::State::Ready;
    GpsUiModel model;model.setScreen({ScreenId::Settings,DisplayMode::View,1,data,0,ActionResult::None});
    for(unsigned i=0;i<settingsRootIndex(SettingsRootItem::Alarm);++i)model.keyPressed(UiKey::Down,i+1);
    model.keyPressed(UiKey::Enter,20);check(model.snapshot().page==SettingsPage::Alarm,"root opens dataset alert settings");
    model.keyPressed(UiKey::Right,21);check(model.editDirty(),"alarm value changes directly");
    model.keyPressed(UiKey::Left,22);model.keyPressed(UiKey::Down,23);
    check(!model.editing() && model.snapshot().action.kind==ActionKind::None,"unchanged alarm does not rewrite its file on row movement");
    model.keyPressed(UiKey::Up,24);model.keyPressed(UiKey::Right,25);model.keyPressed(UiKey::Escape,26);
    check(!model.editing() && model.snapshot().page==SettingsPage::Alarm,"alarm Escape restores the item without leaving its page");
    model.keyPressed(UiKey::Right,27);model.keyPressed(UiKey::Down,28);
    check(model.snapshot().action.kind==ActionKind::SaveAlarm && model.snapshot().action.alarm_settings.enabled && model.snapshot().action.alarm_identity==123,"alarm editing produces file save command rather than NVS settings");
    check(AlarmRows==5 && alarmChoices(1)==3 && !std::strcmp(alarmLabel(1,true),"区域判定"),"one three-choice region judgement row, no width/test rows");
    char text[32];alarmValueText(1,1,true,text,sizeof(text));check(!std::strcmp(text,"標準"),"standard has a word label rather than a numeric threshold");
    GpsUiModel sound;sound.setScreen({ScreenId::Settings,DisplayMode::View,1,data,0,ActionResult::None});
    for(unsigned i=0;i<settingsRootIndex(SettingsRootItem::Alarm);++i)sound.keyPressed(UiKey::Down,40+i);
    sound.keyPressed(UiKey::Enter,50);for(unsigned i=0;i<AlarmSoundRow;++i)sound.keyPressed(UiKey::Down,51+i);
    check(sound.snapshot().action.kind==ActionKind::None,"opening alerts and selecting sound do not play");
    auto ack=[&]{const auto id=sound.snapshot().action.action_id;check(sound.updateContent({DisplayMode::View,1,data,id,ActionResult::Applied})==OperationStatus::Ok,"acknowledge tone preview");};
    sound.keyPressed(UiKey::Right,60);
    check(sound.snapshot().action.kind==ActionKind::PreviewAlarm && sound.snapshot().action.alarm_settings.tone==alarm::Tone::Rising && sound.editDirty(),"tone switch previews the candidate while notification is OFF without saving");
    ack();sound.setAudioPreview(true);sound.setAudioActive(true);sound.keyPressed(UiKey::Right,61);
    check(sound.snapshot().action.kind==ActionKind::PreviewAlarm && sound.snapshot().action.alarm_settings.tone==alarm::Tone::Falling,"preview playback does not consume next tone key as silence");
    ack();sound.keyPressed(UiKey::Escape,62);check(!sound.editing() && sound.snapshot().action.kind==ActionKind::None,"Escape cancels candidate rather than storing it");
    sound.keyPressed(UiKey::Right,63);ack();sound.keyPressed(UiKey::Down,64);
    check(sound.snapshot().action.kind==ActionKind::SaveAlarm && sound.snapshot().action.alarm_settings.tone==alarm::Tone::Rising,"leaving tone row commits the auditioned candidate once");
    GpsUiModel dark;dark.setScreen({ScreenId::Settings,DisplayMode::Background,1,data,0,ActionResult::None});
    PlaceRequest q{1,12,{35.68,140.02,0,true}};q.token=51;q.dataset_generation=data.dataset.generation;q.alarm_sample=true;q.work_bytes=config::IndexReadBufferBytes;
    map::BufferLease buffer{};check(dark.beginPlaceSearch(q,buffer)==OperationStatus::Ok,"background alert borrows existing UI back area");q.buffer=buffer;
    check(dark.discardPlaceSearch(q)==OperationStatus::Ok,"background loan returns without changing screen");
    dark.setAudioActive(true);dark.keyPressed(UiKey::OpenMap,30);
    check(dark.snapshot().action.kind==ActionKind::SilenceAlarm,"first key silences instead of running its normal action");
}
int main(int argc,char** argv) {
    try {check(argc==2,"test root required");engineTests();soundTests();fileTests(argv[1]);settingsTests();uiTests();
        std::puts("PASS: alarm raw-code boundaries, three region thresholds, adjacent cell grids, v1/v2 migration, tone-change previews, sound durations/cap/wrap, file repair/transaction failures, NVS migration, UI and dark-screen borrowing");
    } catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}

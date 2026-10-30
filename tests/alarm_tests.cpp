#include "alarm/AlarmEngine.h"
#include "task/storage/alarm/AlarmFile.h"
#include "task/ui/UiModel.h"
#include "task/ui/AlarmText.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstdio>
using namespace geoputer;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
alarm::Region region(uint32_t a,uint32_t b=20) {alarm::Region r;r.depth=2;r.path[0]=a;r.path[1]=b;return r;}
alarm::Observation observation(uint32_t code,unsigned agree=9) {return {region(code),uint8_t(agree),9};}
void engineTests() {
    alarm::Settings s;s.enabled=true;s.level=2;s.mode=alarm::Mode::Every;alarm::Engine engine;
    check(!engine.observe(observation(1),s,0),"first position only establishes baseline");
    check(engine.observe(observation(2,1),s,1) && engine.observe(observation(1,1),s,2),"every mode reports each center crossing");
    check(!alarm::same(region(1),region(2)),"same lower component in a different parent remains a different region");
    engine.reset();s.mode=alarm::Mode::Relieved;engine.observe(observation(1),s,0);
    check(engine.observe(observation(2,1),s,1),"relief reports first crossing immediately, not seven-point majority");
    for(unsigned i=2;i<100;++i)check(!engine.observe(observation(i%2+1,1),s,i),"A/B oscillation remains suppressed");
    engine.observe(observation(2),s,100);engine.observe(observation(1),s,1000);
    check(!engine.observe(observation(2),s,3100),"settling in opposite region restarts release timer");
    check(!engine.observe(observation(2),s,6100),"stable nine points quietly rearm");
    check(engine.observe(observation(1,1),s,6101),"a new crossing can notify after leaving boundary");
    check(engine.observe(observation(3,1),s,6102),"a third region is a new boundary");
    engine.reset();s.mode=alarm::Mode::Confirmed;engine.observe(observation(1),s,0);
    check(!engine.observe(observation(2,8),s,1) && engine.observe(observation(2),s,2),"confirmed mode waits for all nine points");
    check(!engine.observe({},s,3),"missing region cannot trigger an alert");
    engine.reset();s.mode=alarm::Mode::Relieved;engine.observe(observation(1),s,0);engine.observe(observation(2,1),s,1);
    engine.observe(observation(2),s,10);engine.unavailable();
    engine.observe(observation(2),s,5000);
    check(!engine.observe(observation(1,1),s,5001),"GPS interruption cannot count as continuous stable time");
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
void fileTests(const char* root) {
    namespace fs=std::filesystem;fs::create_directories(fs::path(root)/"01");ReadFileSlot slot;
    AlarmFile file(0,slot,nullptr,root);auto p=file.load(1,4,123);
    check(p.state==alarm::State::Created && !p.settings.enabled && fs::file_size(fs::path(root)/"01/_alert.dat")==64,"missing profile creates fixed OFF file");
    p.settings.enabled=true;p.settings.level=3;p.settings.duration=4;check(file.save(p),"profile transactional save");
    auto loaded=file.load(1,4,123);check(loaded.state==alarm::State::Ready && alarm::same(loaded.settings,p.settings),"profile round trip");
    uint8_t encoded[AlarmFile::Bytes];AlarmFile::encode(p,encoded);
    for(unsigned i=0;i<sizeof(encoded);++i) {encoded[i]^=1;auto bad=p;check(!AlarmFile::decode(encoded,bad),"any damaged byte fails checksum or header");encoded[i]^=1;}
    auto changed=file.load(1,3,456);check(changed.state==alarm::State::Repaired && !changed.settings.enabled,"replaced dataset and invalid hierarchy use same corrupt/OFF policy");
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
    UiModel model;model.setScreen({ScreenId::Settings,DisplayMode::View,1,data,0,ActionResult::None});
    for(unsigned i=0;i<settingsRootIndex(SettingsRootItem::Alarm);++i)model.keyPressed(UiKey::Down,i+1);
    model.keyPressed(UiKey::Enter,20);check(model.snapshot().page==SettingsPage::Alarm,"root opens dataset alert settings");
    model.keyPressed(UiKey::Right,21);check(model.editDirty(),"alarm value changes directly");
    model.keyPressed(UiKey::Left,22);model.keyPressed(UiKey::Down,23);
    check(!model.editing() && model.snapshot().action.kind==ActionKind::None,"unchanged alarm does not rewrite its file on row movement");
    model.keyPressed(UiKey::Up,24);model.keyPressed(UiKey::Right,25);model.keyPressed(UiKey::Escape,26);
    check(!model.editing() && model.snapshot().page==SettingsPage::Alarm,"alarm Escape restores the item without leaving its page");
    model.keyPressed(UiKey::Right,27);model.keyPressed(UiKey::Down,28);
    check(model.snapshot().action.kind==ActionKind::SaveAlarm && model.snapshot().action.alarm_settings.enabled && model.snapshot().action.alarm_identity==123,"alarm editing produces file save command rather than NVS settings");
    UiModel dark;dark.setScreen({ScreenId::Settings,DisplayMode::Background,1,data,0,ActionResult::None});
    PlaceRequest q{1,12,{35.68,140.02,0,true}};q.token=51;q.dataset_generation=data.dataset.generation;q.alarm_sample=true;q.work_bytes=config::IndexReadBufferBytes;
    map::BufferLease buffer{};check(dark.beginPlaceSearch(q,buffer)==OperationStatus::Ok,"background alert borrows existing UI back area");q.buffer=buffer;
    check(dark.discardPlaceSearch(q)==OperationStatus::Ok,"background loan returns without changing screen");
    dark.setAudioActive(true);dark.keyPressed(UiKey::OpenMap,30);
    check(dark.snapshot().action.kind==ActionKind::SilenceAlarm,"first key silences instead of running its normal action");
}
int main(int argc,char** argv) {
    try {check(argc==2,"test root required");engineTests();soundTests();fileTests(argv[1]);settingsTests();uiTests();
        std::puts("PASS: alarm boundaries/hierarchy, suppression and rearm, nine-point entry, sound durations/cap/wrap, file repair/transaction failures, NVS migration, UI and dark-screen borrowing");
    } catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}

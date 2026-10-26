#include <cstring>
#include <filesystem>
#include <fstream>
#include <signal.h>
#include <stdexcept>
#include <sys/resource.h>
#include <vector>
#include "task/storage/log/LogWriter.h"
#include "task/gps/GpsTime.h"
#include "task/ui/UiModel.h"

using namespace geoputer;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Activity : StorageObserver {
    unsigned depth = 0, accesses = 0;
    void sdBegin() override { if (!depth++) ++accesses; }
    void sdEnd() override { check(depth > 0,"balanced SD scope"); --depth; }
    void cacheState(RgCacheState, bool) override {}
};
std::vector<uint8_t> bytes(const std::string& path) {
    std::ifstream input(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
void fileTests(const std::string& directory) {
    Activity activity;
    LogWriter writer(directory+"/logs", &activity);
    uint64_t utc;
    check(nmeaEpochMs("141026", 15*3600000u+1000, utc),"valid absolute GPS date");
    check(writer.open(0x1234,utc-1000)==LogError::None,"create RAM log session");
    check(!std::filesystem::exists(directory+"/logs"),"no empty physical file at session start");
    LogCommand point{}; point.session_id=0x1234;
    point.fix={35.681641,140.027194,15*3600000u+1000,true};
    point.fix.fields=GpsTime|GpsAltitude|GpsSpeed|GpsCourse|GpsHdop|GpsSatellites;
    point.fix.utc_epoch_ms=utc; point.fix.altitude_m=-12.5; point.fix.speed_kmh=36;
    point.fix.course_deg=359.999f; point.fix.hdop=0.75; point.fix.satellites=12;
    const auto before=activity.accesses;
    for(unsigned n=1;n<=64;++n) {
        point.sample_seq=n;
        check(writer.append(point)==LogError::None,"RAM accepts 64 points");
    }
    check(activity.accesses==before,"RAM acceptance does not light SD");
    check(writer.append(point)==LogError::None && writer.snapshot().accepted==64,"duplicate latest sample is idempotent");
    point.sample_seq=65;
    check(writer.append(point)==LogError::BufferFull && writer.snapshot().buffered==64,"65th point cannot overwrite uncommitted records");
    check(writer.requestFlush(0x1234,63,false)==LogError::Invalid,"flush boundary must include all accepted points");
    check(writer.requestFlush(0x1234,64,false)==LogError::None,"pause accepts complete RAM boundary");
    // Force a partial OS write, then verify same-offset recovery has no duplicates.
    struct rlimit original{}, limited{};
    check(getrlimit(RLIMIT_FSIZE,&original)==0,"get file size limit"); limited=original; limited.rlim_cur=1000;
    auto old_signal=signal(SIGXFSZ,SIG_IGN);
    check(setrlimit(RLIMIT_FSIZE,&limited)==0,"install partial write limit");
    const bool failed=!writer.flush(10000);
    check(setrlimit(RLIMIT_FSIZE,&original)==0,"restore file size limit"); signal(SIGXFSZ,old_signal);
    check(failed && writer.snapshot().buffered==64 && writer.snapshot().saved==0 && writer.snapshot().error==LogError::Io,
          "partial save failure retains all accepted records");
    check(writer.flush(15000),"retry pending bytes at the same offset");
    check(writer.snapshot().buffered==0 && writer.snapshot().saved==64 && writer.snapshot().error==LogError::None,
          "confirmed persistence releases RAM and clears error");
    check(writer.path().find("20261015_000001_0.gps.dat")!=std::string::npos,"filename uses first point JST across midnight");
    auto file=bytes(writer.path());
    check(file.size()==512+64*32 && validLogHeader(file.data()),"512-byte header and 64 exact records after partial-write retry");
    check(logGet(file.data()+24,8)==utc-1000 && logGet(file.data()+16,8)==0x1234,"header keeps session start time and ID");
    for(unsigned n=0;n<64;++n) {
        const auto* record=file.data()+512+n*32;
        check(logGet(record+28,4)==logCrc32(record,28),"independent record CRC");
        check(logGet(record,8)==utc && logGet(record+8,4)==356816410 && logGet(record+12,4)==1400271940,"exact coordinate/time encoding");
        check(static_cast<int32_t>(logGet(record+16,4))==-12500 && logGet(record+20,2)==1000 && logGet(record+22,2)==0 &&
              logGet(record+24,2)==75 && record[26]==12 && record[27]==127,"signed altitude, units, course wrap and validity flags");
    }
    check(writer.requestFlush(0x1234,64,true)==LogError::None && writer.flush(16000),"stop flushes and closes");
    check(writer.path().empty() && writer.snapshot().name[0],"completed close releases path storage and preserves the filename snapshot");
    check(writer.open(0x1234,utc)==LogError::SessionExists,"persisted session cannot be overwritten");
    check(writer.open(0x1235,utc)==LogError::None,"new session after close");
    point.session_id=0x1235; point.sample_seq=1;
    check(writer.append(point)==LogError::None && writer.flush(17000),"save second session in same second");
    check(writer.path().find("_1.gps.dat")!=std::string::npos,"filename collisions advance suffix");
    check(writer.requestFlush(0x1235,1,true)==LogError::None && writer.flush(18000),"close second session");
    check(writer.open(0x1236,UINT64_MAX)==LogError::None,"unknown datetime allowed");
    point.session_id=0x1236; point.fix.fields=0;
    check(writer.append(point)==LogError::None && writer.flush(19000),"save point with unknown optional data");
    check(writer.path().find("undated_0000000000001236_0.gps.dat")!=std::string::npos,"unknown datetime stable session filename");
    file=bytes(writer.path()); const auto* record=file.data()+512;
    check(record[27]==1 && logGet(record,8)==UINT64_MAX && logGet(record+16,8)==0 && logGet(record+24,3)==0,
          "missing metadata uses validity flags and canonical empty values");
    check(activity.depth==0,"all physical I/O scopes balanced");
}
void uiTests() {
    for(auto screen:{ScreenId::GpsLogger,ScreenId::LivePlace,ScreenId::Map,ScreenId::Logs,ScreenId::LogDetail,ScreenId::Settings}) {
        for(auto state:{RecordingState::Stopped,RecordingState::Starting,RecordingState::Recording,RecordingState::Paused}) {
            for(unsigned choice=0;choice<(state==RecordingState::Stopped?1u:2u);++choice) {
                UiModel ui;UiData data{};data.settings=defaultSettings();data.storage.card_state=CardState::Mounted;
                data.recording.state=state;data.gps.reception=GpsReception::Fix;
                ui.setScreen({screen,DisplayMode::View,1,data,0,ActionResult::None});
                ui.keyPressed(screen==ScreenId::GpsLogger?UiKey::Enter:UiKey::GpsControl,1);
                const auto initial=state==RecordingState::Stopped?0u:1u;
                check(ui.confirming() && ui.dialog().choice()==initial,"GPS defaults to Start or Pause/Resume without Cancel item");
                ui.keyPressed(UiKey::Down,2);check(ui.dialog().choice()==initial,"vertical key cannot choose action");
                ui.keyPressed(UiKey::Left,3);
                for(unsigned n=0;n<choice;++n)ui.keyPressed(UiKey::Right,3);
                ui.keyPressed(UiKey::Enter,4);const auto a=ui.snapshot().action;
                const auto expected=state==RecordingState::Stopped?LogAction::Start:
                    choice==0?LogAction::Stop:state==RecordingState::Paused?LogAction::Resume:LogAction::Pause;
                check(a.kind==ActionKind::Logging && a.log_action==expected && a.confirmed,"GPS screen and G dispatch identical operations");
            }
            for(auto cancel:{UiKey::Escape,UiKey::Back}) {
                UiModel ui;UiData data{};data.settings=defaultSettings();data.storage.card_state=CardState::Mounted;data.recording.state=state;
                ui.setScreen({screen,DisplayMode::View,1,data,0,ActionResult::None});
                ui.keyPressed(UiKey::GpsControl,1);ui.keyPressed(cancel,2);
                check(!ui.confirming() && ui.snapshot().action.kind==ActionKind::None,"Esc/Del cancel without application request");
            }
            // Enter immediately after opening executes the intended default.
            UiModel ui;UiData data{};data.settings=defaultSettings();data.storage.card_state=CardState::Mounted;data.recording.state=state;
            ui.setScreen({screen,DisplayMode::View,1,data,0,ActionResult::None});
            ui.keyPressed(UiKey::GpsControl,1);ui.keyPressed(UiKey::Enter,2);
            const auto expected=state==RecordingState::Stopped?LogAction::Start:state==RecordingState::Paused?LogAction::Resume:LogAction::Pause;
            check(ui.snapshot().action.kind==ActionKind::Logging && ui.snapshot().action.log_action==expected,"Enter runs default without cursor movement");
        }
    }
    UiData data{};data.settings=defaultSettings();data.storage.card_state=CardState::NoCard;
    data.gps.reception=GpsReception::Fix;UiModel ui;ui.setScreen({ScreenId::GpsLogger,DisplayMode::View,1,data,0,ActionResult::None});
    ui.keyPressed(UiKey::Enter,1);check(ui.dialog().choices()==1 && !ui.dialog().choiceEnabled(0),"without SD sole Start action is disabled");
    ui.keyPressed(UiKey::Right,2);check(ui.dialog().choice()==0,"disabled Start cannot move to a hidden Cancel item");
    ui.keyPressed(UiKey::Enter,2);check(ui.confirming() && ui.snapshot().action.kind==ActionKind::None,"disabled Start cannot execute or close the action menu");
    data.recording.state=RecordingState::Paused;++data.revision;ui.updateContent({DisplayMode::View,1,data,0,ActionResult::None});
    check(!ui.confirming(),"state change invalidates old dialog");
    data.storage.log.closing=true;++data.revision;ui.updateContent({DisplayMode::View,1,data,0,ActionResult::None});
    ui.keyPressed(UiKey::Enter,3);ui.keyPressed(UiKey::Right,4);ui.keyPressed(UiKey::Enter,5);
    check(ui.snapshot().action.kind==ActionKind::None,"closing session disables operations");
    check(ui.confirming() && ui.dialog().choice()==1,"temporarily disabled Pause/Resume retains focus");
    data.storage.log.closing=false;++data.revision;ui.updateContent({DisplayMode::View,1,data,0,ActionResult::None});
    check(ui.dialog().choice()==1 && ui.dialog().choiceEnabled(1),"enabling actions cannot silently select Stop");
    ui.setBattery(72,true,true);ui.updateContent({DisplayMode::View,1,data,0,ActionResult::None});
    check(ui.battery().percent==72,"UI-owned battery survives update");

}
}
void testLogging() {
    check(logCrc32(reinterpret_cast<const uint8_t*>("123456789"),9)==0xcbf43926u,"standard CRC32 test vector");
    uint64_t time;
    check(!nmeaEpochMs("290225",0,time) && nmeaEpochMs("290224",0,time),"GPS calendar leap date validation");
    char directory[]="/tmp/geoputer-log-XXXXXX";
    check(mkdtemp(directory)!=nullptr,"temporary storage");
    try { fileTests(directory); uiTests(); } catch (...) { std::filesystem::remove_all(directory); throw; }
    std::filesystem::remove_all(directory);
}

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
    UiModel ui; UiData data{}; data.settings=defaultSettings();
    ui.setScreen({ScreenId::LivePlace,DisplayMode::View,1,data,0,ActionResult::None});
    ui.keyPressed(UiKey::F1,100);
    check(!ui.confirming() && ui.snapshot().action.kind==ActionKind::None,"F1 cannot start a stopped logger");
    data.recording.state=RecordingState::Recording;
    ui.updateContent({DisplayMode::View,1,data,0,ActionResult::None});
    ui.keyPressed(UiKey::F1,101); ui.keyPressed(UiKey::Left,102);
    check(ui.confirming() && !ui.confirmChoice(),"pause confirmation defaults to cancel; Left is ignored");
    ui.keyPressed(UiKey::Down,103); ui.keyPressed(UiKey::Enter,104);
    auto action=ui.snapshot().action;
    check(action.kind==ActionKind::Logging && action.log_action==LogAction::Pause && action.confirmed,"F1 confirms pause only");
    data.recording.state=RecordingState::Paused;
    ui.updateContent({DisplayMode::View,1,data,action.action_id,ActionResult::Applied});
    ui.keyPressed(UiKey::F1,105); check(ui.snapshot().action.kind==ActionKind::None,"F1 cannot resume");
    ui.setScreen({ScreenId::GpsLogger,DisplayMode::View,2,data,0,ActionResult::None});
    ui.keyPressed(UiKey::Enter,106); action=ui.snapshot().action;
    check(action.log_action==LogAction::Resume,"GPS Logger resumes recording");
    ui.updateContent({DisplayMode::View,2,data,action.action_id,ActionResult::Applied});
    ui.keyPressed(UiKey::Down,107); ui.keyPressed(UiKey::Enter,108);
    check(ui.confirming(),"GPS Logger stop requires confirmation");
    ui.keyPressed(UiKey::Escape,109); check(!ui.confirming(),"Esc cancels stop");
    ui.setBattery(72,true,true); ui.updateContent({DisplayMode::View,2,data,0,ActionResult::None});
    check(ui.battery().percent==72 && ui.battery().charging,"App updates preserve UI-owned battery sample");
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

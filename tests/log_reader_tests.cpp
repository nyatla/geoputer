#include "support/LogReaderFixture.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include "task/storage/log/LogReader.h"
#include "task/ui/UiModel.h"
#include "task/ui/KeyBindings.h"
#include "task/ui/LogPresentation.h"
using namespace geoputer;
namespace {
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
GpsFix fix(unsigned n) {
    GpsFix f{35.0+n*0.00001,140.0,0,true};
    f.fields=GpsTime|GpsAltitude|GpsSpeed|GpsCourse|GpsHdop|GpsSatellites;
    f.utc_epoch_ms=1789309921000ull+n*1000ull;
    f.altitude_m=-12.345f; f.speed_kmh=36; f.course_deg=183.20f; f.hdop=0.8f; f.satellites=12;
    return f;
}
void file(const std::string& path,unsigned count,uint64_t session=100,unsigned tail=0) {
    std::ofstream out(path,std::ios::binary); uint8_t header[512]; encodeLogHeader(header,session,UINT64_MAX);
    out.write(reinterpret_cast<char*>(header),512);
    for(unsigned n=0;n<count;++n) { uint8_t bytes[32]; check(encodeLogRecord(fix(n),bytes),"encode fixture"); out.write(reinterpret_cast<char*>(bytes),32); }
    for(unsigned n=0;n<tail;++n) out.put('x');
}
LogReadReply run(LogReader& reader,LogReadRequest request,LogWriter* writer=nullptr) {
    check(reader.begin(request),"accept read request");
    for(unsigned n=0;n<1000 && !reader.done();++n) reader.step(writer);
    check(reader.done(),"bounded scan completes"); return reader.result();
}
void statisticsTests(const std::string& dir) {
    std::filesystem::create_directory(dir);
    const char* name="20261027_000000_0.gps.dat";const auto path=dir+"/"+name;
    test_storage::LogReader reader(dir);LogReadRequest req{};req.kind=LogReadKind::Statistics;req.token=87;req.screen_generation=19;
    std::strcpy(req.name,name);
    file(path,0);auto result=run(reader,req);
    check(result.error==LogReadError::None && result.data.statistics.processed==0 && result.data.statistics.distance_m==0 && !result.data.statistics.has_speed,"empty statistics completes without invented measurements");
    file(path,25);check(reader.begin(req) && !reader.step(),"statistics yields at eight records");
    check(reader.progress().processed==8 && reader.progress().total==25 && reader.progress().token==87,"progress preserves job identity and bounded work");
    // Append after the range was captured: the result must not chase the writer.
    {std::ofstream append(path,std::ios::binary|std::ios::app);uint8_t bytes[32];encodeLogRecord(fix(25),bytes);append.write(reinterpret_cast<char*>(bytes),32);}
    while(!reader.done())reader.step();
    result=reader.result();
    const auto& stats=result.data.statistics;
    check(result.error==LogReadError::None && result.summary.records==25 && stats.processed==25,"appended suffix is outside the pinned statistics range");
    check(stats.distance_m>26.6 && stats.distance_m<26.8 && stats.has_speed && stats.max_speed_kmh==36 && stats.has_altitude && std::abs(stats.min_altitude_m+12.345)<0.002,"streaming distance and valid speed/altitude extrema");
    check(reader.begin(req) && !reader.step(),"start cancellable calculation");reader.cancel();
    check(reader.done() && reader.result().error==LogReadError::Cancelled && reader.releaseForReuse(),"cancellation closes the file before reuse");
    file(path,25);{std::fstream f(path,std::ios::binary|std::ios::in|std::ios::out);f.seekp(512+12*32+10);f.put('x');}
    result=run(reader,req);check(result.error==LogReadError::Corrupt && result.failed_record==12,"middle-record corruption never becomes successful partial statistics");
    req.session_id=999;file(path,1);check(run(reader,req).error==LogReadError::Changed,"statistics refuses a replaced session");req.session_id=0;
    LogStatisticsAccumulator kernel;GpsFix a{};a.valid=true;a.longitude=179.999;kernel.add(a);a.longitude=-179.999;kernel.add(a);
    check(kernel.value.distance_m>222 && kernel.value.distance_m<223 && !kernel.value.has_speed && !kernel.value.has_altitude,"antimeridian and missing optional fields");
    LogStatisticsAccumulator one;one.add(a);check(one.value.distance_m==0,"one point has zero distance");
    a.latitude=90;one.add(a);check(std::isfinite(one.value.distance_m),"polar distances remain finite");
    // A retained writer uses its existing FIL. Closing it during the scan is safe.
    LogWriter writer(dir);check(writer.open(654,UINT64_MAX)==LogError::None,"statistics active writer");
    for(unsigned n=0;n<25;++n) {LogCommand command{};command.session_id=654;command.sample_seq=n+1;command.fix=fix(n);check(writer.append(command)==LogError::None,"stats writer append");}
    check(writer.requestFlush(654,25,false)==LogError::None && writer.flush(1),"save statistics input");
    const auto active_name=std::filesystem::path(std::string(writer.path().view())).filename().string();std::strcpy(req.name,active_name.c_str());req.session_id=654;
    check(reader.begin(req) && !reader.step(&writer) && reader.progress().processed==8,"active writer statistics first batch");
    check(writer.requestFlush(654,25,true)==LogError::None && writer.flush(2),"close writer between statistics batches");
    while(!reader.done())reader.step(&writer);
    check(reader.result().error==LogReadError::None && reader.result().data.statistics.processed==25,"closed writer falls back to the pinned file without duplicate or missing points");
}
void statisticsUiTests() {
    UiData data{};data.settings=defaultSettings();auto& view=data.editBrowse();view.summary.records=25;
    UiModel properties;properties.setScreen({ScreenId::LogProperties,DisplayMode::View,1,data,0,ActionResult::None});
    properties.keyPressed(UiKey::Enter,1);
    check(properties.snapshot().action.browse_action==BrowseAction::OpenPoint,"properties Enter opens text");
    UiModel menu;menu.setScreen({ScreenId::LogProperties,DisplayMode::View,1,data,0,ActionResult::None});
    menu.keyPressed(mapPhysicalKey('m',false),1);check(menu.confirming() && menu.dialog().choice()==2,"M defaults to statistics");
    menu.keyPressed(UiKey::Up,2);check(menu.dialog().choice()==2,"log menu follows horizontal dialog convention");
    menu.keyPressed(UiKey::Left,3);check(menu.dialog().choice()==0,"unavailable map option is grey and skipped");
    menu.keyPressed(UiKey::Back,4);check(!menu.confirming() && menu.snapshot().action.kind==ActionKind::None,"Del cancels menu without action");
    menu.keyPressed(UiKey::LogMenu,5);menu.keyPressed(UiKey::Enter,6);
    check(menu.snapshot().action.browse_action==BrowseAction::Statistics,"M Enter requests statistics explicitly");
    view.loading=true;view.statistics_from_point=true;
    UiModel busy;busy.setScreen({ScreenId::LogStatistics,DisplayMode::View,1,data,0,ActionResult::None});
    busy.keyPressed(UiKey::Recalculate,1);check(busy.snapshot().action.kind==ActionKind::None,"R cannot overlap a running scan");
    busy.keyPressed(UiKey::Escape,2);check(busy.snapshot().action.target==ScreenId::LogPoint,"statistics Esc returns to caller while scan cancels");
    data.settings.screen_timeout=0;UiModel timer;timer.setScreen({ScreenId::LogStatistics,DisplayMode::View,1,data,0,ActionResult::None});
    timer.tick(10);timer.tick(60010);check(timer.snapshot().action.kind==ActionKind::None,"progress stays visible through the inactivity timeout");
    view.loading=false;timer.updateContent({DisplayMode::View,1,data,0,ActionResult::None});
    timer.tick(65009);check(timer.snapshot().action.kind==ActionKind::None,"full inactivity interval remains after statistics completes");
    timer.tick(65010);check(timer.snapshot().action.kind==ActionKind::Sleep,"automatic sleep resumes after completion");
}
void listTests(const std::string& dir) {
    std::filesystem::create_directory(dir);
    for(unsigned n=0;n<17;++n) { char name[64];std::snprintf(name,sizeof(name),"20261016_0100%02u_0.gps.dat",n);file(dir+"/"+name,n+1); }
    file(dir+"/undated_0000000000000064_0.gps.dat",1);
    std::ofstream(dir+"/ignored.txt").put('x');
    std::filesystem::create_directory(dir+"/directory.gps.dat");
    test_storage::LogReader reader(dir); LogReadRequest req{}; req.kind=LogReadKind::List;
    auto first=run(reader,req);
    check(first.count==6 && !first.has_newer && first.has_older && std::strcmp(first.data.list.names[0],"20261016_010016_0.gps.dat")==0,"newest six names, skipping other files and directories");
    check(first.data.list.total_files==18 && first.data.list.first_index==0 && first.data.list.records[0]==17 && first.data.list.records[5]==12,"list carries size-derived records and total without growing its payload");
    req.direction=LogListDirection::Older; std::strcpy(req.anchor,first.data.list.names[5]); auto second=run(reader,req);
    check(second.count==6 && second.has_newer && second.has_older && std::strcmp(second.data.list.names[0],"20261016_010010_0.gps.dat")==0,"older page uses bounded filename anchor");
    check(second.data.list.first_index==6 && second.data.list.records[0]==11 && second.data.list.records[5]==6,"sorted names and sizes move together on older pages");
    req.direction=LogListDirection::Newer; std::strcpy(req.anchor,second.data.list.names[0]); auto previous=run(reader,req);
    check(previous.count==first.count && !previous.has_newer && std::memcmp(previous.data.list.names,first.data.list.names,sizeof(first.data.list.names))==0,"newer returns the immediately previous page");
    req.direction=LogListDirection::Older; std::strcpy(req.anchor,second.data.list.names[5]); auto last=run(reader,req);
    check(last.count==6 && !last.has_older && last.has_newer && std::strncmp(last.data.list.names[5],"undated_",8)==0,"undated files follow known dates and last page ends");
    req={};req.kind=LogReadKind::List;req.count=4;
    const auto usb_first=run(reader,req);
    check(usb_first.count==4 && !usb_first.has_newer && usb_first.has_older,
          "USB list uses four rows without changing the normal six-row default");
    req.direction=LogListDirection::Older;std::strcpy(req.anchor,usb_first.data.list.names[3]);
    const auto usb_second=run(reader,req);
    check(usb_second.count==4 && std::strcmp(usb_second.data.list.names[0],"20261016_010012_0.gps.dat")==0,
          "USB older page advances exactly four filenames");
    req.direction=LogListDirection::Newer;std::strcpy(req.anchor,usb_second.data.list.names[0]);
    const auto usb_previous=run(reader,req);
    check(usb_previous.count==4 && !usb_previous.has_newer &&
          std::memcmp(usb_first.data.list.names,usb_previous.data.list.names,sizeof(usb_first.data.list.names))==0,
          "USB reverse paging returns the same four-row page");
    req.direction=LogListDirection::Newest;req.count=7;
    check(run(reader,req).error==LogReadError::Range,"oversized filename pages fail before scanning");
    req.count=0;
    req.direction=LogListDirection::Newest; req.anchor[0]=0;
    check(reader.begin(req) && !reader.step(),"directory scanning yields after ten entries"); reader.cancel();
    check(reader.done() && reader.result().error==LogReadError::Cancelled,"cancel closes unfinished scan");
    const std::string missing_directory=dir+"/missing";
    test_storage::LogReader missing(missing_directory); check(run(missing,req).count==0 && missing.result().error==LogReadError::None,"no logs directory is an empty list");
}
void sizeOnlyTests(const std::string& dir) {
    std::filesystem::create_directory(dir);
    auto bytes=[&](const char* name,size_t count) {std::ofstream f(dir+"/"+name,std::ios::binary);for(size_t n=0;n<count;++n)f.put('x');};
    bytes("c.gps.dat",512+5*32+31);bytes("b.gps.dat",100);bytes("a.gps.dat",512);
    bytes("d.gps.dat",0);std::filesystem::resize_file(dir+"/d.gps.dat",UINT32_MAX); // Sparse FAT32-limit fixture, no content read.
    test_storage::LogReader reader(dir);LogReadRequest request{};request.kind=LogReadKind::List;
    const auto reply=run(reader,request);
    check(reply.error==LogReadError::None && reply.count==4 && reply.data.list.total_files==4,"listing does not validate deliberately corrupt headers/records");
    check(reply.data.list.records[0]==LogMaxRecords && reply.data.list.records[1]==5 && reply.data.list.records[2]==0 && reply.data.list.records[3]==0,"size-only counts floor tails and guard short files");
    char display[27];logListDisplayName("20261026_120000_0.gps.dat",display);
    check(!std::strcmp(display,"20261026_120000_0"),"display omits the entire .gps.dat suffix");
    logListDisplayName("undated_0000000000000064_4294967295.gps.dat",display);
    check(std::strlen(display)==26 && !std::strcmp(display+23,"..."),"long names are elided after suffix removal");
}

void readTests(const std::string& dir) {
    std::filesystem::create_directory(dir); const std::string name="20261016_093012_0.gps.dat",path=dir+"/"+name;
    file(path,25,100,5);
    test_storage::LogReader reader(dir); LogReadRequest req{};req.kind=LogReadKind::Summary;std::strcpy(req.name,name.c_str());
    auto summary=run(reader,req);
    check(summary.error==LogReadError::None && summary.summary.records==25 && summary.summary.tail==5 && summary.summary.first_utc==fix(0).utc_epoch_ms && summary.summary.last_utc==fix(24).utc_epoch_ms,"summary validates endpoints and reports partial tail without truncation");
    check(std::filesystem::file_size(path)==512+25*32+5,"read leaves source file unchanged");
    req.kind=LogReadKind::Records;req.session_id=100;req.limit=25;req.count=10;req.start=20;
    auto end=run(reader,req);
    check(end.error==LogReadError::None && end.count==5 && end.eof,"final batch returns fewer than ten");
    GpsFix decoded{};check(decodeLogRecord(end.data.records[0],decoded) && decoded.utc_epoch_ms==fix(20).utc_epoch_ms && decoded.fields==126,"record decoder preserves fields and timestamps");
    req.start=25;end=run(reader,req);check(end.count==0 && end.eof,"snapshot limit permits zero-row EOF");
    req.start=26;check(run(reader,req).error==LogReadError::Range,"out of range is a failure");
    req.start=0;req.limit=26;check(run(reader,req).error==LogReadError::Changed,"shrunk file cannot silently change frozen upper limit");
    req.limit=25;req.session_id=101;check(run(reader,req).error==LogReadError::Changed,"replacement session is rejected");
    req.session_id=100;req.count=11;check(run(reader,req).error==LogReadError::Range,"request cap is ten");
    req.count=10;std::strcpy(req.name,"../escape.gps.dat");check(run(reader,req).error==LogReadError::InvalidName,"path traversal never opens a file");std::strcpy(req.name,name.c_str());
    { std::fstream out(path,std::ios::binary|std::ios::in|std::ios::out);out.seekp(512+12*32);out.put('x'); }
    req.start=10;auto bad=run(reader,req);
    check(bad.error==LogReadError::Corrupt && bad.failed_record==12 && bad.count==0,"corrupt record fails its batch without renumbering");
    req.count=1;req.start=11;check(run(reader,req).error==LogReadError::None,"one-record fallback can inspect valid neighbor");
    req.start=12;check(run(reader,req).error==LogReadError::Corrupt,"bad record retains its physical number");
    req.start=13;check(run(reader,req).error==LogReadError::None,"later valid records remain accessible");
    uint8_t record[32];encodeLogRecord(fix(0),record);record[27]&=~GpsAltitude;logPut(record+28,logCrc32(record,28),4);
    check(!decodeLogRecord(record,decoded),"valid CRC does not permit noncanonical invalid fields");
    file(path,1);{ std::fstream out(path,std::ios::binary|std::ios::in|std::ios::out);uint8_t header[512];encodeLogHeader(header,100,0);logPut(header+8,2,2);logPut(header+508,logCrc32(header,508),4);out.write(reinterpret_cast<char*>(header),512); }
    req.kind=LogReadKind::Summary;check(run(reader,req).error==LogReadError::Unsupported,"future file version is not parsed as v1");
    char text[32];formatLogDateTime(1789309921000ull,text,sizeof(text));check(std::strcmp(text,"2026-09-13 23:32:01")==0,"viewer dates use JST");
    formatLogDateTime(UINT64_MAX,text,sizeof(text));check(std::strcmp(text,"--")==0,"unknown time is never replaced with current time");
}
void activeTests(const std::string& dir) {
    LogWriter writer(dir);check(writer.open(100,0)==LogError::None,"active log opens");
    for(unsigned n=0;n<25;++n) { LogCommand p{};p.session_id=100;p.sample_seq=n+1;p.fix=fix(n);check(writer.append(p)==LogError::None,"active append"); }
    check(writer.flush(1000),"commit first active prefix");
    test_storage::LogReader reader(dir);LogReadRequest req{};req.kind=LogReadKind::Summary;
    std::strcpy(req.name,std::filesystem::path(std::string(writer.path().view())).filename().string().c_str());
    auto summary=run(reader,req,&writer);check(summary.summary.records==25,"active summary uses committed prefix");
    for(unsigned n=25;n<30;++n) { LogCommand p{};p.session_id=100;p.sample_seq=n+1;p.fix=fix(n);check(writer.append(p)==LogError::None,"new active points remain in RAM"); }
    check(run(reader,req,&writer).summary.records==25,"active RAM is excluded from viewer");
    check(writer.flush(2000),"flush after read restores writer position");
    req.kind=LogReadKind::Records;req.session_id=100;req.limit=25;req.start=20;req.count=10;
    auto fixed=run(reader,req,&writer);check(fixed.count==5 && fixed.eof,"appended points do not move existing viewer upper bound");
    check(writer.requestFlush(100,30,true)==LogError::None && writer.flush(3000),"active file can close after reader operations");
    req.kind=LogReadKind::Summary;check(run(reader,req,&writer).summary.records==30,"closed file uses independent read-only handle");
}
void uiTests() {
    UiModel ui;UiData data{};data.settings=defaultSettings();data.editBrowse().count=6;data.editBrowse().has_older=true;data.editBrowse().revision=1;
    std::strcpy(data.editBrowse().names[0],"20261016_093012_0.gps.dat");
    ui.setScreen({ScreenId::Logs,DisplayMode::View,1,data,0,ActionResult::None});
    for(unsigned i=0;i<5;++i)ui.keyPressed(UiKey::Down,i+1);
    ui.keyPressed(UiKey::Down,10);check(ui.snapshot().action.browse_action==BrowseAction::Older,"bottom Down requests older page");
    auto action=ui.snapshot().action;ui.updateContent({DisplayMode::View,1,data,action.action_id,ActionResult::Applied});
    ui.keyPressed(UiKey::Left,11);check(ui.snapshot().action.kind==ActionKind::None,"left/right remain limited to Settings");
    data.editBrowse().loading=true;ui.updateContent({DisplayMode::View,1,data,0,ActionResult::None});
    ui.keyPressed(UiKey::Escape,12);check(ui.snapshot().action.target==ScreenId::Menu,"Escape opens Menu directly during a pending read");
    data.editBrowse().loading=false;data.editBrowse().summary.records=25;data.editBrowse().target=9;
    ui.setScreen({ScreenId::LogPoint,DisplayMode::View,2,data,ui.snapshot().action.action_id,ActionResult::Applied});
    ui.keyPressed(UiKey::Right,13);check(ui.snapshot().action.browse_index==10,"point navigation crosses ten-record cache boundary");
    action=ui.snapshot().action;data.editBrowse().target=24;ui.updateContent({DisplayMode::View,2,data,action.action_id,ActionResult::Applied});
    ui.keyPressed(UiKey::Down,14);check(ui.snapshot().action.kind==ActionKind::None,"last record does not wrap to first");
    ui.keyPressed(UiKey::Escape,15);check(ui.snapshot().action.target==ScreenId::LogProperties,"point Escape returns to properties");
    action=ui.snapshot().action;
    data.editBrowse().summary.session_id=12;data.editBrowse().summary.records=25;
    data.settings.dataset=0;data.dataset.state=PlaceState::Unselected;
    data.editBrowse().summary.records=25;
    ui.setScreen({ScreenId::LogPoint,DisplayMode::View,3,data,action.action_id,ActionResult::Applied});
    ui.keyPressed(UiKey::ToggleView,20);
    check(ui.snapshot().action.kind==ActionKind::None,"V is unavailable without a dataset while text remains usable");
    data.editBrowse().target=12;
    ui.setScreen({ScreenId::LogPoint,DisplayMode::View,4,data,action.action_id,ActionResult::Applied});
    ui.keyPressed(UiKey::Up,20);action=ui.snapshot().action;check(action.browse_index==2,"up skips ten points");
    ui.updateContent({DisplayMode::View,4,data,action.action_id,ActionResult::Applied});
    ui.keyPressed(UiKey::FirstPoint,21);action=ui.snapshot().action;check(action.browse_index==0,"B requests first point");
    ui.updateContent({DisplayMode::View,4,data,action.action_id,ActionResult::Applied});
    ui.keyPressed(UiKey::LastPoint,22);action=ui.snapshot().action;check(action.browse_index==24,"E requests last point");
    ui.updateContent({DisplayMode::View,4,data,action.action_id,ActionResult::Applied});
    ui.keyPressed(UiKey::ToggleView,23);check(ui.snapshot().action.kind==ActionKind::None,"V cannot open unusable map");

}
struct DeleteActivity : StorageObserver {
    unsigned depth=0,accesses=0;
    void sdBegin() override {++depth;++accesses;}
    void sdEnd() override {check(depth>0,"balanced delete SD scope");--depth;}
    void cacheState(RgCacheState,bool) override {}
};
void deleteTests(const std::string& dir) {
    namespace fs=std::filesystem;
    fs::create_directory(dir);
    const std::string name="20261016_093012_0.gps.dat",path=dir+"/"+name;
    file(path,1);
    DeleteActivity activity;test_storage::LogReader reader(dir,&activity);
    LogReadRequest req{};req.kind=LogReadKind::Delete;req.token=71;req.screen_generation=9;
    std::strcpy(req.name,name.c_str());
    check(run(reader,req).error==LogReadError::Unconfirmed && fs::exists(path) && !activity.accesses,
          "unconfirmed deletion is rejected without touching SD");
    req.confirmed=true;
    auto removed=run(reader,req);
    check(removed.error==LogReadError::None && removed.request.token==71 && removed.request.screen_generation==9 &&
          !fs::exists(path) && !activity.depth,"confirmed delete removes only the chosen file and retains result identity");
    check(run(reader,req).error==LogReadError::NotFound,"disappeared log reports a distinct failure");
    file(path,1);std::strcpy(req.name,"../outside.gps.dat");
    check(run(reader,req).error==LogReadError::InvalidName && fs::exists(path),"delete rejects traversal");
    std::strcpy(req.name,"directory.gps.dat");fs::create_directory(dir+"/"+req.name);
    check(run(reader,req).error==LogReadError::InvalidName && fs::is_directory(dir+"/"+req.name),"delete never removes a directory");
    std::strcpy(req.name,name.c_str());
    if(::getuid()!=0) {
        fs::permissions(dir,fs::perms::owner_read|fs::perms::owner_exec);
        const auto failed=run(reader,req);
        fs::permissions(dir,fs::perms::owner_all);
        check(failed.error==LogReadError::DeleteFailed && fs::exists(path) && !activity.depth,
              "unlink failure preserves the file and reports a delete-specific error");
    }
    LogWriter writer(dir);check(writer.open(900,UINT64_MAX)==LogError::None,"writer for protected deletion");
    LogCommand initial{};initial.session_id=900;initial.sample_seq=1;initial.fix=fix(0);
    initial.fix.fields&=~GpsTime;initial.fix.utc_epoch_ms=UINT64_MAX;
    check(writer.append(initial)==LogError::None && writer.flush(500),"create and save an undated active log");
    const auto active_name=fs::path(std::string(writer.path().view())).filename().string();
    std::strcpy(req.name,active_name.c_str());const auto before=activity.accesses;
    check(run(reader,req,&writer).error==LogReadError::Active && fs::exists(std::string(writer.path().view())) && activity.accesses==before,
          "open recording cannot be deleted and protection performs no SD access");
    for(size_t n=0;n<std::strlen(req.name)-8;++n) if(req.name[n]>='a'&&req.name[n]<='z')req.name[n]-=32;
    check(run(reader,req,&writer).error==LogReadError::Active,"FAT32 case aliases cannot bypass active-file protection");
    std::strcpy(req.name,active_name.c_str());
    LogCommand sample{};sample.session_id=900;sample.sample_seq=2;sample.fix=fix(1);
    check(writer.append(sample)==LogError::None && writer.requestFlush(900,2,true)==LogError::None,"create closing session with unsaved data");
    check(run(reader,req,&writer).error==LogReadError::Active && writer.snapshot().buffered==1,"closing buffered log remains protected");
    std::strcpy(req.name,name.c_str());
    check(run(reader,req,&writer).error==LogReadError::None && writer.snapshot().buffered==1 && fs::exists(std::string(writer.path().view())),
          "deleting another log leaves the active writer and RAM queue intact");
    check(writer.flush(1000),"close protected log after saving retained records");
    std::strcpy(req.name,active_name.c_str());
    check(run(reader,req,&writer).error==LogReadError::None && !fs::exists(fs::path(dir)/active_name),"fully closed and saved file can be deleted");

    // Inclusive refresh keeps the old first row when it survives deletion.
    for(unsigned n=0;n<8;++n) {char row[64];std::snprintf(row,sizeof(row),"20261016_0100%02u_0.gps.dat",n);file(dir+"/"+row,1);}
    req={};req.kind=LogReadKind::List;auto first=run(reader,req);
    req.direction=LogListDirection::FromAnchor;std::strcpy(req.anchor,first.data.list.names[0]);
    auto refreshed=run(reader,req);
    check(refreshed.count==6 && !refreshed.has_newer && refreshed.has_older && !std::strcmp(refreshed.data.list.names[0],first.data.list.names[0]),
          "inclusive refresh preserves the current page start");
    LogReadRequest del{};del.kind=LogReadKind::Delete;del.confirmed=true;std::strcpy(del.name,first.data.list.names[0]);
    check(run(reader,del).error==LogReadError::None,"delete first row for anchored refresh");
    refreshed=run(reader,req);
    check(refreshed.count==6 && !std::strcmp(refreshed.data.list.names[0],first.data.list.names[1]),"removed page anchor still resumes at its next surviving row");
    std::strcpy(req.anchor,"00000000_000000_0.gps.dat");refreshed=run(reader,req);
    check(!refreshed.count && refreshed.has_newer,"empty old page signals a previous page for fallback");
    req.direction=LogListDirection::Newer;
    check(run(reader,req).count==6,"previous-page fallback can fill an emptied final page");
    check(!activity.depth,"all delete and refresh SD scopes are balanced");
}
void deleteUiTests() {
    UiData data{};data.settings=defaultSettings();data.editBrowse().count=3;data.editBrowse().selected=1;data.editBrowse().revision=7;
    for(unsigned n=0;n<3;++n)std::snprintf(data.editBrowse().names[n],LogNameBytes,"20261016_09301%u_0.gps.dat",n);
    UiModel ui;ui.setScreen({ScreenId::Logs,DisplayMode::View,1,data,0,ActionResult::None});
    ui.keyPressed(UiKey::DeleteLog,10);
    check(ui.confirming() && !ui.confirmChoice() && ui.snapshot().action.kind==ActionKind::None,"D opens confirmation with Cancel selected, no request");
    ui.keyPressed(UiKey::Enter,11);
    check(!ui.confirming() && ui.snapshot().action.kind==ActionKind::None,"default Enter cancels deletion");
    ui.keyPressed(UiKey::DeleteLog,12);ui.keyPressed(UiKey::Escape,13);
    check(!ui.confirming() && ui.snapshot().action.kind==ActionKind::None,"Esc/Del cancels without deleting");
    ui.keyPressed(UiKey::DeleteLog,14);ui.keyPressed(UiKey::Right,15);ui.keyPressed(UiKey::Enter,16);
    const auto action=ui.snapshot().action;
    check(action.kind==ActionKind::Browse && action.browse_action==BrowseAction::Delete && action.confirmed &&
          action.browse_index==1 && action.browse_revision==7,"only explicit Delete confirmation creates a request for the shown row/revision");
    data.editBrowse().loading=data.editBrowse().deleting=true;
    ui.updateContent({DisplayMode::View,1,data,action.action_id,ActionResult::Applied});
    for(auto key:{UiKey::Escape,UiKey::DeleteLog,UiKey::Enter,UiKey::OpenGpsLogger,UiKey::OpenSettings,UiKey::OpenLogs})ui.keyPressed(key,20);
    ui.tick(100);ui.tick(100000);
    check(ui.snapshot().action.kind==ActionKind::None,"accepted deletion cannot be abandoned by navigation, duplicate D or sleep");
    data.editBrowse().loading=data.editBrowse().deleting=false;data.editBrowse().active_mask=2;
    UiModel active;active.setScreen({ScreenId::Logs,DisplayMode::View,1,data,0,ActionResult::None});
    active.keyPressed(UiKey::DeleteLog,10);active.keyPressed(UiKey::Right,11);active.keyPressed(UiKey::Enter,12);
    check(!active.confirming() && active.snapshot().action.kind==ActionKind::None,"active row explains protection and cannot confirm Delete");
    data.editBrowse().active_mask=0;
    UiModel changed;changed.setScreen({ScreenId::Logs,DisplayMode::View,1,data,0,ActionResult::None});
    changed.keyPressed(UiKey::DeleteLog,10);++data.editBrowse().revision;
    changed.updateContent({DisplayMode::View,1,data,0,ActionResult::None});
    check(!changed.confirming() && changed.snapshot().action.kind==ActionKind::None,"refreshed list invalidates the shown delete target");
    data.editBrowse().count=0;UiModel empty;empty.setScreen({ScreenId::Logs,DisplayMode::View,1,data,0,ActionResult::None});
    empty.keyPressed(UiKey::DeleteLog,10);check(!empty.confirming(),"empty list offers no delete target");
}
}
void testLogReader() {
    const std::string oversized_directory(config::TemporaryPathBytes,'x');
    test_storage::LogReader oversized(oversized_directory);LogReadRequest oversized_request{};
    oversized_request.kind=LogReadKind::List;
    check(oversized.begin(oversized_request) && oversized.done() && oversized.result().error==LogReadError::Range,
          "oversized log directory fails as a bounded request");
    char root[]="/tmp/geoputer-reader-XXXXXX";check(mkdtemp(root)!=nullptr,"temporary read fixtures");
    try {statisticsUiTests();statisticsTests(std::string(root)+"/statistics");listTests(std::string(root)+"/pages"); sizeOnlyTests(std::string(root)+"/pages"+"-size-only");readTests(std::string(root)+"/read");activeTests(std::string(root)+"/active");deleteTests(std::string(root)+"/delete");uiTests();deleteUiTests();}
    catch(...) {std::filesystem::remove_all(root);throw;}
    std::filesystem::remove_all(root);
}

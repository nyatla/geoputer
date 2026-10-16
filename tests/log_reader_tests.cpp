#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include "task/storage/log/LogReader.h"
#include "task/ui/UiModel.h"
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
void listTests(const std::string& dir) {
    std::filesystem::create_directory(dir);
    for(unsigned n=0;n<17;++n) { char name[64];std::snprintf(name,sizeof(name),"20261016_0100%02u_0.gps.dat",n);file(dir+"/"+name,1); }
    file(dir+"/undated_0000000000000064_0.gps.dat",1);
    std::ofstream(dir+"/ignored.txt").put('x');
    std::filesystem::create_directory(dir+"/directory.gps.dat");
    LogReader reader(dir); LogReadRequest req{}; req.kind=LogReadKind::List;
    auto first=run(reader,req);
    check(first.count==6 && !first.has_newer && first.has_older && std::strcmp(first.data.names[0],"20261016_010016_0.gps.dat")==0,"newest six names, skipping other files and directories");
    req.direction=LogListDirection::Older; std::strcpy(req.anchor,first.data.names[5]); auto second=run(reader,req);
    check(second.count==6 && second.has_newer && second.has_older && std::strcmp(second.data.names[0],"20261016_010010_0.gps.dat")==0,"older page uses bounded filename anchor");
    req.direction=LogListDirection::Newer; std::strcpy(req.anchor,second.data.names[0]); auto previous=run(reader,req);
    check(previous.count==first.count && !previous.has_newer && std::memcmp(previous.data.names,first.data.names,sizeof(first.data.names))==0,"newer returns the immediately previous page");
    req.direction=LogListDirection::Older; std::strcpy(req.anchor,second.data.names[5]); auto last=run(reader,req);
    check(last.count==6 && !last.has_older && last.has_newer && std::strncmp(last.data.names[5],"undated_",8)==0,"undated files follow known dates and last page ends");
    req.direction=LogListDirection::Newest; req.anchor[0]=0;
    check(reader.begin(req) && !reader.step(),"directory scanning yields after ten entries"); reader.cancel();
    check(reader.done() && reader.result().error==LogReadError::Cancelled,"cancel closes unfinished scan");
    LogReader missing(dir+"/missing"); check(run(missing,req).count==0 && missing.result().error==LogReadError::None,"no logs directory is an empty list");
}
void readTests(const std::string& dir) {
    std::filesystem::create_directory(dir); const std::string name="20261016_093012_0.gps.dat",path=dir+"/"+name;
    file(path,25,100,5);
    LogReader reader(dir); LogReadRequest req{};req.kind=LogReadKind::Summary;std::strcpy(req.name,name.c_str());
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
    LogReader reader(dir);LogReadRequest req{};req.kind=LogReadKind::Summary;
    std::strcpy(req.name,std::filesystem::path(writer.path()).filename().string().c_str());
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
    UiModel ui;UiData data{};data.settings=defaultSettings();data.browse.count=6;data.browse.has_older=true;data.browse.revision=1;
    std::strcpy(data.browse.names[0],"20261016_093012_0.gps.dat");
    ui.setScreen({ScreenId::Logs,DisplayMode::View,1,data,0,ActionResult::None});
    for(unsigned i=0;i<5;++i)ui.keyPressed(UiKey::Down,i+1);
    ui.keyPressed(UiKey::Down,10);check(ui.snapshot().action.browse_action==BrowseAction::Older,"bottom Down requests older page");
    auto action=ui.snapshot().action;ui.updateContent({DisplayMode::View,1,data,action.action_id,ActionResult::Applied});
    ui.keyPressed(UiKey::Left,11);check(ui.snapshot().action.kind==ActionKind::None,"left/right remain limited to Settings");
    data.browse.loading=true;ui.updateContent({DisplayMode::View,1,data,0,ActionResult::None});
    ui.keyPressed(UiKey::Escape,12);check(ui.snapshot().action.target==ScreenId::Menu,"Escape leaves a pending read without blocking UI");
    data.browse.loading=false;data.browse.summary.records=25;data.browse.target=9;
    ui.setScreen({ScreenId::LogPoint,DisplayMode::View,2,data,ui.snapshot().action.action_id,ActionResult::Applied});
    ui.keyPressed(UiKey::Down,13);check(ui.snapshot().action.browse_index==10,"point navigation crosses ten-record cache boundary");
    action=ui.snapshot().action;data.browse.target=24;ui.updateContent({DisplayMode::View,2,data,action.action_id,ActionResult::Applied});
    ui.keyPressed(UiKey::Down,14);check(ui.snapshot().action.kind==ActionKind::None,"last record does not wrap to first");
    ui.keyPressed(UiKey::Escape,15);check(ui.snapshot().action.target==ScreenId::LogDetail,"point Escape returns to file detail");
}
}
void testLogReader() {
    char root[]="/tmp/geoputer-reader-XXXXXX";check(mkdtemp(root)!=nullptr,"temporary read fixtures");
    try {listTests(std::string(root)+"/pages");readTests(std::string(root)+"/read");activeTests(std::string(root)+"/active");uiTests();}
    catch(...) {std::filesystem::remove_all(root);throw;}
    std::filesystem::remove_all(root);
}

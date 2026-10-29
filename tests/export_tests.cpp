#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
#include "task/storage/export/LogExporter.h"
#include "task/storage/log/LogFormat.h"
using namespace geoputer;
namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
std::string contents(const std::filesystem::path& p) {
    std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};
}
void fixture(const std::filesystem::path& p,unsigned count=25,unsigned tail=0,bool corrupt=false) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p,std::ios::binary);uint8_t header[512];encodeLogHeader(header,100,0);
    out.write(reinterpret_cast<char*>(header),512);
    for(unsigned n=0;n<count;++n) {
        GpsFix fix{35.681641+n*1e-7,140.027194,0,true};
        fix.fields=n%3==2?0:126;fix.utc_epoch_ms=n%3==1?951782400001ull:0;
        fix.altitude_m=-12.345f;fix.speed_kmh=36;fix.course_deg=140.25f;fix.hdop=0.8f;fix.satellites=12;
        uint8_t record[32];check(encodeLogRecord(fix,record),"encode export fixture");
        if(corrupt && n==12)record[8]^=1;
        out.write(reinterpret_cast<char*>(record),32);
    }
    for(unsigned n=0;n<tail;++n)out.put('x');
}
ExportSnapshot run(LogExporter& exporter,const char* name,ExportFormat format) {
    check(exporter.begin(name,format),"export command accepted");
    uint32_t before=0;
    for(unsigned n=0;n<1000 && exporter.running();++n) {
        exporter.step();
        const auto& job=exporter.snapshot();
        check(job.processed-before<=config::ExportRecordsPerStep,"export yields within its record budget");before=job.processed;
    }
    check(!exporter.running(),"export completes within bounded steps");
    return exporter.snapshot();
}
}
void testExport() {
    check(fatFsPath(0,"/sd/logs/a.gps.dat").view()=="0:/logs/a.gps.dat" &&
          fatFsPath(12,"/sd/dataset/01/日本.glc").view()=="12:/dataset/01/日本.glc",
          "shared native path conversion retains drive, directories and UTF-8 bytes");
    check(fatFsPath(255,"/sd/"+std::string(250,'x')).view()=="255:/"+std::string(250,'x') &&
          fatFsPath(0,"/sd/").view()=="0:/", "native path assembly preserves 255-byte boundary, full drive range and root");
    bool too_long=false;
    try{(void)fatFsPath(255,"/sd/"+std::string(251,'x'));}catch(const std::length_error&){too_long=true;}
    check(too_long,"oversized native path must fail without truncation");
    for(const auto* path:{"/sd", "/sd-other/a", "relative.dat", ""}) {
        bool rejected=false;
        try { (void)fatFsPath(0,path); } catch(const std::runtime_error&) { rejected=true; }
        check(rejected,"FatFs paths must start with the exact SD mount prefix");
    }
    check(storageNativePath(3,"/tmp/host/path").view()=="/tmp/host/path","host paths bypass FatFs translation");
    LogExporter oversized(std::string(config::TemporaryPathBytes,'x'));
    check(oversized.begin("a.gps.dat",ExportFormat::Gpx) && oversized.snapshot().phase==ExportPhase::Failed &&
          oversized.snapshot().error==ExportError::InvalidName,"oversized export path fails before creating files");
    LogFile oversized_file;errno=0;
    check(!oversized_file.openRead(std::string(config::TemporaryPathBytes,'x')) && !oversized_file.opened() &&
          errno==ENAMETOOLONG,"log file overflow returns an error without an open handle");
    namespace fs=std::filesystem;
    const char* configured=std::getenv("GEOPUTER_EXPORT_FIXTURES");
    const fs::path root=configured?fs::path(configured):fs::temp_directory_path()/"geoputer-export-native";
    fs::create_directories(root);
    const char* name="20261017_093012_0.gps.dat";
    fixture(root/"logs"/name);
    const auto source=contents(root/"logs"/name);
    LogExporter converter(root.string());
    for(auto format:{ExportFormat::Gpx,ExportFormat::Kml,ExportFormat::Csv}) {
        auto job=run(converter,name,format);
        check(job.phase==ExportPhase::Complete && job.output_created && job.processed==25 && job.total==25,"three formats complete every point");
        const auto path=root/"export"/(std::string(name)+exportExtension(format));
        check(fs::exists(path) && !fs::exists(root/"export"/(std::string(".")+name+exportExtension(format)+".part")),"only completed output is published under appended extension");
        const auto output=contents(path);
        check(run(converter,name,format).error==ExportError::Exists && contents(path)==output,"duplicate output never overwrites existing content");
    }
    check(contents(root/"logs"/name)==source,"conversion never changes the binary source");
    const std::string longest=std::string(39,'a')+".gps.dat";
    check(longest.size()==47,"maximum supported name fixture");fixture(root/"logs"/longest,1);
    check(run(converter,longest.c_str(),ExportFormat::Gpx).phase==ExportPhase::Complete &&
          fs::exists(root/"export"/(longest+".gpx")),"47-byte source retains all 51 output filename bytes");
    fixture(root/"logs"/"single.gps.dat",1);
    check(run(converter,"single.gps.dat",ExportFormat::Kml).phase==ExportPhase::Complete,"one-point KML uses Point geometry");
    check(contents(root/"export"/"single.gps.dat.kml").find("<Point>")!=std::string::npos,"single point is not an invalid one-vertex LineString");
    check(run(converter,"../escape.gps.dat",ExportFormat::Gpx).error==ExportError::InvalidName,"traversal rejected");
    check(run(converter,name,static_cast<ExportFormat>(77)).error==ExportError::InvalidName,"unknown format rejected");
    fixture(root/"logs"/"bad.gps.dat",25,0,true);
    auto corrupt=run(converter,"bad.gps.dat",ExportFormat::Gpx);
    check(corrupt.error==ExportError::Corrupt && corrupt.failed_record==12 && corrupt.processed==12,"interior CRC failure keeps the physical record index");
    check(!fs::exists(root/"export"/"bad.gps.dat.gpx") && !fs::exists(root/"export"/".bad.gps.dat.gpx.part"),"failed output is not exposed and owned temporary file is removed");
    fixture(root/"logs"/"tail.gps.dat",2,5);
    check(run(converter,"tail.gps.dat",ExportFormat::Csv).error==ExportError::Tail,"partial trailing record is not silently discarded");
    fixture(root/"logs"/"empty.gps.dat",0);
    check(run(converter,"empty.gps.dat",ExportFormat::Kml).error==ExportError::Empty,"empty logs do not create invalid KML geometry");
    fixture(root/"logs"/"header.gps.dat",1);
    {std::fstream out(root/"logs"/"header.gps.dat",std::ios::in|std::ios::out|std::ios::binary);out.put('x');}
    check(run(converter,"header.gps.dat",ExportFormat::Gpx).error==ExportError::Header,"bad header rejected");
    fixture(root/"logs"/"future.gps.dat",1);
    {uint8_t h[512];encodeLogHeader(h,100,0);logPut(h+8,2,2);logPut(h+508,logCrc32(h,508),4);
     std::fstream out(root/"logs"/"future.gps.dat",std::ios::in|std::ios::out|std::ios::binary);out.write(reinterpret_cast<char*>(h),512);}
    check(run(converter,"future.gps.dat",ExportFormat::Gpx).error==ExportError::Unsupported,"future format is not decoded as v1");
    fixture(root/"logs"/"stale.gps.dat",1);
    std::ofstream(root/"export"/".stale.gps.dat.gpx.part")<<"PREEXISTING";
    check(run(converter,"stale.gps.dat",ExportFormat::Gpx).error==ExportError::PartialExists &&
          contents(root/"export"/".stale.gps.dat.gpx.part")=="PREEXISTING","preexisting partial files are preserved");
    fixture(root/"logs"/"limited.gps.dat",1);
    LogExporter limited(root.string(),100);
    check(run(limited,"limited.gps.dat",ExportFormat::Gpx).error==ExportError::Limit &&
          !fs::exists(root/"export"/"limited.gps.dat.gpx"),"FAT32 output size limit is enforced before publishing");
    fixture(root/"logs"/"changed.gps.dat",300);
    check(converter.begin("changed.gps.dat",ExportFormat::Gpx),"start truncation fixture");converter.step();
    fs::resize_file(root/"logs"/"changed.gps.dat",512);
    // The first buffered stdio read can still contain cached bytes; reading
    // beyond the frozen size must eventually fail and cannot publish a file.
    while(converter.running())converter.step();
    check(converter.snapshot().error==ExportError::Read,"short read aborts conversion");
    check(!fs::exists(root/"export"/"changed.gps.dat.gpx"),"short read cannot leave a completed output");
    const auto blocked=root/"blocked";fs::create_directories(blocked/"logs");fixture(blocked/"logs"/name,1);
    std::ofstream(blocked/"export")<<"not a directory";
    LogExporter no_directory(blocked.string());
    check(run(no_directory,name,ExportFormat::Csv).error==ExportError::Directory &&
          contents(blocked/"export")=="not a directory","file masquerading as export directory is preserved");
    if(!configured)fs::remove_all(root);
}

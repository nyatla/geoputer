#include "LogExporter.h"
#include "../log/LogReader.h"
#include "../log/LogFormat.h"
#include <sys/stat.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace geoputer {
namespace {
bool utcText(uint64_t ms, char (&out)[32]) {
    if(ms>=253402300800000ull) return false;
    const uint64_t z=ms/86400000ull+719468;
    const unsigned era=z/146097, doe=z%146097;
    const unsigned yoe=(doe-doe/1460+doe/36524-doe/146096)/365;
    unsigned year=era*400+yoe;
    const unsigned doy=doe-(365*yoe+yoe/4-yoe/100), mp=(5*doy+2)/153;
    const unsigned day=doy-(153*mp+2)/5+1, month=mp<10?mp+3:mp-9;
    year+=month<=2;
    const unsigned seconds=(ms/1000)%86400;
    std::snprintf(out,sizeof(out),"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",year,month,day,
                  seconds/3600,seconds/60%60,seconds%60,static_cast<unsigned>(ms%1000));
    return true;
}
bool absent(const std::string& path) {
    struct stat info{};
    errno=0;
    return ::stat(path.c_str(),&info)!=0 && errno==ENOENT;
}
}
bool LogExporter::begin(const char* name, ExportFormat format) {
    if(running() || input_.opened() || output_.opened()) return false;
    state_={};state_.phase=ExportPhase::Running;state_.format=format;
    buffered_=0;written_=0;started_=false;owns_partial_=false;target_.clear();partial_.clear();
    if(!validLogName(name) || !validExportFormat(format)) {fail(ExportError::InvalidName);return true;}
    std::strcpy(state_.name,name);
    target_=root_+"/export/"+name+exportExtension(format);
    partial_=root_+"/export/."+name+exportExtension(format)+".part";
    return true;
}
bool LogExporter::open() {
    if(!input_.openRead(root_+"/logs/"+state_.name)) {fail(ExportError::Read);return false;}
    uint64_t size=0;
    if(!input_.size(size) || size<512 || size>UINT32_MAX || !input_.read(read_buffer_,512)) {
        fail(ExportError::Header);return false;
    }
    if(logGet(read_buffer_+8,2)!=1) {fail(ExportError::Unsupported);return false;}
    if(!validLogHeader(read_buffer_)) {fail(ExportError::Header);return false;}
    if((size-512)%32) {fail(ExportError::Tail);return false;}
    state_.total=(size-512)/32;
    if(!state_.total) {fail(ExportError::Empty);return false;}
    const auto directory=root_+"/export";
    struct stat info{};
    if(::stat(directory.c_str(),&info)) {
        if(errno!=ENOENT || ::mkdir(directory.c_str(),0775)) {fail(ExportError::Directory);return false;}
    } else if(!S_ISDIR(info.st_mode)) {fail(ExportError::Directory);return false;}
    if(!absent(target_)) {fail(ExportError::Exists);return false;}
    if(!output_.open(partial_,true)) {fail(errno==EEXIST?ExportError::PartialExists:ExportError::Write);return false;}
    owns_partial_=true;
    switch(state_.format) {
        case ExportFormat::Gpx:
            std::snprintf(line_,sizeof(line_),"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<gpx version=\"1.1\" creator=\"GeoPuter\" xmlns=\"http://www.topografix.com/GPX/1/1\"><trk><name>%s</name><trkseg>\n",state_.name);
            return append(line_);
        case ExportFormat::Kml:
            std::snprintf(line_,sizeof(line_),"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<kml xmlns=\"http://www.opengis.net/kml/2.2\"><Document><Placemark><name>%s</name><%s><altitudeMode>clampToGround</altitudeMode><coordinates>\n",state_.name,state_.total==1?"Point":"LineString");
            return append(line_);
        case ExportFormat::Csv:
            return append("record_index,utc,utc_ms,latitude_deg,longitude_deg,altitude_m,speed_m_s,course_deg,hdop,satellites\r\n");
    }
    return false;
}
bool LogExporter::append(const char* text) {
    const size_t length=std::strlen(text);
    if(written_+buffered_+length>byte_limit_) {fail(ExportError::Limit);return false;}
    size_t offset=0;
    while(offset<length) {
        const size_t count=std::min(length-offset,sizeof(output_buffer_)-buffered_);
        std::memcpy(output_buffer_+buffered_,text+offset,count);buffered_+=count;offset+=count;
        if(buffered_==sizeof(output_buffer_) && !flush()) return false;
    }
    return true;
}
bool LogExporter::flush() {
    if(buffered_ && !output_.write(reinterpret_cast<uint8_t*>(output_buffer_),buffered_)) {
        fail(ExportError::Write);return false;
    }
    written_+=buffered_;buffered_=0;return true;
}
bool LogExporter::record(const uint8_t* b) {
    GpsFix fix{};
    if(!decodeLogRecord(b,fix)) {state_.failed_record=state_.processed;fail(ExportError::Corrupt);return false;}
    const unsigned flags=b[27];
    const double lat=static_cast<int32_t>(logGet(b+8,4))/1e7,lon=static_cast<int32_t>(logGet(b+12,4))/1e7;
    char time[32]{};
    if((flags&GpsTime) && !utcText(logGet(b,8),time)) {fail(ExportError::Time);return false;}
    if(state_.format==ExportFormat::Kml) {
        std::snprintf(line_,sizeof(line_),"%.7f,%.7f\n",lon,lat);return append(line_);
    }
    if(state_.format==ExportFormat::Gpx) {
        std::snprintf(line_,sizeof(line_),"<trkpt lat=\"%.7f\" lon=\"%.7f\">",lat,lon);
        if(!append(line_))return false;
        if(flags&GpsAltitude) {
            std::snprintf(line_,sizeof(line_),"<ele>%.3f</ele>",static_cast<int32_t>(logGet(b+16,4))/1000.0);
            if(!append(line_))return false;
        }
        if(flags&GpsTime) {std::snprintf(line_,sizeof(line_),"<time>%s</time>",time);if(!append(line_))return false;}
        if(flags&GpsSatellites) {std::snprintf(line_,sizeof(line_),"<sat>%u</sat>",b[26]);if(!append(line_))return false;}
        if(flags&GpsHdop) {std::snprintf(line_,sizeof(line_),"<hdop>%.2f</hdop>",logGet(b+24,2)/100.0);if(!append(line_))return false;}
        return append("</trkpt>\n");
    }
    std::snprintf(line_,sizeof(line_),"%lu,%s,",static_cast<unsigned long>(state_.processed),time);
    if(!append(line_))return false;
    if(flags&GpsTime) {std::snprintf(line_,sizeof(line_),"%llu",static_cast<unsigned long long>(logGet(b,8)));if(!append(line_))return false;}
    std::snprintf(line_,sizeof(line_),",%.7f,%.7f",lat,lon);if(!append(line_))return false;
    for(unsigned n=0;n<5;++n) {
        if(!append(","))return false;
        const unsigned flag=n==0?GpsAltitude:n==1?GpsSpeed:n==2?GpsCourse:n==3?GpsHdop:GpsSatellites;
        if(!(flags&flag))continue;
        if(n==0)std::snprintf(line_,sizeof(line_),"%.3f",static_cast<int32_t>(logGet(b+16,4))/1000.0);
        else if(n==4)std::snprintf(line_,sizeof(line_),"%u",b[26]);
        else std::snprintf(line_,sizeof(line_),"%.2f",logGet(b+(n==1?20:n==2?22:24),2)/100.0);
        if(!append(line_))return false;
    }
    return append("\r\n");
}
bool LogExporter::moveComplete() {
#ifdef ARDUINO_ARCH_ESP32
    const auto native=[&](const std::string& p){return std::to_string(drive_)+":"+p.substr(3);};
    return f_rename(native(partial_).c_str(),native(target_).c_str())==FR_OK;
#else
    // link does not replace an existing target, unlike POSIX rename.
    if(::link(partial_.c_str(),target_.c_str()))return false;
    return ::unlink(partial_.c_str())==0;
#endif
}
bool LogExporter::finish() {
    if(state_.format==ExportFormat::Gpx && !append("</trkseg></trk></gpx>\n"))return false;
    if(state_.format==ExportFormat::Kml) {
        std::snprintf(line_,sizeof(line_),"</coordinates></%s></Placemark></Document></kml>\n",state_.total==1?"Point":"LineString");
        if(!append(line_))return false;
    }
    if(!flush() || !output_.sync()) {if(running())fail(ExportError::Write);return false;}
    if(!input_.close() || !output_.close()) {fail(ExportError::Close);return false;}
    if(!moveComplete()) {fail(absent(target_)?ExportError::Write:ExportError::Exists);return false;}
    owns_partial_=false;state_.output_created=true;state_.phase=ExportPhase::Complete;return true;
}
bool LogExporter::closeFiles() {
    const bool in=input_.close(),out=output_.close();
    if(!in || !out)return false;
    if(owns_partial_) {
        // A failed unlink may leave a clearly named .part file, but all file
        // handles are closed. It must not prevent PC access to repair the SD,
        // nor trigger a later filesystem call after the card is republished.
        std::remove(partial_.c_str());
        owns_partial_=false;
    }
    return true;
}
void LogExporter::fail(ExportError error) {
    state_.error=error;state_.phase=ExportPhase::Failed;
    if(!closeFiles())state_.error=ExportError::Close;
}
void LogExporter::step() {
    if(!running())return;
    if(!started_) {started_=true;open();return;}
    const auto count=std::min<uint32_t>(config::ExportRecordsPerStep,state_.total-state_.processed);
    if(count && !input_.read(read_buffer_,count*32)) {fail(ExportError::Read);return;}
    for(unsigned n=0;n<count;++n) {if(!record(read_buffer_+n*32))return;++state_.processed;}
    if(state_.processed==state_.total)finish();
}
}

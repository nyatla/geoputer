#include "LogReader.h"
#include <cerrno>
#include <cctype>
#include <cstring>
#include <sys/stat.h>

namespace geoputer {
bool validLogName(const char* name) {
    const auto length=strnlen(name,LogNameBytes);
    if(length<9 || length>=LogNameBytes || std::strcmp(name+length-8,".gps.dat") || std::strstr(name,"..")) return false;
    for(size_t i=0;i<length;++i) {
        const char c=name[i];
        if(!((c>='0'&&c<='9')||(c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_'||c=='-'||c=='.')) return false;
    }
    return name[0]!='.';
}
namespace {
int order(const char* a,const char* b) {
    const bool ua=std::strncmp(a,"undated_",8)==0,ub=std::strncmp(b,"undated_",8)==0;
    return ua!=ub ? (ua?-1:1) : std::strcmp(a,b);
}
}
bool LogReader::begin(const LogReadRequest& request) {
    if(!done_ || directory_handle_) return false;
    result_={}; result_.request=request;
    result_.summary.first_utc=result_.summary.last_utc=UINT64_MAX;
    eligible_=0; started_=false; done_=false;
    if(request.kind==LogReadKind::Delete && !request.confirmed) fail(LogReadError::Unconfirmed);
    else if(request.kind!=LogReadKind::List && !validLogName(request.name)) fail(LogReadError::InvalidName);
    else if(request.kind==LogReadKind::List && request.direction!=LogListDirection::Newest && !validLogName(request.anchor))
        fail(LogReadError::InvalidName);
    else if(request.kind==LogReadKind::List && request.count>LogListRows)fail(LogReadError::Range);
    else if(request.kind==LogReadKind::Records && (!request.session_id || !request.count || request.count>LogReadCount || request.limit>LogMaxRecords))
        fail(LogReadError::Range);
    return true;
}
void LogReader::fail(LogReadError error) {
    if(directory_handle_) { SdAccessScope access(observer_); closedir(directory_handle_); directory_handle_=nullptr; }
    result_.error=error; done_=true;
}
void LogReader::cancel() {
    if(!done_) fail(LogReadError::Cancelled);
    if(file_.opened()) { SdAccessScope access(observer_); file_.close(); }
}
bool LogReader::releaseForReuse() {
    if(!done_ || directory_handle_)return false;
    if(file_.opened()) {SdAccessScope access(observer_);if(!file_.close())return false;}
    std::string{}.swap(path_);
    return true;
}
const std::string& LogReader::pathFor(const char* name) {
    // All names have passed validLogName(). Reserve once for any accepted name.
    path_.reserve(directory_.size()+LogNameBytes);
    path_.assign(directory_);path_.push_back('/');path_.append(name);
    return path_;
}
void LogReader::finishList(LogWriter* writer) {
    closedir(directory_handle_); directory_handle_=nullptr;
    const auto direction=result_.request.direction;
    const auto rows=result_.request.count?result_.request.count:LogListRows;
    if(direction!=LogListDirection::FromAnchor)
        result_.has_newer=direction==LogListDirection::Older || (direction==LogListDirection::Newer && eligible_>rows);
    result_.has_older=direction==LogListDirection::Newer || eligible_>rows;
    if(writer && writer->hasRetainedSession()) {
        for(unsigned n=0;n<result_.count;++n)
            if(pathFor(result_.data.names[n])==writer->path()) result_.active_mask|=1u<<n;
    }
    done_=true;
}
bool LogReader::step(LogWriter* writer) {
    if(done_) return true;
    if(result_.request.kind==LogReadKind::Delete) { deleteTarget(writer); return true; }
    SdAccessScope access(observer_);
    const auto rows=result_.request.count?result_.request.count:LogListRows;
    if(result_.request.kind!=LogReadKind::List) { readTarget(writer); return true; }
    if(!started_) {
        started_=true; errno=0; directory_handle_=opendir(directory_.c_str());
        if(!directory_handle_) { if(errno!=ENOENT) result_.error=LogReadError::Io; done_=true; return true; }
    }
    for(unsigned scanned=0;scanned<10;++scanned) {
        errno=0; auto* entry=readdir(directory_handle_);
        if(!entry) { if(errno) fail(LogReadError::Io); else finishList(writer); return true; }
        if(!validLogName(entry->d_name)) continue;
        struct stat info{};
        if(stat(pathFor(entry->d_name).c_str(),&info)) { fail(LogReadError::Io); return true; }
        if(!S_ISREG(info.st_mode)) continue;
        const auto direction=result_.request.direction;
        if(direction!=LogListDirection::Newest) {
            const int cmp=order(entry->d_name,result_.request.anchor);
            if(direction==LogListDirection::FromAnchor && cmp>0) {result_.has_newer=true;continue;}
            if((direction==LogListDirection::Older && cmp>=0)||(direction==LogListDirection::Newer && cmp<=0)) continue;
        }
        ++eligible_;
        auto& count=result_.count;
        auto& names=result_.data.names;
        if(count==rows) {
            if(direction==LogListDirection::Newer) {
                if(order(entry->d_name,names[0])>=0) continue;
                std::memmove(names[0],names[1],(rows-1)*LogNameBytes);
            } else if(order(entry->d_name,names[count-1])<=0) continue;
            --count;
        }
        unsigned position=0;
        while(position<count && order(names[position],entry->d_name)>0) ++position;
        if(position<count) std::memmove(names[position+1],names[position],(count-position)*LogNameBytes);
        std::memcpy(names[position],entry->d_name,std::strlen(entry->d_name)+1); ++count;
    }
    return false;
}
void LogReader::deleteTarget(LogWriter* writer) {
    const auto& path=pathFor(result_.request.name);
    // FAT32 filenames are case-insensitive. Protect even an alias of the
    // retained writer path, including paused/closing/failed-save sessions.
    auto samePath=[](const std::string& a,const std::string& b) {
        if(a.size()!=b.size()) return false;
        for(size_t n=0;n<a.size();++n)
            if(std::tolower(static_cast<unsigned char>(a[n]))!=std::tolower(static_cast<unsigned char>(b[n]))) return false;
        return true;
    };
    if(writer && writer->hasRetainedSession() && samePath(path,writer->path())) {
        fail(LogReadError::Active); return;
    }
    SdAccessScope access(observer_);
    if(file_.opened() && !file_.close()) {fail(LogReadError::DeleteFailed);return;}
    struct stat info{};
    if(stat(path.c_str(),&info)) {fail(errno==ENOENT?LogReadError::NotFound:LogReadError::DeleteFailed);return;}
    if(!S_ISREG(info.st_mode)) {fail(LogReadError::InvalidName);return;}
    if(std::remove(path.c_str())) {fail(LogReadError::DeleteFailed);return;}
    done_=true;
}
void LogReader::readTarget(LogWriter* writer) {
    const auto& request=result_.request;
    const auto& path=pathFor(request.name);
    const bool active=writer && writer->hasRetainedSession() && writer->path()==path;
    auto& file=file_;
    if(file.opened() && !file.close()) { fail(LogReadError::Io); return; }
    const auto current=active?writer->snapshot():LogSnapshot{};
    auto read=[&](uint64_t offset,uint8_t* bytes,size_t length) {
        return active ? writer->readSaved(offset,bytes,length,current.session_id) : file.seek(offset)&&file.read(bytes,length);
    };
    auto& summary=result_.summary;
    if(active) summary.bytes=512ull+32ull*current.saved;
    else if(!file.openRead(path)) { fail(LogReadError::Io); return; }
    do {
        if(!active && !file.size(summary.bytes)) { result_.error=LogReadError::Io; break; }
        if(summary.bytes<512 || summary.bytes>UINT32_MAX) { result_.error=LogReadError::Header; break; }
        uint8_t header[512];
        if(!read(0,header,sizeof(header))) { result_.error=LogReadError::Io; break; }
        if(std::memcmp(header,"GPTLOG\r\n",8) || logGet(header+508,4)!=logCrc32(header,508)) { result_.error=LogReadError::Header; break; }
        if(logGet(header+8,2)!=1) { result_.error=LogReadError::Unsupported; break; }
        if(!validLogHeader(header)) { result_.error=LogReadError::Header; break; }
        summary.session_id=logGet(header+16,8); summary.records=(summary.bytes-512)/32; summary.tail=(summary.bytes-512)%32;
        if(request.kind==LogReadKind::Records) {
            if(summary.session_id!=request.session_id || summary.records<request.limit) { result_.error=LogReadError::Changed; break; }
            if(request.start>request.limit) { result_.error=LogReadError::Range; break; }
            const auto count=static_cast<uint8_t>(request.limit-request.start<request.count ? request.limit-request.start : request.count);
            if(count && !read(512ull+32ull*request.start,result_.data.records[0],count*32)) { result_.error=LogReadError::Io; break; }
            for(unsigned n=0;n<count;++n) {
                GpsFix fix{};
                if(!decodeLogRecord(result_.data.records[n],fix)) { result_.error=LogReadError::Corrupt; result_.failed_record=request.start+n; break; }
            }
            if(result_.error!=LogReadError::None) break;
            result_.count=count; result_.eof=request.start+count==request.limit;
        } else if(summary.records) {
            uint8_t record[32]; GpsFix fix{};
            for(unsigned end=0;end<2;++end) {
                const auto index=end?summary.records-1:0;
                if(!read(512ull+32ull*index,record,32)) { result_.error=LogReadError::Io; break; }
                if(!decodeLogRecord(record,fix)) { result_.error=LogReadError::Corrupt; result_.failed_record=index; continue; }
                (end?summary.last_utc:summary.first_utc)=fix.fields&GpsTime?fix.utc_epoch_ms:UINT64_MAX;
            }
        }
    } while(false);
    if(!active && !file.close() && result_.error==LogReadError::None) result_.error=LogReadError::Io;
    done_=true;
}
} // namespace geoputer

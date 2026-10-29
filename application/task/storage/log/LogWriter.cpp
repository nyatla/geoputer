#include "LogWriter.h"
#include "../../gps/GpsTime.h"
#include <cerrno>
#include <cstring>
#include <string_view>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace geoputer {
namespace {
[[gnu::noinline]] bool sameSession(FILE* file,uint64_t session) {
    uint8_t header[LogHeaderBytes];const auto count=std::fread(header,1,sizeof(header),file);
    return count==sizeof(header) && validLogHeader(header) && logGet(header+16,8)==session;
}
}
std::string_view LogWriter::retainedName() const {
    // Storage-only reference. App changes record counters, never filename bytes.
    ShortLock::Guard guard(lock_);
    return state_.open || state_.closing || state_.buffered?std::string_view(state_.name):std::string_view{};
}
bool LogWriter::matches(const LogDirectory& directory,std::string_view name,bool fold) const {
    const auto retained=retainedName();
    return !retained.empty() && directory_.same(directory,fold) && LogDirectory::equal(retained,name,fold);
}
bool LogWriter::sameSessionExists(uint64_t session, bool& io_error) {
    SdAccessScope scope(observer_);
    if(!directory_.valid()) {io_error=true;errno=ENAMETOOLONG;return false;}
    DIR* dir=directory_.open();
    if (!dir) { io_error=errno!=ENOENT; return false; }
    bool found=false;
    while (auto* entry=readdir(dir)) {
        const std::string_view name=entry->d_name;
        if (name.size()<8 || name.compare(name.size()-8,8,".gps.dat")) continue;
        if(!directory_.fits(name)) {io_error=true;errno=ENAMETOOLONG;break;}
        auto* file=directory_.openStream(name);
        if (!file) { io_error=true; break; }
        found=sameSession(file,session);std::fclose(file);
        if(found)break;
    }
    closedir(dir);
    return found;
}
LogSnapshot LogWriter::snapshot() const {
    ShortLock::Guard guard(lock_);
    return state_;
}
bool LogWriter::hasRetainedSession() const {
    const auto state = snapshot();
    return state.open || state.closing || state.buffered;
}
bool LogWriter::needsFlush() const {
    ShortLock::Guard guard(lock_);
    return flush_requested_ || batch_count_ || state_.closing || state_.buffered==LogBufferCount;
}
LogError LogWriter::open(uint64_t session, uint64_t created) {
    // Session/file controls have one consumer (Storage), and never hold lock_ during I/O.
    if (hasRetainedSession()) return LogError::Busy;
    if (!session) return LogError::Invalid;
    bool io_error=false;
    if (sameSessionExists(session,io_error)) return LogError::SessionExists;
    if (io_error) return LogError::Io;
    header_saved_=false;
    ShortLock::Guard guard(lock_);
    const auto revision=state_.revision;
    state_={}; state_.revision=revision; state_.open=true; state_.session_id=session;
    created_=created; first_utc_=UINT64_MAX;
    head_=batch_count_=0; flush_sequence_=batch_flush_sequence_=after_batch_since_ms_=0;
    flush_requested_=false;
    changed();
    return LogError::None;
}
LogError LogWriter::append(const LogCommand& command, uint32_t now_ms) {
    // CRC/float conversion runs outside the critical section; no batch-sized staging buffer.
    uint8_t encoded[LogRecordBytes];
    const bool valid=encodeLogRecord(command.fix,encoded);
    ShortLock::Guard guard(lock_);
    if (!state_.open || command.session_id!=state_.session_id) return LogError::Invalid;
    if (state_.accepted && static_cast<int32_t>(command.sample_seq-state_.accepted_seq)<=0)
        return command.sample_seq==state_.accepted_seq ? LogError::None : LogError::Invalid;
    if (state_.buffered==LogBufferCount) return LogError::BufferFull;
    if (state_.accepted==LogMaxRecords) return LogError::Capacity;
    if (!valid) return LogError::Invalid;
    const auto tail=(head_+state_.buffered)%LogBufferCount;
    std::memcpy(records_[tail],encoded,LogRecordBytes);
    sample_seqs_[tail]=command.sample_seq;
    if (!state_.accepted) first_utc_=command.fix.fields & GpsTime ? command.fix.utc_epoch_ms : UINT64_MAX;
    if (!state_.buffered) state_.buffer_since_ms=now_ms;
    if (batch_count_ && state_.buffered==batch_count_) after_batch_since_ms_=now_ms;
    ++state_.buffered; ++state_.accepted; state_.accepted_seq=command.sample_seq;
    changed();
    // RAM acceptance never clears an outstanding physical write failure.
    return LogError::None;
}
LogError LogWriter::requestFlush(uint64_t session, uint32_t boundary, bool close) {
    ShortLock::Guard guard(lock_);
    if ((!state_.open && !state_.closing) || state_.session_id!=session ||
        (state_.accepted && boundary!=state_.accepted_seq)) return LogError::Invalid;
    flush_requested_=true; ++flush_sequence_;
    if (close) { state_.open=false; state_.closing=true; }
    changed();
    return LogError::None;
}
bool LogWriter::createFile(uint64_t session, uint64_t first_utc) {
    if(file_.opened())return true;
    if(!directory_.ensure())return false;
    const auto retained=retainedName();
    if(!retained.empty())return directory_.openWrite(file_,retained,false);
    char prefix[25];
    if(!formatJstFilenameTime(first_utc,prefix,sizeof(prefix)))
        std::snprintf(prefix,sizeof(prefix),"undated_%016llx",static_cast<unsigned long long>(session));
    for(uint32_t n=0;;++n) {
        char name[sizeof(state_.name)];
        const auto length=std::snprintf(name,sizeof(name),"%s_%lu.gps.dat",prefix,static_cast<unsigned long>(n));
        if(length<0 || size_t(length)>=sizeof(name) || !directory_.fits(name)){errno=ENAMETOOLONG;return false;}
        if(directory_.openWrite(file_,name,true)) {
            ShortLock::Guard guard(lock_);std::memcpy(state_.name,name,size_t(length)+1);return true;
        }
        if(errno!=EEXIST || n==UINT32_MAX)return false;
    }
}
bool LogWriter::writeHeader(uint64_t session,uint64_t created) {
    uint8_t header[LogHeaderBytes];encodeLogHeader(header,session,created);
    return file_.seek(0) && file_.write(header,sizeof(header));
}
bool LogWriter::flush(uint32_t now) {
    uint8_t head=0, count=0;
    uint32_t saved=0;
    uint64_t session=0, created=0, first_utc=0;
    bool finished=false;
    {
        ShortLock::Guard guard(lock_);
        if (!state_.open && !state_.closing && !state_.buffered && !file_.opened()) finished=true;
        else if (!state_.buffered && !file_.opened() && state_.closing) {
            state_.closing=false; flush_requested_=false; changed(); finished=true;
        } else if (!state_.buffered && !state_.closing) { flush_requested_=false; return true; }
        if(!finished) {
            // Retain the exact batch across failures. Producer can only append after it.
            if (!batch_count_) { batch_count_=state_.buffered; batch_flush_sequence_=flush_sequence_; }
            head=head_; count=batch_count_; saved=state_.saved;
            session=state_.session_id; created=created_; first_utc=first_utc_;
        }
    }
    if(finished)return true;
    SdAccessScope scope(observer_);
    bool ok=createFile(session,first_utc);
    if (ok) {
        if (!header_saved_) {
            ok=writeHeader(session,created);
        }
        const uint64_t offset=LogHeaderBytes+static_cast<uint64_t>(LogRecordBytes)*saved;
        if (ok) ok=file_.seek(offset);
        // Ring wrap needs two writes, not a contiguous copy on the stack.
        const auto first=static_cast<unsigned>(count)<LogBufferCount-head ? count : LogBufferCount-head;
        if (ok && first) ok=file_.write(records_[head],first*LogRecordBytes);
        if (ok && count>first) ok=file_.write(records_[0],(count-first)*LogRecordBytes);
        if (ok) ok=file_.sync();
    }
    if (!ok) {
        ShortLock::Guard guard(lock_);
        state_.error=LogError::Io; ++retries; changed(); return false;
    }
    header_saved_=true;
    bool close;
    {
        ShortLock::Guard guard(lock_);
        if (count) {
            state_.saved_seq=sample_seqs_[(head+count-1)%LogBufferCount];
            state_.saved+=count; state_.buffered-=count;
            head_=(head+count)%LogBufferCount;
            state_.buffer_since_ms=state_.buffered ? after_batch_since_ms_ : 0;
            state_.last_saved_ms=now;
        }
        batch_count_=0;
        state_.error=LogError::None;
        if (flush_sequence_==batch_flush_sequence_) flush_requested_=false;
        close=state_.closing && !state_.buffered;
        changed();
    }
    if (close) {
        const bool closed=file_.close();
        {
            ShortLock::Guard guard(lock_);
            if (!closed) { state_.error=LogError::Io; flush_requested_=true; changed(); return false; }
            state_.closing=false; changed();
        }
    }
    return true;
}
}  // namespace geoputer

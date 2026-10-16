#include "LogWriter.h"
#include "../../gps/GpsTime.h"
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace geoputer {
bool LogWriter::sameSessionExists(uint64_t session, bool& io_error) {
    SdAccessScope scope(observer_);
    DIR* dir=opendir(directory_.c_str());
    if (!dir) { io_error=errno!=ENOENT; return false; }
    bool found=false;
    while (auto* entry=readdir(dir)) {
        const std::string name=entry->d_name;
        if (name.size()<8 || name.compare(name.size()-8,8,".gps.dat")) continue;
        auto* file=std::fopen((directory_+"/"+name).c_str(),"rb");
        if (!file) { io_error=true; break; }
        uint8_t header[512];
        const auto count=std::fread(header,1,sizeof(header),file);
        std::fclose(file);
        if (count==sizeof(header) && validLogHeader(header) && logGet(header+16,8)==session) { found=true; break; }
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
    path_.clear(); header_saved_=false;
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
    if (file_.opened()) return true;
    if (mkdir(directory_.c_str(),0775) && errno!=EEXIST) return false;
    if (path_.empty()) {
        char prefix[64], time[24];
        if (formatJstFilenameTime(first_utc,time,sizeof(time))) std::snprintf(prefix,sizeof(prefix),"%s",time);
        else std::snprintf(prefix,sizeof(prefix),"undated_%016llx",static_cast<unsigned long long>(session));
        for (uint32_t n=0;;++n) {
            path_=directory_+"/"+prefix+"_"+std::to_string(n)+".gps.dat";
            if (file_.open(path_,true)) break;
            if (errno!=EEXIST || n==UINT32_MAX) { path_.clear(); return false; }
        }
    } else {
        // Retry a previously created but not yet committed header, never truncate.
        file_.open(path_,false);
    }
    return file_.opened();
}
bool LogWriter::flush(uint32_t now) {
    uint8_t head, count;
    uint32_t saved;
    uint64_t session, created, first_utc;
    {
        ShortLock::Guard guard(lock_);
        if (!state_.open && !state_.closing && !state_.buffered) return true;
        if (!state_.buffered && !file_.opened() && state_.closing) {
            state_.closing=false; flush_requested_=false; changed(); return true;
        }
        if (!state_.buffered && !state_.closing) { flush_requested_=false; return true; }
        // Retain the exact batch across failures. Producer can only append after it.
        if (!batch_count_) { batch_count_=state_.buffered; batch_flush_sequence_=flush_sequence_; }
        head=head_; count=batch_count_; saved=state_.saved;
        session=state_.session_id; created=created_; first_utc=first_utc_;
    }
    SdAccessScope scope(observer_);
    bool ok=createFile(session,first_utc);
    if (ok) {
        if (!header_saved_) {
            uint8_t header[LogHeaderBytes]; encodeLogHeader(header,session,created);
            ok=file_.seek(0) && file_.write(header,sizeof(header));
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
        ShortLock::Guard guard(lock_);
        if (!closed) { state_.error=LogError::Io; flush_requested_=true; changed(); return false; }
        state_.closing=false; changed();
    }
    return true;
}
}  // namespace geoputer

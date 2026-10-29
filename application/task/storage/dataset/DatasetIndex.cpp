#include "DatasetIndex.h"
#include "DatasetIndexFormat.h"
#include "../galuchat/FixedHeader.h"
#include "../galuchat/WordBookHeaderReader.h"
#include "../StoragePath.h"
#include "../galuchat/ReadTiming.h"
#include "../../../utils/FileReadBuffer.h"
#include "../ReadFile.h"
#include <galuchat/wgsmap3_reader.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <ctime>
#include <optional>
#include <stdexcept>
#include <string_view>
#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace geoputer {
namespace {
using namespace index_format;
using galuchat::ByteReader;
ReadFileSlot* available(ReadFileSlots* files) {if(!files)throw std::invalid_argument("Index file slots are required");return files->available();}
void require(bool value) { if (!value) throw std::runtime_error("Invalid dataset index or source"); }
uint32_t u32(uint64_t value) { require(value<=UINT32_MAX); return static_cast<uint32_t>(value); }

uint32_t crcUpdate(uint32_t crc,const uint8_t* data,size_t size) {
    for (size_t i=0;i<size;++i) {
        crc^=data[i];
        for (unsigned bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xedb88320u:0u);
    }
    return crc;
}
uint32_t nameCrc(std::string_view name) {
    return crcUpdate(UINT32_MAX,reinterpret_cast<const uint8_t*>(name.data()),name.size())^UINT32_MAX;
}
struct IndexFile : ReadFile {
    IndexFile(uint8_t drive,StorageObserver* observer,ReadFileSlot* slot=nullptr,FileIoTiming* timing=nullptr):ReadFile(drive,slot),observer_(observer),timing_(diagnosticTiming(timing)) {}
    ~IndexFile() { if (opened()) { SdAccessScope access(observer_); FileIoTime elapsed(timing_,&FileIoTiming::close_us); if(timing_)++timing_->closes; close(); } }
    IndexFile(const IndexFile&)=delete;
    IndexFile& operator=(const IndexFile&)=delete;
private:
    StorageObserver* observer_;
    FileIoTiming* timing_;
};
// Missing files and IO errors are distinct: never overwrite an unreadable old index.
bool exists(uint8_t drive,std::string_view path) {
#ifdef ARDUINO_ARCH_ESP32
    FILINFO info{}; const auto result=f_stat(storageNativePath(drive,path).c_str(),&info);
    require(result==FR_OK || result==FR_NO_FILE || result==FR_NO_PATH);
    if (result==FR_OK) require(!(info.fattrib&AM_DIR));
    return result==FR_OK;
#else
    struct stat info{};
    if (::stat(storageNativePath(drive,path).c_str(),&info)==0) { require(S_ISREG(info.st_mode)); return true; }
    require(errno==ENOENT); return false;
#endif
}
bool removeFile(uint8_t drive,std::string_view path) {
#ifdef ARDUINO_ARCH_ESP32
    const auto result=f_unlink(storageNativePath(drive,path).c_str());
    return result==FR_OK || result==FR_NO_FILE || result==FR_NO_PATH;
#else
    return std::remove(storageNativePath(drive,path).c_str())==0 || errno==ENOENT;
#endif
}
bool renameFile(uint8_t drive,std::string_view from,std::string_view to) {
#ifdef ARDUINO_ARCH_ESP32
    return f_rename(storageNativePath(drive,from).c_str(),storageNativePath(drive,to).c_str())==FR_OK;
#else
    return std::rename(storageNativePath(drive,from).c_str(),storageNativePath(drive,to).c_str())==0;
#endif
}
using Stamp=IndexSourceStamp;
Stamp statFile(uint8_t drive,std::string_view path) {
    Stamp result;
#ifdef ARDUINO_ARCH_ESP32
    FILINFO info{}; require(f_stat(storageNativePath(drive,path).c_str(),&info)==FR_OK && !(info.fattrib&AM_DIR));
    result.bytes=u32(info.fsize); result.mtime=uint32_t(info.fdate)<<16 | info.ftime;
#else
    struct stat info{}; require(::stat(storageNativePath(drive,path).c_str(),&info)==0 && S_ISREG(info.st_mode) && info.st_size>=0);
    result.bytes=u32(static_cast<uint64_t>(info.st_size));
    // Host tests use local time, matching the FAT volume's wall-clock convention.
    std::tm time{}; require(localtime_r(&info.st_mtime,&time)!=nullptr && time.tm_year>=80 && time.tm_year<=207);
    result.mtime=(uint32_t(time.tm_year-80)<<25) | (uint32_t(time.tm_mon+1)<<21) |
        (uint32_t(time.tm_mday)<<16) | (uint32_t(time.tm_hour)<<11) | (uint32_t(time.tm_min)<<5) | uint32_t(time.tm_sec/2);
#endif
    require(result.bytes>0); return result;
}
// Buffered, bounded source access. Map generation reads through skips; small
// backward header probes are satisfied from the same buffer without an SD seek.
struct Progress {
    Progress(IndexProgressCallback cb=nullptr,void* ct=nullptr):callback(cb),context(ct) {}
    IndexProgressCallback callback=nullptr;void* context=nullptr;
    IndexPhase phase=IndexPhase::Idle;char language[config::DatasetLanguageBytes]{};int percent=-1;bool verifying=false;
    void report(IndexPhase next,uint64_t done,uint64_t total,std::string_view lang="") {
        const int value=total?int(std::min<uint64_t>(100,done*100/total)):0;
        if(next==phase && lang==language && value<=percent)return;
        require(lang.size()<sizeof(language));
        if(!lang.empty())std::memcpy(language,lang.data(),lang.size());
        language[lang.size()]='\0';phase=next;percent=value;
        if(callback) {IndexProgress p{};p.phase=next;p.percent=value;p.verifying=verifying;
            std::snprintf(p.language,sizeof(p.language),"%s",language);callback(context,p);}
    }
};
class Source final : public ByteReader {
public:
    Source(uint8_t drive,std::string_view path,StorageObserver* observer,bool through=false,FileIoTiming* timing=nullptr,
           BorrowedReadBuffer* work=nullptr,ReadFileSlot* file_slot=nullptr,DatasetIndexWork* session=nullptr)
        : file_(drive,observer,file_slot,timing),observer_(observer),through_(through),timing_(diagnosticTiming(timing)) {
        buffer_.resize(config::IndexReadBufferBytes,work?work:session?session->available():nullptr);
        SdAccessScope access(observer_); uint64_t length=0;
        FileIoTime elapsed(timing_,&FileIoTiming::open_us); if(timing_)++timing_->opens;
        require(file_.openRead(path) && file_.size(length)); size_=u32(length);
    }
    size_t pos() const override { return position_; }
    bool atEnd() const override { return position_==size_; }
    uint32_t length() const { return size_; }
    void setProgress(Progress* progress) {progress_=progress;}
    uint32_t number() { return u32(readMbUInt()); }
    void seek(uint32_t offset) {
        require(offset<=size_); skipToByte();
        if (offset>=buffer_start_ && uint64_t(offset)-buffer_start_<=buffer_used_) position_=offset;
        else {
            SdAccessScope access(observer_); FileIoTime elapsed(timing_,&FileIoTiming::seek_us); if(timing_)++timing_->seeks;
            require(file_.seek(offset));
            position_=offset; buffer_start_=offset; buffer_used_=0;
        }
    }
    uint32_t crc(uint32_t start,uint32_t bytes) {
        require(uint64_t(start)+bytes<=size_);
        const auto saved=u32(pos()); seek(start); uint32_t value=UINT32_MAX;
        for (uint32_t i=0;i<bytes;++i) { const auto byte=readByte(); value=crcUpdate(value,&byte,1); }
        seek(saved); return value^UINT32_MAX;
    }
protected:
    uint8_t nextByte() override {
        require(position_<size_);
        if (position_<buffer_start_ || uint64_t(position_)-buffer_start_>=buffer_used_) refill();
        return buffer_[position_++-buffer_start_];
    }
    void skipByte(size_t bytes) override {
        require(bytes<=uint64_t(size_)-position_);
        if (!through_) { seek(u32(uint64_t(position_)+bytes)); return; }
        while (bytes) {
            if (position_<buffer_start_ || uint64_t(position_)-buffer_start_>=buffer_used_) refill();
            const auto count=std::min(bytes,buffer_used_-(position_-buffer_start_));
            position_+=count; bytes-=count;
        }
    }
private:
    void refill() {
        SdAccessScope access(observer_);
        buffer_start_=position_; buffer_used_=std::min<size_t>(buffer_.size(),size_-position_);
        // Physical position is always immediately after the previous buffer.
        { FileIoTime elapsed(timing_,&FileIoTiming::read_us); if(timing_)++timing_->reads;
          size_t received=0;require(file_.read(buffer_.data(),buffer_used_,received) && received==buffer_used_); if(timing_)timing_->bytes+=buffer_used_; }
        if(progress_)progress_->report(IndexPhase::Map,uint64_t(position_)+buffer_used_,size_);
#ifdef ARDUINO_ARCH_ESP32
        vTaskDelay(1); // Keep input / drawing tasks responsive throughout full-map scans.
#endif
    }
    FileReadBuffer buffer_;
    IndexFile file_;
    StorageObserver* observer_;
    uint32_t size_=0,position_=0,buffer_start_=0;
    size_t buffer_used_=0;
    bool through_;
    Progress* progress_=nullptr;
    FileIoTiming* timing_;
};
class Output {
public:
    Output(uint8_t drive,std::string_view path,StorageObserver* observer,bool compare=false,ReadFileSlots* files=nullptr,DatasetIndexWork* session=nullptr)
        : observer_(observer) {
        SdAccessScope access(observer_);
        if(compare) comparison_.emplace(drive,path,observer,false,nullptr,nullptr,available(files),session);
        else {
            file_.emplace(drive,observer,available(files));
            buffer_.resize(config::IndexReadBufferBytes,session?session->available():nullptr);
            require(file_->openStaging(path));
        }
    }
    uint32_t pos() const { return position_; }
    template<size_t N> void write(const std::array<uint8_t,N>& bytes) { write(bytes.data(),N); }
    void write(const uint8_t* bytes,size_t count) {
        require(uint64_t(position_)+count<=UINT32_MAX);
        if(comparison_) {
            for(size_t i=0;i<count;++i)require(comparison_->readByte()==bytes[i]);
            position_+=count;return;
        }
        while (count) {
            const auto n=std::min(count,buffer_.size()-used_);
            std::memcpy(buffer_.data()+used_,bytes,n); used_+=n; position_+=n; bytes+=n; count-=n;
            if (used_==buffer_.size()) flush();
        }
    }
    void seek(uint32_t offset) {
        if(comparison_) {comparison_->seek(offset);position_=offset;return;}
        flush();SdAccessScope access(observer_);require(file_->seek(offset));position_=offset;
    }
    template<size_t N> void placeholder(const std::array<uint8_t,N>& bytes) {
        if(comparison_) {seek(u32(uint64_t(position_)+N));} else write(bytes);
    }
    void finish() {
        if(comparison_) {require(uint64_t(position_)+4==comparison_->length());return;}
        flush(); const auto end=position_; seek(0);
        uint32_t crc=UINT32_MAX,remaining=end;
        while (remaining) {
            const auto n=std::min<size_t>(remaining,buffer_.size());
            { SdAccessScope access(observer_); require(file_->read(buffer_.data(),n)); }
            crc=crcUpdate(crc,buffer_.data(),n); remaining-=n;
        }
        seek(end); std::array<uint8_t,4> footer{}; put(footer.data(),crc^UINT32_MAX); write(footer); flush();
        SdAccessScope access(observer_); require(file_->sync()); require(file_->close());
    }
private:
    void flush() {
        if (!used_) return;
        SdAccessScope access(observer_); require(file_->write(buffer_.data(),used_)); used_=0;
    }
    std::optional<IndexFile> file_;
    StorageObserver* observer_;
    FileReadBuffer buffer_;
    size_t used_=0;
    uint32_t position_=0;
    std::optional<Source> comparison_;
};
bool matchesTag(Source& reader,const char* expected) {
    char tag[4];for(auto& byte:tag)byte=static_cast<char>(reader.readByte());
    return std::memcmp(tag,expected,sizeof(tag))==0;
}
struct Chunk { uint32_t start=0,data=0,bytes=0,end=0; };
Chunk chunkHeader(Source& source,const char* expected) {
    Chunk c; c.start=u32(source.pos()); require(matchesTag(source,expected)); c.bytes=source.number();
    c.data=u32(source.pos()); c.end=u32(uint64_t(c.data)+c.bytes); require(c.end<=source.length()); return c;
}
Stamp fingerprint(uint8_t drive,std::string_view path,const char* magic,StorageObserver* observer,ReadFileSlots* files,DatasetIndexWork* work=nullptr) {
    SdAccessScope access(observer); auto stamp=statFile(drive,path);
    Source source(drive,path,observer,false,nullptr,nullptr,available(files),work); const auto header=chunkHeader(source,magic);
    stamp.header_crc=source.crc(0,header.end); return stamp;
}
void setStamp(uint8_t* out,std::string_view name,const Stamp& stamp) {
    put(out,nameCrc(name)); put(out+4,stamp.bytes); put(out+8,stamp.mtime); put(out+12,stamp.header_crc);
}

void mapIndex(Output& out,Source& source,std::array<uint8_t,64>& header,Progress* progress=nullptr,DatasetIndexWork* work=nullptr) {
    const auto glch=chunkHeader(source,"GLCH");
    galuchat::detail::LimitedReader payload(source,glch.bytes);
    require(matchesFixedAscii16(payload,"WGSMapSet/3"));
    const auto unit_x=u32(payload.readMbUInt()),unit_y=u32(payload.readMbUInt());
    require(unit_x && unit_y && unit_x<=INT32_MAX && unit_y<=INT32_MAX); put(header.data() + Header::UnitInvX,unit_x); put(header.data() + Header::UnitInvY,unit_y);
    const auto metadata=u32(payload.readMbUInt()); require(metadata<=payload.remaining()); payload.skipInByte(metadata);
    const auto count=u32(payload.readMbUInt());
    require(count && count<=config::MaxDatasetChunks && count<=payload.remaining()/2);
    require(work && count<=work->origins().size());
    auto* origins=work->origins().data();
    for (uint32_t i=0;i<count;++i) {
        const auto x=payload.readMbInt(),y=payload.readMbInt();
        require(x>=INT32_MIN && x<=INT32_MAX && y>=INT32_MIN && y<=INT32_MAX);
        origins[i]={static_cast<int32_t>(x),static_cast<int32_t>(y)};
    }
    require(payload.atEnd()); put(header.data() + Header::ChunkCount,count);
    const auto next=u32(source.pos());
    if (matchesTag(source,"LAYO")) { const auto bytes=source.number(); source.skipInByte(bytes); }
    else source.seek(next);
    const auto begin=out.pos();
    for (uint32_t i=0;i<count;++i) {
        const auto c=chunkHeader(source,"GI01"); source.seek(c.start);
        galuchat::GaluchatImageDataChunk01Reader image(source);
        require(int64_t(origins[i].x)+image.width<=INT32_MAX && int64_t(origins[i].y)+image.height<=INT32_MAX);
        const auto first=u32(source.pos());
        const auto blocks=u32(uint64_t(image.hus)*image.vus);
        std::array<uint8_t,ChunkRecord::Bytes> record{};
        put(record.data(),static_cast<uint32_t>(origins[i].x)); put(record.data() + ChunkRecord::South,static_cast<uint32_t>(origins[i].y));
        put(record.data() + ChunkRecord::Width,image.width); put(record.data() + ChunkRecord::Height,image.height); put(record.data() + ChunkRecord::SquareUnit,image.square_unit);
        put(record.data() + ChunkRecord::ChunkOffset,c.start); put(record.data() + ChunkRecord::ChunkBytes,c.end-c.start); put(record.data() + ChunkRecord::FirstBlockOffset,first); put(record.data() + ChunkRecord::BlockCount,blocks);
        const auto record_offset=out.pos(); out.placeholder(record);
        uint32_t groups=0,group_first=0,group_offset=first,group_bytes=0,group_count=0;
        const bool indexed=c.end-c.start>=config::IndexMinChunkBytes;
        auto emit=[&]() {
            std::array<uint8_t,Group::Bytes> group{};
            put(group.data(),group_first); put(group.data() + Group::BlockCount,group_count);
            put(group.data() + Group::DataOffset,group_offset); put(group.data() + Group::DataBytes,group_bytes); out.write(group);
            ++groups; group_first+=group_count; group_offset=u32(uint64_t(group_offset)+group_bytes); group_count=group_bytes=0;
        };
        image.visitBlockPositions([&](size_t index,size_t relative,size_t bytes) {
            if(progress)progress->report(IndexPhase::Map,source.pos(),source.length());
            require(bytes && index<blocks && uint64_t(first)+relative+bytes<=c.end);
            if (!indexed) return;
            require(index==uint64_t(group_first)+group_count && uint64_t(first)+relative==uint64_t(group_offset)+group_bytes);
            group_bytes=u32(uint64_t(group_bytes)+bytes); ++group_count;
            if (group_bytes>=config::IndexGroupTargetBytes) emit();
        });
        require(source.pos()==c.end);
        if (indexed && group_count) emit();
        if (indexed) require(group_first==blocks && group_offset==c.end);
        const auto end=out.pos(); put(record.data() + ChunkRecord::GroupCount,groups); out.seek(record_offset); out.write(record); out.seek(end);
    }
    require(source.atEnd()); put(header.data() + Header::MapIndexBytes,out.pos()-begin);
}
// Skips a complete TI00 node, preserving bit alignment. The depth bound limits
// recursion even when a PrefixContainer uses payload_bits=0.
uint32_t indexNode(ByteReader& reader,uint32_t level,uint32_t depth,uint32_t bits) {
    if (level==depth-1) {
        const auto leaves=u32(reader.readMbUInt()); require(leaves>0);
        const auto skip=uint64_t(leaves)*bits; require(skip<=SIZE_MAX); reader.skipBits(static_cast<size_t>(skip)); return leaves;
    }
    (void)reader.readBitsAsInt32(bits);
    const auto leaves=u32(reader.readMbUInt()),payload_bits=u32(reader.readMbUInt()); require(leaves>0);
    if (payload_bits) reader.skipBits(payload_bits);
    else {
        uint32_t total=0;
        while (total<leaves) { const auto child=indexNode(reader,level+1,depth,bits); require(child<=leaves-total); total+=child; }
    }
    return leaves;
}
void wordbookIndex(Output& out,Source& source,std::string_view language,std::string_view name,const Stamp& stamp,Progress* progress=nullptr) {
    std::array<Chunk,4> chunks{};
    const char* names[]={"GW00","TM00","TT00","TI00"};
    for (size_t i=0;i<chunks.size();++i) { chunks[i]=chunkHeader(source,names[i]); source.seek(chunks[i].end); }
    require(source.atEnd()); source.seek(chunks[0].data);
    require(matchesFixedAscii16(source,"GisWordBook/0"));
    const auto gw_metadata=source.number(); require(uint64_t(source.pos())+gw_metadata==chunks[0].end);
    std::array<uint8_t,Wordbook::Bytes> record{};
    require(!language.empty() && language.size()<32);
    for (unsigned char c:language) require(c>=0x21 && c<0x7f);
    std::memcpy(record.data(),language.data(),language.size());
    setStamp(record.data() + Wordbook::NameCrc32,name,stamp);
    put(record.data() + Wordbook::TiDataOffset,chunks[3].data); put(record.data() + Wordbook::TiDataBytes,chunks[3].bytes);
    put(record.data() + Wordbook::TtDataOffset,chunks[2].data); put(record.data() + Wordbook::TtDataBytes,chunks[2].bytes);
    put(record.data() + Wordbook::TmDataOffset,chunks[1].data); put(record.data() + Wordbook::TmDataBytes,chunks[1].bytes);
    put16(record.data() + Wordbook::TiRecordBytes,16); put16(record.data() + Wordbook::TtRecordBytes,8); put16(record.data() + Wordbook::TmRecordBytes,16); put16(record.data() + Wordbook::HeaderBytes,128);
    const auto start=out.pos(); out.placeholder(record);
    // TI00 roots are bit aligned; pos*8-bitOffset identifies the next unread bit.
    source.seek(chunks[3].data);
    galuchat::detail::LimitedReader ti(source,chunks[3].bytes);
    const auto depth=u32(ti.readMbUInt()),records=u32(ti.readMbUInt()),bits=u32(ti.readMbUInt());
    const auto roots=u32(ti.readMbUInt()),stream=u32(ti.readMbUInt());
    require(depth>0 && depth<=config::IndexMaxDepth && records && bits>0 && bits<=31 && roots>0 && roots<=records && stream==ti.remaining());
    put(record.data() + Wordbook::RecordCount,records); put16(record.data() + Wordbook::Depth,depth); record[Wordbook::CodeBits]=static_cast<uint8_t>(bits); put(record.data() + Wordbook::RootCount,roots);
    uint32_t first_index=0;
    for (uint32_t i=0;i<roots;++i) {
        const auto position=(uint64_t(chunks[3].data)+ti.pos())*8-ti.bitOffset();
        if(progress)progress->report(IndexPhase::TI,i,roots,language);
        const auto leaves=indexNode(ti,0,depth,bits); require(leaves<=records-first_index);
        std::array<uint8_t,TiRoot::Bytes> root{}; put(root.data(),first_index); put(root.data() + TiRoot::RecordCount,leaves);
        put(root.data() + TiRoot::DataOffset,u32(position/8)); root[TiRoot::StartBit]=static_cast<uint8_t>(position%8); out.write(root); first_index+=leaves;
    }
    require(first_index==records);
    if (ti.bitOffset()) require(ti.readBitsAsInt32(ti.bitOffset())==0);
    require(ti.atEnd());
    // TT00: one entry per page, no page payload decoding or index array.
    source.seek(chunks[2].data);
    galuchat::detail::LimitedReader tt(source,chunks[2].bytes);
    const auto components=u32(tt.readMbUInt()),page_size=u32(tt.readMbUInt()),pages=u32(tt.readMbUInt()),token_bits=u32(tt.readMbUInt());
    require(components && page_size && pages && pages<=components && token_bits>0 && token_bits<=16 && uint64_t(components)<=(uint64_t(1)<<bits));
    put(record.data() + Wordbook::ComponentCount,components); record[Wordbook::TokenBits]=static_cast<uint8_t>(token_bits); put(record.data() + Wordbook::TextPageCount,pages);
    uint32_t first_code=0,previous_tokens=0;
    for (uint32_t i=0;i<pages;++i) {
        if(progress)progress->report(IndexPhase::TT,i,pages,language);
        const auto position=u32(source.pos()); require(tt.readByte()==0);
        const auto tokens=u32(tt.readMbUInt()),count=u32(tt.readMbUInt()),bytes=u32(tt.readMbUInt());
        require(tokens>=previous_tokens && count>0 && count<=page_size && count<=components-first_code && bytes>0 && bytes<=tt.remaining());
        std::array<uint8_t,TtPage::Bytes> page{}; put(page.data(),first_code); put(page.data() + TtPage::DataOffset,position); out.write(page);
        tt.skipInByte(bytes); first_code+=count; previous_tokens=tokens;
    }
    require(first_code==components && tt.atEnd());
    // TM00: fixed-length fragments within each page; store the stream start.
    source.seek(chunks[1].data);
    galuchat::detail::LimitedReader tm(source,chunks[1].bytes);
    const auto tokens=u32(tm.readMbUInt()),token_pages=u32(tm.readMbUInt());
    require(tokens && token_pages && token_pages<=tokens && uint64_t(tokens)<=(uint64_t(1)<<token_bits));
    put(record.data() + Wordbook::TokenCount,tokens); put(record.data() + Wordbook::TokenPageCount,token_pages);
    uint32_t first_token=0,previous_bytes=0;
    for (uint32_t i=0;i<token_pages;++i) {
        if(progress)progress->report(IndexPhase::TM,i,token_pages,language);
        require(tm.readByte()==0);
        const auto bytes=u32(tm.readMbUInt()),count=u32(tm.readMbUInt()),stream_bytes=u32(tm.readMbUInt());
        require(bytes>0 && bytes>=previous_bytes && count>0 && count<=tokens-first_token && uint64_t(bytes)*count==stream_bytes && stream_bytes<=tm.remaining());
        std::array<uint8_t,TmPage::Bytes> page{}; put(page.data(),first_token); put(page.data() + TmPage::TokenCount,count); put(page.data() + TmPage::TokenBytes,bytes); put(page.data() + TmPage::DataOffset,u32(source.pos())); out.write(page);
        tm.skipInByte(stream_bytes); first_token+=count; previous_bytes=bytes;
    }
    require(first_token==tokens && tm.atEnd());
    const auto end=out.pos(); put(record.data() + Wordbook::SectionBytes,end-start); out.seek(start); out.write(record); out.seek(end);
}
// Operation-scoped ownership of the fixed dictionary table.
class BookSources {
public:
    explicit BookSources(DatasetIndexWork* work):work_(work) {
        if(!work_)throw std::invalid_argument("Index work memory is required");
        table_=&work_->borrowBooks();
    }
    ~BookSources() {work_->returnBooks(*table_);}
    BookSources(const BookSources&)=delete;
    BookSources& operator=(const BookSources&)=delete;
    IndexBookTable& table() {return *table_;}
private:
    DatasetIndexWork* work_;
    IndexBookTable* table_;
};
void readBookSources(uint8_t drive,std::string_view directory,const DatasetMetadata& metadata,
    IndexBookTable& result,StorageObserver* observer,ReadFileSlots* files,DatasetIndexWork* work=nullptr) {
    DatasetLanguages languages;
    if(metadata.language_count) {
        DatasetMetadata selected;
        readDatasetMetadataInto(directory,UiLanguage::Japanese,metadata.language,selected,observer,{},0,&languages);
        require(selected.error==DatasetError::None && std::string_view(selected.map)==metadata.map);
    } else {
        require(validDatasetLanguage(metadata.language));
        languages.count=1;std::memcpy(languages.ids[0].data(),metadata.language,sizeof(metadata.language));
    }
    std::sort(languages.ids.begin(),languages.ids.begin()+languages.count,[](const auto& a,const auto& b) {
        return std::string_view(a.data())<std::string_view(b.data());
    });
    require(languages.count<=result.records.size());result.count=0;
    for(unsigned i=0;i<languages.count;++i) {
        const auto* language=languages.ids[i].data();
        // The parsed metadata is consumed before fingerprint's Reader is entered.
        DatasetMetadata selected;
        if(std::string_view(language)==metadata.language)selected=metadata;
        else readDatasetMetadataInto(directory,UiLanguage::Japanese,language,selected,observer);
        require(selected.error==DatasetError::None && std::string_view(selected.map)==metadata.map && selected.wordbook[0]);
        auto& book=result.records[result.count];
        std::memcpy(book.language,language,sizeof(book.language));
        std::memcpy(book.name,selected.wordbook,sizeof(book.name));
        book.stamp=fingerprint(drive,TemporaryPath({directory,"/",book.name}).view(),"GW00",observer,files,work);
        ++result.count;
    }
}
void generate(uint8_t drive,std::string_view path,std::string_view directory,const DatasetMetadata& metadata,
              const Stamp& map_stamp,const IndexBookTable& books,StorageObserver* observer,
              ReadFileSlots* files,Progress* progress=nullptr,bool compare=false,DatasetIndexWork* work=nullptr) {
    Output out(drive,path,observer,compare,files,work);
    std::array<uint8_t,Header::Bytes> header{};std::memcpy(header.data(),"RGPMIDX",8);
    put16(header.data() + Header::Version,1);put16(header.data() + Header::HeaderBytes,64);put16(header.data() + Header::ChunkRecordBytes,40);put16(header.data() + Header::GroupRecordBytes,16);
    put(header.data() + Header::MapBytes,map_stamp.bytes);put(header.data() + Header::MapFatMtime,map_stamp.mtime);
    put(header.data() + Header::MapNameCrc32,nameCrc(metadata.map));put(header.data() + Header::MapHeaderCrc32,map_stamp.header_crc);
    put(header.data() + Header::IndexedChunkMinBytes,config::IndexMinChunkBytes);put(header.data() + Header::GroupTargetBytes,config::IndexGroupTargetBytes);put(header.data() + Header::WordbookCount,u32(books.size()));
    out.placeholder(header);
    if(progress)progress->report(IndexPhase::Map,0,1);
    {
        // Keep the path alive until the Source and its read buffer are released.
        const auto source_path=TemporaryPath({directory,"/",metadata.map});
        Source source(drive,source_path.view(),observer,true,nullptr,nullptr,available(files),work);
        source.setProgress(progress);mapIndex(out,source,header,progress,work);
    }
    if(progress)progress->report(IndexPhase::Map,1,1);
    for(const auto& book:books) {
        const auto source_path=TemporaryPath({directory,"/",book.name});
        Source source(drive,source_path.view(),observer,false,nullptr,nullptr,available(files),work);
        wordbookIndex(out,source,book.language,book.name,book.stamp,progress);
        if(progress)progress->report(IndexPhase::TM,1,1,book.language);
    }
    put(header.data() + Header::FileBytes,u32(uint64_t(out.pos())+4));out.seek(0);out.write(header);
    out.seek(get(header.data() + Header::FileBytes)-4);out.finish();
}
// A forward-only validator: fixed-size records, bounded counters, no table arrays.
class Validation {
public:
    explicit Validation(Source& source):source_(source) {}
    template<size_t N> std::array<uint8_t,N> read() {
        std::array<uint8_t,N> bytes{};
        require(uint64_t(source_.pos())+N<=source_.length()-4);
        bytes=readRecord<N>(source_);
        crc_=crcUpdate(crc_,bytes.data(),N); return bytes;
    }
    void run(DatasetMapIndex* summary=nullptr,DatasetWordBookIndex* book=nullptr,std::string_view selected_language="") {
        if (book) *book={};
        require(source_.length()>=68);
        const auto header=read<Header::Bytes>(); const auto* h=header.data();
        require(std::memcmp(h,"RGPMIDX",8)==0 && get16(h + Header::Version)==1 && get16(h + Header::HeaderBytes)==64 && get16(h + Header::ChunkRecordBytes)==40 && get16(h + Header::GroupRecordBytes)==16);
        require(get(h + Header::FileBytes)==source_.length() && get(h + Header::MapBytes)>0 && get(h + Header::ChunkCount)>0 && get(h + Header::ChunkCount)<=config::MaxDatasetChunks && get(h + Header::UnitInvX) && get(h + Header::UnitInvY));
        require(get(h + Header::IndexedChunkMinBytes)==config::IndexMinChunkBytes && get(h + Header::GroupTargetBytes)==config::IndexGroupTargetBytes && get(h + Header::WordbookCount)>0);
        const auto map_end=uint64_t(64)+get(h + Header::MapIndexBytes); require(map_end<=source_.length()-4);
        if (summary) summary->header=header;
        int64_t west=INT32_MAX,south=INT32_MAX,east=INT32_MIN,north=INT32_MIN;
        uint64_t previous_chunk_end=0;
        for (uint32_t i=0;i<get(h + Header::ChunkCount);++i) {
            const auto record=read<ChunkRecord::Bytes>(); const auto* c=record.data();
            const auto width=get(c + ChunkRecord::Width),height=get(c + ChunkRecord::Height),square=get(c + ChunkRecord::SquareUnit),start=get(c + ChunkRecord::ChunkOffset),bytes=get(c + ChunkRecord::ChunkBytes),first=get(c + ChunkRecord::FirstBlockOffset),blocks=get(c + ChunkRecord::BlockCount),groups=get(c + ChunkRecord::GroupCount);
            const auto end=uint64_t(start)+bytes;
            const int64_t x=static_cast<int32_t>(get(c)),y=static_cast<int32_t>(get(c + ChunkRecord::South));
            require(width<=INT32_MAX && height<=INT32_MAX && x+width<=INT32_MAX && y+height<=INT32_MAX);
            west=std::min(west,x); south=std::min(south,y); east=std::max(east,x+width); north=std::max(north,y+height);
            require(width && height && square && square<=uint32_t(1)<<30 && !(square&(square-1)) && bytes>0 && end<=get(h + Header::MapBytes));
            require(first>start && first<end && start>=previous_chunk_end && (!i || start==previous_chunk_end));
            require(blocks==((uint64_t(width)+square-1)/square)*((uint64_t(height)+square-1)/square));
            require(bytes<config::IndexMinChunkBytes ? groups==0 : groups>0 && groups<=blocks);
            require(uint64_t(source_.pos())+uint64_t(groups)*16<=map_end);
            uint64_t block=0,offset=first;
            for (uint32_t g=0;g<groups;++g) {
                const auto group=read<Group::Bytes>(); const auto* p=group.data();
                require(get(p)==block && get(p + Group::BlockCount)>0 && get(p + Group::BlockCount)<=blocks-block && get(p + Group::DataOffset)==offset && get(p + Group::DataBytes)>0 && offset+get(p + Group::DataBytes)<=end);
                require(g+1==groups || get(p + Group::DataBytes)>=config::IndexGroupTargetBytes);
                block+=get(p + Group::BlockCount); offset+=get(p + Group::DataBytes);
            }
            if (groups) require(block==blocks && offset==end);
            previous_chunk_end=end;
        }
        require(source_.pos()==map_end && previous_chunk_end==get(h + Header::MapBytes));
        require(east-west<=INT32_MAX && north-south<=INT32_MAX);
        if (summary) { summary->west=west; summary->south=south; summary->width=east-west; summary->height=north-south; }
        char previous_language[32]{};
        for (uint32_t section=0;section<get(h + Header::WordbookCount);++section) {
            const auto begin=source_.pos(); const auto record=read<Wordbook::Bytes>(); const auto* w=record.data();
            size_t language_bytes=0;
            while (language_bytes<32 && w[language_bytes]) { require(w[language_bytes]>=0x21 && w[language_bytes]<0x7f); ++language_bytes; }
            require(language_bytes && language_bytes<32);
            for (size_t i=language_bytes;i<32;++i) require(w[i]==0);
            const std::string_view language(reinterpret_cast<const char*>(w),language_bytes);
            require(!previous_language[0] || std::string_view(previous_language)<language);
            std::memcpy(previous_language,w,language_bytes+1);
            if (book && ((selected_language.empty() && section==0) || language==selected_language)) {
                book->header=record; book->section_offset=u32(begin);
            }
            for (unsigned i=Wordbook::Reserved;i<Wordbook::Bytes;++i) require(w[i]==0);
            require(get16(w + Wordbook::TiRecordBytes)==16 && get16(w + Wordbook::TtRecordBytes)==8 && get16(w + Wordbook::TmRecordBytes)==16 && get16(w + Wordbook::HeaderBytes)==128);
            const uint64_t ti_end=uint64_t(get(w + Wordbook::TiDataOffset))+get(w + Wordbook::TiDataBytes),tt_end=uint64_t(get(w + Wordbook::TtDataOffset))+get(w + Wordbook::TtDataBytes),tm_end=uint64_t(get(w + Wordbook::TmDataOffset))+get(w + Wordbook::TmDataBytes);
            require(get(w + Wordbook::TmDataOffset)>0 && get(w + Wordbook::TmDataBytes)>0 && get(w + Wordbook::TtDataBytes)>0 && get(w + Wordbook::TiDataBytes)>0 && tm_end<get(w + Wordbook::TtDataOffset) && tt_end<get(w + Wordbook::TiDataOffset) && ti_end==get(w + Wordbook::SourceBytes));
            const auto records=get(w + Wordbook::RecordCount),components=get(w + Wordbook::ComponentCount),tokens=get(w + Wordbook::TokenCount),roots=get(w + Wordbook::RootCount),pages=get(w + Wordbook::TextPageCount),token_pages=get(w + Wordbook::TokenPageCount);
            require(records && components && tokens && roots && roots<=records && pages && pages<=components && token_pages && token_pages<=tokens);
            require(get16(w + Wordbook::Depth)>0 && get16(w + Wordbook::Depth)<=config::IndexMaxDepth && w[Wordbook::CodeBits]>0 && w[Wordbook::CodeBits]<=31 && w[Wordbook::TokenBits]>0 && w[Wordbook::TokenBits]<=16);
            require(uint64_t(components)<=(uint64_t(1)<<w[Wordbook::CodeBits]) && uint64_t(tokens)<=(uint64_t(1)<<w[Wordbook::TokenBits]));
            const auto section_bytes=uint64_t(128)+uint64_t(roots)*16+uint64_t(pages)*8+uint64_t(token_pages)*16;
            require(section_bytes==get(w + Wordbook::SectionBytes) && begin+section_bytes<=source_.length()-4);
            uint64_t index=0,previous_bit=0;
            for (uint32_t i=0;i<roots;++i) {
                const auto root=read<TiRoot::Bytes>(); const auto* p=root.data();
                const auto bit=uint64_t(get(p + TiRoot::DataOffset))*8+p[TiRoot::StartBit];
                require(get(p)==index && get(p + TiRoot::RecordCount)>0 && get(p + TiRoot::RecordCount)<=records-index && p[TiRoot::StartBit]<8 && !p[13] && !p[14] && !p[15]);
                require(bit>=uint64_t(get(w + Wordbook::TiDataOffset))*8 && bit<ti_end*8 && (!i || bit>previous_bit));
                index+=get(p + TiRoot::RecordCount); previous_bit=bit;
            }
            require(index==records);
            uint32_t previous_code=0,previous_offset=0;
            for (uint32_t i=0;i<pages;++i) {
                const auto page=read<TtPage::Bytes>(); const auto code=get(page.data()),offset=get(page.data() + TtPage::DataOffset);
                require(code<components && (i ? code>previous_code : code==0));
                require(offset>=get(w + Wordbook::TtDataOffset) && offset<tt_end && (!i || offset>previous_offset));
                previous_code=code; previous_offset=offset;
            }
            index=0; uint32_t previous_bytes=0; uint64_t previous_end=0;
            for (uint32_t i=0;i<token_pages;++i) {
                const auto page=read<TmPage::Bytes>(); const auto* p=page.data();
                const auto end=uint64_t(get(p + TmPage::DataOffset))+uint64_t(get(p + TmPage::TokenCount))*get(p + TmPage::TokenBytes);
                require(get(p)==index && get(p + TmPage::TokenCount)>0 && get(p + TmPage::TokenCount)<=tokens-index && get(p + TmPage::TokenBytes)>0 && get(p + TmPage::TokenBytes)>=previous_bytes);
                require(get(p + TmPage::DataOffset)>=get(w + Wordbook::TmDataOffset) && get(p + TmPage::DataOffset)>previous_end && end<=tm_end);
                index+=get(p + TmPage::TokenCount); previous_bytes=get(p + TmPage::TokenBytes); previous_end=end;
            }
            require(index==tokens && previous_end==tm_end && source_.pos()==begin+section_bytes);
        }
        require(source_.pos()==source_.length()-4);
        std::array<uint8_t,4> footer{}; for (auto& b:footer) b=source_.readByte();
        require(get(footer.data())==(crc_^UINT32_MAX) && source_.atEnd());
    }
private:
    Source& source_;
    uint32_t crc_=UINT32_MAX;
};

void checkSources(uint8_t drive,std::string_view directory,const DatasetMetadata& metadata,
                  const Stamp& map,const IndexBookTable& books,StorageObserver* observer,ReadFileSlots* files,DatasetIndexWork* work=nullptr) {
    require(map==fingerprint(drive,TemporaryPath({directory,"/",metadata.map}).view(),"GLCH",observer,files,work));
    for(const auto& b:books)require(b.stamp==fingerprint(drive,TemporaryPath({directory,"/",b.name}).view(),"GW00",observer,files,work));
}
struct MapHeader {
    Chunk chunk;
    uint32_t count,unit_x,unit_y;
};
MapHeader mapHeader(Source& source) {
    const auto chunk=chunkHeader(source,"GLCH");
    require(matchesFixedAscii16(source,"WGSMapSet/3"));
    const auto x=source.number(),y=source.number(),metadata=source.number();
    require(x && y && x<=INT32_MAX && y<=INT32_MAX && uint64_t(source.pos())+metadata<chunk.end);
    source.skipInByte(metadata); const auto count=source.number();
    require(count && uint64_t(source.pos())+uint64_t(count)*2<=chunk.end);
    if (count>config::MaxDatasetChunks) throw MapChunkLimitExceeded(count,config::MaxDatasetChunks);
    return {chunk,count,x,y};
}
} // namespace

DatasetMapIndex DatasetIndex::loadMap(std::string_view path,std::string_view map,FileIoTiming* map_timing,FileIoTiming* index_timing,
                                     DatasetWordBookIndex* book,std::string_view language,BorrowedReadBuffer* work,ReadFileSlot* file_slot) const {
    Stamp stamp;
    MapHeader metadata;
    {
        SdAccessScope access(observer_);
        { FileIoTime elapsed(diagnosticTiming(map_timing),&FileIoTiming::stat_us); if(config::DetailedDiagnostics && map_timing)++map_timing->stats; stamp=statFile(drive_,map); }
        Source source(drive_,map,observer_,false,map_timing,work,file_slot?file_slot:available(files_),work_); metadata=mapHeader(source);
        stamp.header_crc=source.crc(0,metadata.chunk.end);
    }
    DatasetMapIndex info;
    { FileIoTime elapsed(diagnosticTiming(index_timing),&FileIoTiming::stat_us); if(config::DetailedDiagnostics && index_timing)++index_timing->stats;
      if(!exists(drive_,path)) throw std::runtime_error("Missing _mapindex.dat; reapply dataset"); }
    try { Source index(drive_,path,observer_,false,index_timing,work,file_slot?file_slot:available(files_),work_); Validation(index).run(&info,book,language); }
    catch(const std::bad_alloc&) { throw; }
    catch(const std::exception&) { throw std::runtime_error("Invalid _mapindex.dat; reapply dataset"); }
    if (book && !book->section_offset) throw std::runtime_error("Missing Wordbook index; reapply dataset");
    const auto* h=info.header.data(); const auto slash=map.find_last_of('/');
    if (!(get(h + Header::MapBytes)==stamp.bytes && get(h + Header::MapFatMtime)==stamp.mtime && get(h + Header::MapNameCrc32)==nameCrc(std::string_view(map).substr(slash==std::string::npos?0:slash+1)) && get(h + Header::MapHeaderCrc32)==stamp.header_crc &&
          get(h + Header::ChunkCount)==metadata.count && get(h + Header::UnitInvX)==metadata.unit_x && get(h + Header::UnitInvY)==metadata.unit_y))
        throw std::runtime_error("Stale _mapindex.dat; reapply dataset");
    return info;
}
void DatasetIndex::checkWordBook(const DatasetWordBookIndex& info,std::string_view path,FileIoTiming* timing,BorrowedReadBuffer* work) const {
    SdAccessScope access(observer_);
    Stamp stamp;
    { FileIoTime elapsed(timing,&FileIoTiming::stat_us); if(config::DetailedDiagnostics && timing)++timing->stats; stamp=statFile(drive_,path); }
    Source source(drive_,path,observer_,false,timing,work,available(files_),work_);
    const auto header=WordBookHeaderReader::read(source);
    stamp.header_crc=source.crc(0,header.gw.offset+header.gw.bytes);
    const auto* w=info.header.data();const auto slash=path.find_last_of('/');
    if(!(info.section_offset && get(w + Wordbook::NameCrc32)==nameCrc(path.substr(slash==std::string_view::npos?0:slash+1)) &&
         get(w + Wordbook::SourceBytes)==stamp.bytes && get(w + Wordbook::FatMtime)==stamp.mtime && get(w + Wordbook::HeaderCrc32)==stamp.header_crc))
        throw std::runtime_error("Stale Wordbook index; reapply dataset");
    require(header.tm.offset==get(w + Wordbook::TmDataOffset) && header.tm.bytes==get(w + Wordbook::TmDataBytes) &&
            header.tt.offset==get(w + Wordbook::TtDataOffset) && header.tt.bytes==get(w + Wordbook::TtDataBytes) &&
            header.ti.offset==get(w + Wordbook::TiDataOffset) && header.ti.bytes==get(w + Wordbook::TiDataBytes));
    require(header.tokens==get(w + Wordbook::TokenCount) && header.token_pages==get(w + Wordbook::TokenPageCount) && header.tm_payload_bytes);
    require(header.components==get(w + Wordbook::ComponentCount) && header.page_size && header.text_pages==get(w + Wordbook::TextPageCount) &&
            header.token_bits==w[Wordbook::TokenBits] && header.tt_payload_bytes);
    require(header.depth==get16(w + Wordbook::Depth) && header.records==get(w + Wordbook::RecordCount) &&
            header.code_bits==w[Wordbook::CodeBits] && header.roots==get(w + Wordbook::RootCount));
}
void DatasetIndex::inspectWordBook(std::string_view path) const {
    Source source(drive_,path,observer_,false,nullptr,nullptr,available(files_),work_);
    (void)WordBookHeaderReader::read(source);
}

DatasetMapIndex DatasetIndex::inspectMap(std::string_view map) const {
    Source origins(drive_,map,observer_,false,nullptr,nullptr,available(files_),work_); const auto metadata=mapHeader(origins);
    Source chunks(drive_,map,observer_,false,nullptr,nullptr,available(files_),work_); chunks.seek(metadata.chunk.end);
    const auto next=u32(chunks.pos());
    if (matchesTag(chunks,"LAYO")) { const auto bytes=chunks.number(); chunks.skipInByte(bytes); }
    else chunks.seek(next);
    int64_t west=INT32_MAX,south=INT32_MAX,east=INT32_MIN,north=INT32_MIN;
    for (uint32_t i=0;i<metadata.count;++i) {
        require(origins.pos()<metadata.chunk.end);
        const auto x=origins.readMbInt(),y=origins.readMbInt();
        require(x>=INT32_MIN && x<=INT32_MAX && y>=INT32_MIN && y<=INT32_MAX);
        galuchat::GaluchatImageDataChunk01Reader image(chunks);
        require(x+image.width<=INT32_MAX && y+image.height<=INT32_MAX);
        west=std::min(west,x); south=std::min(south,y); east=std::max(east,x+image.width); north=std::max(north,y+image.height);
        image.skipToEnd();
    }
    require(origins.pos()==metadata.chunk.end && chunks.atEnd() && east-west<=INT32_MAX && north-south<=INT32_MAX);
    DatasetMapIndex info; put(info.header.data() + Header::ChunkCount,metadata.count); put(info.header.data() + Header::UnitInvX,metadata.unit_x); put(info.header.data() + Header::UnitInvY,metadata.unit_y);
    info.west=west; info.south=south; info.width=east-west; info.height=north-south; return info;
}

bool DatasetIndex::validate(std::string_view path) const {
    try { Source source(drive_,path,observer_,false,nullptr,nullptr,available(files_),work_); Validation(source).run(); return true; }
    catch (const std::exception&) { return false; }
}
DatasetError DatasetIndex::apply(std::string_view directory,const DatasetMetadata& metadata,
                                 SaveSelection save,void* context) const {
    if(!save)return DatasetError::SaveFailed;
    Progress progress{progress_,context_};progress.verifying=true;
    try {
        progress.report(IndexPhase::Preparing,0,1);require(metadata.error==DatasetError::None);
        BookSources book_storage(work_);auto& books=book_storage.table();
        readBookSources(drive_,directory,metadata,books,observer_,files_,work_);
        const auto map=fingerprint(drive_,TemporaryPath({directory,"/",metadata.map}).view(),"GLCH",observer_,files_,work_);
        const auto path=TemporaryPath({directory,"/_mapindex.dat"});
        bool current=false;
        if(exists(drive_,path.view())) {
            try {
                // Full structure/CRC and GLC identity, then all declared books.
                const auto summary=loadMap(path.view(),TemporaryPath({directory,"/",metadata.map}).view());
                require(get(summary.header.data()+Header::WordbookCount)==books.size());
                {
                    Source index(drive_,path.view(),observer_,false,nullptr,nullptr,available(files_),work_);
                    index.seek(u32(uint64_t(Header::Bytes)+get(summary.header.data()+Header::MapIndexBytes)));
                    for(const auto& book:books) {
                        DatasetWordBookIndex info;info.section_offset=u32(index.pos());
                        info.header=readRecord<Wordbook::Bytes>(index);
                        const std::string_view language(book.language);
                        require(language.size()<Wordbook::LanguageBytes &&
                                std::memcmp(info.header.data(),language.data(),language.size()+1)==0);
                        checkWordBook(info,TemporaryPath({directory,"/",book.name}).view());
                        index.seek(u32(uint64_t(info.section_offset)+get(info.header.data()+Wordbook::SectionBytes)));
                    }
                }
                checkSources(drive_,directory,metadata,map,books,observer_,files_,work_);
                current=true;
            } catch(const std::bad_alloc&) {throw;}
            catch(const std::exception&) {} // Incomplete/stale/corrupt: rebuild everything.
        }
        if(current) {
            if(!save(context))return DatasetError::SaveFailed;
            progress.report(IndexPhase::Complete,1,1);return DatasetError::None;
        }
    } catch(const std::bad_alloc&) {return DatasetError::NoMemory;}
    catch(const std::exception&) {return DatasetError::IndexFailed;}
    return build(directory,metadata,save,context);
}
DatasetError DatasetIndex::rebuild(std::string_view directory,const DatasetMetadata& metadata) const {
    return build(directory,metadata,nullptr,nullptr);
}
DatasetError DatasetIndex::verifyAgainstDataset(std::string_view directory,const DatasetMetadata& metadata) const {
    Progress progress{progress_,context_};progress.verifying=true;
    try {
        progress.report(IndexPhase::Preparing,0,1);require(metadata.error==DatasetError::None);
        BookSources book_storage(work_);auto& books=book_storage.table();
        readBookSources(drive_,directory,metadata,books,observer_,files_,work_);
        const auto map=fingerprint(drive_,TemporaryPath({directory,"/",metadata.map}).view(),"GLCH",observer_,files_,work_);
        const auto path=TemporaryPath({directory,"/_mapindex.dat"});require(validate(path.view()));
        progress.verifying=true;progress.phase=IndexPhase::Idle;
        generate(drive_,path.view(),directory,metadata,map,books,observer_,files_,&progress,true,work_);
        checkSources(drive_,directory,metadata,map,books,observer_,files_,work_);
        progress.report(IndexPhase::Complete,1,1);return DatasetError::None;
    }catch(const std::bad_alloc&) {return DatasetError::NoMemory;}
    catch(const std::exception&) {return DatasetError::IndexMismatch;}
}
DatasetError DatasetIndex::build(std::string_view directory,const DatasetMetadata& metadata,
                                 SaveSelection save,void* context) const {
    constexpr std::string_view temporary_name="/_mapindex.tmp",final_name="/_mapindex.dat",backup_name="/_mapindex.bak";
    if(!TemporaryPath::fits({directory,temporary_name}) || !TemporaryPath::fits({directory,final_name}) ||
       !TemporaryPath::fits({directory,backup_name}))return DatasetError::IndexFailed;
    const TemporaryPath temporary({directory,temporary_name}),final({directory,final_name}),backup({directory,backup_name});
    Progress progress{progress_,context_};
    bool old_moved=false,published=false;
    auto rollback=[&]() noexcept {
        try {
            SdAccessScope access(observer_);
            if (published && !removeFile(drive_,final.view())) return false;
            published=false;
            if (old_moved && !renameFile(drive_,backup.view(),final.view())) return false;
            old_moved=false; return true;
        } catch (const std::exception&) { return false; }
    };
    auto cleanup=[&](std::string_view path) noexcept {
        try { if (!path.empty()) { SdAccessScope access(observer_); (void)removeFile(drive_,path); } }
        catch (const std::exception&) {} // A leftover is never automatically adopted.
    };
    try {
        progress.report(IndexPhase::Preparing,0,1);require(metadata.error==DatasetError::None);
        { SdAccessScope access(observer_); require(removeFile(drive_,temporary.view())); }
        BookSources book_storage(work_);auto& books=book_storage.table();
        readBookSources(drive_,directory,metadata,books,observer_,files_,work_);
        const auto map_stamp=fingerprint(drive_,TemporaryPath({directory,"/",metadata.map}).view(),"GLCH",observer_,files_,work_);
        generate(drive_,temporary.view(),directory,metadata,map_stamp,books,observer_,files_,&progress,false,work_);
        progress.report(IndexPhase::Structure,0,1);require(validate(temporary.view()));
        // A second, read-only pass regenerates expected records and compares them before publication.
        progress.verifying=true;progress.phase=IndexPhase::Idle;
        generate(drive_,temporary.view(),directory,metadata,map_stamp,books,observer_,files_,&progress,true,work_);
        checkSources(drive_,directory,metadata,map_stamp,books,observer_,files_,work_);
        progress.verifying=false;progress.report(IndexPhase::Publish,0,1);
        {
            SdAccessScope access(observer_);
            // Explicit Apply supersedes stale leftovers only after a new index is valid.
            if (exists(drive_,final.view())) {
                require(removeFile(drive_,backup.view()));
                require(renameFile(drive_,final.view(),backup.view())); old_moved=true;
            }
            require(renameFile(drive_,temporary.view(),final.view())); published=true;
        }
        if (save && !save(context)) {
            return rollback() ? DatasetError::SaveFailed : DatasetError::IndexRestoreFailed;
        }
        // Selection is committed; a leftover backup may be removed on the next Apply.
        old_moved=false; published=false;
        cleanup(backup.view());
        progress.report(IndexPhase::Complete,1,1);
        return DatasetError::None;
    } catch (const std::bad_alloc&) {
        const bool restored=rollback();
        cleanup(temporary.view());
        return restored ? DatasetError::NoMemory : DatasetError::IndexRestoreFailed;
    } catch (const std::exception&) {
        const bool restored=rollback();
        cleanup(temporary.view());
        return restored ? DatasetError::IndexFailed : DatasetError::IndexRestoreFailed;
    }
}
} // namespace geoputer

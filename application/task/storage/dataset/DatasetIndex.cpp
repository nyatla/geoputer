#include "DatasetIndex.h"
#include "DatasetIndexFormat.h"
#include "../log/LogFile.h"
#include "../galuchat/ReadTiming.h"
#include <galuchat/wgsmap3_reader.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <ctime>
#include <memory>
#include <stdexcept>
#include <vector>
#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace geoputer {
namespace {
using namespace index_format;
using galuchat::ByteReader;
void require(bool value) { if (!value) throw std::runtime_error("Invalid dataset index or source"); }
uint32_t u32(uint64_t value) { require(value<=UINT32_MAX); return static_cast<uint32_t>(value); }

uint32_t crcUpdate(uint32_t crc,const uint8_t* data,size_t size) {
    for (size_t i=0;i<size;++i) {
        crc^=data[i];
        for (unsigned bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xedb88320u:0u);
    }
    return crc;
}
uint32_t nameCrc(const std::string& name) {
    return crcUpdate(UINT32_MAX,reinterpret_cast<const uint8_t*>(name.data()),name.size())^UINT32_MAX;
}
struct File : LogFile {
    File(uint8_t drive,StorageObserver* observer,FileIoTiming* timing=nullptr):observer_(observer),timing_(timing) { setDrive(drive); }
    ~File() { if (opened()) { SdAccessScope access(observer_); FileIoTime elapsed(timing_,&FileIoTiming::close_us); if(timing_)++timing_->closes; close(); } }
    File(const File&)=delete;
    File& operator=(const File&)=delete;
private:
    StorageObserver* observer_;
    FileIoTiming* timing_;
};
std::string nativePath(uint8_t drive,const std::string& path) {
#ifdef ARDUINO_ARCH_ESP32
    require(path.compare(0,4,"/sd/")==0);
    return std::to_string(drive)+":"+path.substr(3);
#else
    (void)drive; return path;
#endif
}
// Missing files and IO errors are distinct: never overwrite an unreadable old index.
bool exists(uint8_t drive,const std::string& path) {
#ifdef ARDUINO_ARCH_ESP32
    FILINFO info{}; const auto result=f_stat(nativePath(drive,path).c_str(),&info);
    require(result==FR_OK || result==FR_NO_FILE || result==FR_NO_PATH);
    if (result==FR_OK) require(!(info.fattrib&AM_DIR));
    return result==FR_OK;
#else
    struct stat info{};
    if (::stat(nativePath(drive,path).c_str(),&info)==0) { require(S_ISREG(info.st_mode)); return true; }
    require(errno==ENOENT); return false;
#endif
}
bool removeFile(uint8_t drive,const std::string& path) {
#ifdef ARDUINO_ARCH_ESP32
    const auto result=f_unlink(nativePath(drive,path).c_str());
    return result==FR_OK || result==FR_NO_FILE || result==FR_NO_PATH;
#else
    return std::remove(nativePath(drive,path).c_str())==0 || errno==ENOENT;
#endif
}
bool renameFile(uint8_t drive,const std::string& from,const std::string& to) {
#ifdef ARDUINO_ARCH_ESP32
    return f_rename(nativePath(drive,from).c_str(),nativePath(drive,to).c_str())==FR_OK;
#else
    return std::rename(nativePath(drive,from).c_str(),nativePath(drive,to).c_str())==0;
#endif
}
struct Stamp {
    uint32_t bytes=0,mtime=0,header_crc=0;
    bool operator==(const Stamp& rhs) const { return bytes==rhs.bytes && mtime==rhs.mtime && header_crc==rhs.header_crc; }
};
Stamp statFile(uint8_t drive,const std::string& path) {
    Stamp result;
#ifdef ARDUINO_ARCH_ESP32
    FILINFO info{}; require(f_stat(nativePath(drive,path).c_str(),&info)==FR_OK && !(info.fattrib&AM_DIR));
    result.bytes=u32(info.fsize); result.mtime=uint32_t(info.fdate)<<16 | info.ftime;
#else
    struct stat info{}; require(::stat(nativePath(drive,path).c_str(),&info)==0 && S_ISREG(info.st_mode) && info.st_size>=0);
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
    IndexPhase phase=IndexPhase::Idle;std::string language;int percent=-1;bool verifying=false;
    void report(IndexPhase next,uint64_t done,uint64_t total,const std::string& lang="") {
        const int value=total?int(std::min<uint64_t>(100,done*100/total)):0;
        if(next==phase && lang==language && value<=percent)return;
        phase=next;language=lang;percent=value;
        if(callback) {IndexProgress p{};p.phase=next;p.percent=value;p.verifying=verifying;
            std::snprintf(p.language,sizeof(p.language),"%s",lang.c_str());callback(context,p);}
    }
};
class Source final : public ByteReader {
public:
    Source(uint8_t drive,const std::string& path,StorageObserver* observer,bool through=false,FileIoTiming* timing=nullptr)
        : file_(std::make_unique<File>(drive,observer,timing)),observer_(observer),buffer_(config::IndexReadBufferBytes),through_(through),timing_(timing) {
        SdAccessScope access(observer_); uint64_t length=0;
        FileIoTime elapsed(timing_,&FileIoTiming::open_us); if(timing_)++timing_->opens;
        require(file_->openRead(path) && file_->size(length)); size_=u32(length);
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
            require(file_->seek(offset));
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
          require(file_->read(buffer_.data(),buffer_used_)); if(timing_)timing_->bytes+=buffer_used_; }
        if(progress_)progress_->report(IndexPhase::Map,uint64_t(position_)+buffer_used_,size_);
#ifdef ARDUINO_ARCH_ESP32
        vTaskDelay(1); // Keep input / drawing tasks responsive throughout full-map scans.
#endif
    }
    std::unique_ptr<File> file_;
    StorageObserver* observer_;
    std::vector<uint8_t> buffer_;
    uint32_t size_=0,position_=0,buffer_start_=0;
    size_t buffer_used_=0;
    bool through_;
    Progress* progress_=nullptr;
    FileIoTiming* timing_;
};
class Output {
public:
    Output(uint8_t drive,const std::string& path,StorageObserver* observer,bool compare=false)
        : file_(std::make_unique<File>(drive,observer)),observer_(observer),buffer_(config::IndexReadBufferBytes) {
        SdAccessScope access(observer_);
        if(compare) comparison_=std::make_unique<Source>(drive,path,observer);
        else require(file_->open(path,true));
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
    std::unique_ptr<File> file_;
    StorageObserver* observer_;
    std::vector<uint8_t> buffer_;
    size_t used_=0;
    uint32_t position_=0;
    std::unique_ptr<Source> comparison_;
};
std::string tag(Source& reader) { return galuchat::detail::readAscii(reader,4); }
struct Chunk { uint32_t start=0,data=0,bytes=0,end=0; };
Chunk chunkHeader(Source& source,const char* expected) {
    Chunk c; c.start=u32(source.pos()); require(tag(source)==expected); c.bytes=source.number();
    c.data=u32(source.pos()); c.end=u32(uint64_t(c.data)+c.bytes); require(c.end<=source.length()); return c;
}
Stamp fingerprint(uint8_t drive,const std::string& path,const char* magic,StorageObserver* observer) {
    SdAccessScope access(observer); auto stamp=statFile(drive,path);
    Source source(drive,path,observer); const auto header=chunkHeader(source,magic);
    stamp.header_crc=source.crc(0,header.end); return stamp;
}
void setStamp(uint8_t* out,const std::string& name,const Stamp& stamp) {
    put(out,nameCrc(name)); put(out+4,stamp.bytes); put(out+8,stamp.mtime); put(out+12,stamp.header_crc);
}

void mapIndex(Output& out,Source& source,std::array<uint8_t,64>& header,Progress* progress=nullptr) {
    const auto glch=chunkHeader(source,"GLCH");
    galuchat::detail::LimitedReader payload(source,glch.bytes);
    require(galuchat::detail::readBStrAscii(payload,16)=="WGSMapSet/3");
    const auto unit_x=u32(payload.readMbUInt()),unit_y=u32(payload.readMbUInt());
    require(unit_x && unit_y && unit_x<=INT32_MAX && unit_y<=INT32_MAX); put(header.data() + Header::UnitInvX,unit_x); put(header.data() + Header::UnitInvY,unit_y);
    const auto metadata=u32(payload.readMbUInt()); require(metadata<=payload.remaining()); payload.skipInByte(metadata);
    const auto count=u32(payload.readMbUInt());
    require(count && count<=config::MaxDatasetChunks && count<=payload.remaining()/2);
    struct Origin { int32_t x,y; };
    std::vector<Origin> origins; origins.reserve(count); // Only the bounded GLCH coordinates, never all blocks.
    for (uint32_t i=0;i<count;++i) {
        const auto x=payload.readMbInt(),y=payload.readMbInt();
        require(x>=INT32_MIN && x<=INT32_MAX && y>=INT32_MIN && y<=INT32_MAX);
        origins.push_back({static_cast<int32_t>(x),static_cast<int32_t>(y)});
    }
    require(payload.atEnd()); put(header.data() + Header::ChunkCount,count);
    const auto next=u32(source.pos());
    if (tag(source)=="LAYO") { const auto bytes=source.number(); source.skipInByte(bytes); }
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
void wordbookIndex(Output& out,Source& source,const DatasetMetadata& metadata,const Stamp& stamp,Progress* progress=nullptr) {
    std::array<Chunk,4> chunks{};
    const char* names[]={"GW00","TM00","TT00","TI00"};
    for (size_t i=0;i<chunks.size();++i) { chunks[i]=chunkHeader(source,names[i]); source.seek(chunks[i].end); }
    require(source.atEnd()); source.seek(chunks[0].data);
    require(galuchat::detail::readBStrAscii(source,16)=="GisWordBook/0");
    const auto gw_metadata=source.number(); require(uint64_t(source.pos())+gw_metadata==chunks[0].end);
    std::array<uint8_t,Wordbook::Bytes> record{};
    require(!metadata.language.empty() && metadata.language.size()<32);
    for (unsigned char c:metadata.language) require(c>=0x21 && c<0x7f);
    std::memcpy(record.data(),metadata.language.data(),metadata.language.size());
    setStamp(record.data() + Wordbook::NameCrc32,metadata.wordbook,stamp);
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
        if(progress)progress->report(IndexPhase::TI,i,roots,metadata.language);
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
        if(progress)progress->report(IndexPhase::TT,i,pages,metadata.language);
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
        if(progress)progress->report(IndexPhase::TM,i,token_pages,metadata.language);
        require(tm.readByte()==0);
        const auto bytes=u32(tm.readMbUInt()),count=u32(tm.readMbUInt()),stream_bytes=u32(tm.readMbUInt());
        require(bytes>0 && bytes>=previous_bytes && count>0 && count<=tokens-first_token && uint64_t(bytes)*count==stream_bytes && stream_bytes<=tm.remaining());
        std::array<uint8_t,TmPage::Bytes> page{}; put(page.data(),first_token); put(page.data() + TmPage::TokenCount,count); put(page.data() + TmPage::TokenBytes,bytes); put(page.data() + TmPage::DataOffset,u32(source.pos())); out.write(page);
        tm.skipInByte(stream_bytes); first_token+=count; previous_bytes=bytes;
    }
    require(first_token==tokens && tm.atEnd());
    const auto end=out.pos(); put(record.data() + Wordbook::SectionBytes,end-start); out.seek(start); out.write(record); out.seek(end);
}
struct BookSource {std::string language,name;Stamp stamp;};
std::vector<BookSource> bookSources(uint8_t drive,const std::string& directory,
                                    const DatasetMetadata& metadata,StorageObserver* observer) {
    auto languages=metadata.languages;
    if(languages.empty())languages.push_back(metadata.language);
    std::sort(languages.begin(),languages.end());
    require(std::adjacent_find(languages.begin(),languages.end())==languages.end());
    std::vector<BookSource> result;
    for(const auto& language:languages) {
        auto selected=language==metadata.language?metadata:readDatasetMetadata(directory,UiLanguage::Japanese,language.c_str(),observer);
        require(selected.error==DatasetError::None && selected.map==metadata.map && !selected.wordbook.empty());
        result.push_back({language,selected.wordbook,fingerprint(drive,directory+"/"+selected.wordbook,"GW00",observer)});
    }
    return result;
}
struct ReusePlan {
    std::string path;
    DatasetMapIndex map;
    bool use_map=false;
    std::vector<DatasetWordBookIndex> books; // Headers only, never root/page tables.
};
void copyIndex(Output& out,Source& source,uint32_t offset,uint32_t count) {
    source.seek(offset);std::array<uint8_t,512> bytes{};
    while(count) {
        const size_t n=std::min<size_t>(count,bytes.size());
        for(size_t i=0;i<n;++i)bytes[i]=source.readByte();
        out.write(bytes.data(),n);count-=n;
    }
}
void generatePlanned(uint8_t drive,const std::string& path,const std::string& directory,const DatasetMetadata& metadata,
              const Stamp& map_stamp,const std::vector<BookSource>& books,StorageObserver* observer,
              const ReusePlan& reuse,Progress* progress=nullptr,bool compare=false) {
    Output out(drive,path,observer,compare);
    std::unique_ptr<Source> old;
    if(reuse.use_map || std::any_of(reuse.books.begin(),reuse.books.end(),[](const auto& b){return b.section_offset!=0;}))
        old=std::make_unique<Source>(drive,reuse.path,observer);
    std::array<uint8_t,Header::Bytes> header{};std::memcpy(header.data(),"RGPMIDX",8);
    put16(header.data() + Header::Version,1);put16(header.data() + Header::HeaderBytes,64);put16(header.data() + Header::ChunkRecordBytes,40);put16(header.data() + Header::GroupRecordBytes,16);
    put(header.data() + Header::MapBytes,map_stamp.bytes);put(header.data() + Header::MapFatMtime,map_stamp.mtime);
    put(header.data() + Header::MapNameCrc32,nameCrc(metadata.map));put(header.data() + Header::MapHeaderCrc32,map_stamp.header_crc);
    put(header.data() + Header::IndexedChunkMinBytes,config::IndexMinChunkBytes);put(header.data() + Header::GroupTargetBytes,config::IndexGroupTargetBytes);put(header.data() + Header::WordbookCount,u32(books.size()));
    out.placeholder(header);
    if(progress)progress->report(IndexPhase::Map,0,1);
    if(reuse.use_map) {
        for(size_t offset:{size_t(36),size_t(40),size_t(44),size_t(60)})put(header.data()+offset,get(reuse.map.header.data()+offset));
        copyIndex(out,*old,64,get(header.data() + Header::MapIndexBytes));
    } else {Source source(drive,directory+"/"+metadata.map,observer,true);source.setProgress(progress);mapIndex(out,source,header,progress);}
    if(progress)progress->report(IndexPhase::Map,1,1);
    for(size_t i=0;i<books.size();++i) {
        const auto& book=books[i];
        if(i<reuse.books.size() && reuse.books[i].section_offset) {
            if(progress)progress->report(IndexPhase::TM,1,1,book.language);
            copyIndex(out,*old,reuse.books[i].section_offset,get(reuse.books[i].header.data() + Header::IndexedChunkMinBytes));
        } else {
            DatasetMetadata selected;selected.language=book.language;selected.wordbook=book.name;
            Source source(drive,directory+"/"+book.name,observer);wordbookIndex(out,source,selected,book.stamp,progress);
            if(progress)progress->report(IndexPhase::TM,1,1,book.language);
        }
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
    void run(DatasetMapIndex* summary=nullptr,DatasetWordBookIndex* book=nullptr,const std::string& selected_language="") {
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
        std::string previous_language;
        for (uint32_t section=0;section<get(h + Header::WordbookCount);++section) {
            const auto begin=source_.pos(); const auto record=read<Wordbook::Bytes>(); const auto* w=record.data();
            size_t language_bytes=0;
            while (language_bytes<32 && w[language_bytes]) { require(w[language_bytes]>=0x21 && w[language_bytes]<0x7f); ++language_bytes; }
            require(language_bytes && language_bytes<32);
            for (size_t i=language_bytes;i<32;++i) require(w[i]==0);
            const std::string language(reinterpret_cast<const char*>(w),language_bytes);
            require(previous_language.empty() || previous_language<language); previous_language=language;
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

ReusePlan reusePlan(uint8_t drive,const std::string& directory,const DatasetMetadata& metadata,
                    const std::vector<BookSource>& books,IndexTarget target,StorageObserver* observer) {
    ReusePlan result;result.path=directory+"/_mapindex.dat";result.books.resize(books.size());
    if(target==IndexTarget::All)return result;
    DatasetMapIndex summary;
    try {Source source(drive,result.path,observer);Validation(source).run(&summary);}
    catch(const std::exception&) {
        if(target!=IndexTarget::Reuse)throw;
        return result;
    }
    if(target!=IndexTarget::Map) {
        try {result.map=DatasetIndex(drive,observer).loadMap(result.path,directory+"/"+metadata.map);result.use_map=true;}
        catch(const std::exception&) {if(target==IndexTarget::WordBooks)throw;}
    }
    if(target!=IndexTarget::WordBooks) {
        Source source(drive,result.path,observer);source.seek(u32(uint64_t(64)+get(summary.header.data() + Header::MapIndexBytes)));
        for(uint32_t i=0;i<get(summary.header.data() + Header::WordbookCount);++i) {
            DatasetWordBookIndex section;section.section_offset=u32(source.pos());
            for(auto& b:section.header)b=source.readByte();
            for(size_t j=0;j<books.size();++j)if(books[j].language==reinterpret_cast<const char*>(section.header.data())) {
                try {(void)DatasetIndex(drive,observer).checkWordBook(section,directory+"/"+books[j].name);result.books[j]=section;}
                catch(const std::exception&) {if(target==IndexTarget::Map)throw;}
            }
            source.seek(u32(uint64_t(section.section_offset)+get(section.header.data() + Header::IndexedChunkMinBytes)));
        }
        if(target==IndexTarget::Map)for(const auto& b:result.books)require(b.section_offset!=0);
    }
    return result;
}
void checkSources(uint8_t drive,const std::string& directory,const DatasetMetadata& metadata,
                  const Stamp& map,const std::vector<BookSource>& books,StorageObserver* observer) {
    require(map==fingerprint(drive,directory+"/"+metadata.map,"GLCH",observer));
    for(const auto& b:books)require(b.stamp==fingerprint(drive,directory+"/"+b.name,"GW00",observer));
}
struct MapHeader {
    Chunk chunk;
    uint32_t count,unit_x,unit_y;
};
MapHeader mapHeader(Source& source) {
    const auto chunk=chunkHeader(source,"GLCH");
    require(galuchat::detail::readBStrAscii(source,16)=="WGSMapSet/3");
    const auto x=source.number(),y=source.number(),metadata=source.number();
    require(x && y && x<=INT32_MAX && y<=INT32_MAX && uint64_t(source.pos())+metadata<chunk.end);
    source.skipInByte(metadata); const auto count=source.number();
    require(count && uint64_t(source.pos())+uint64_t(count)*2<=chunk.end);
    if (count>config::MaxDatasetChunks) throw MapChunkLimitExceeded(count,config::MaxDatasetChunks);
    return {chunk,count,x,y};
}
} // namespace

DatasetMapIndex DatasetIndex::loadMap(const std::string& path,const std::string& map,FileIoTiming* map_timing,FileIoTiming* index_timing,
                                     DatasetWordBookIndex* book,const std::string& language) const {
    Stamp stamp;
    MapHeader metadata;
    {
        SdAccessScope access(observer_);
        { FileIoTime elapsed(map_timing,&FileIoTiming::stat_us); if(map_timing)++map_timing->stats; stamp=statFile(drive_,map); }
        Source source(drive_,map,observer_,false,map_timing); metadata=mapHeader(source);
        stamp.header_crc=source.crc(0,metadata.chunk.end);
    }
    DatasetMapIndex info;
    { FileIoTime elapsed(index_timing,&FileIoTiming::stat_us); if(index_timing)++index_timing->stats;
      if(!exists(drive_,path)) throw std::runtime_error("Missing _mapindex.dat; reapply dataset"); }
    try { Source index(drive_,path,observer_,false,index_timing); Validation(index).run(&info,book,language); }
    catch(const std::bad_alloc&) { throw; }
    catch(const std::exception&) { throw std::runtime_error("Invalid _mapindex.dat; reapply dataset"); }
    if (book && !book->section_offset) throw std::runtime_error("Missing Wordbook index; reapply dataset");
    const auto* h=info.header.data(); const auto slash=map.find_last_of('/');
    if (!(get(h + Header::MapBytes)==stamp.bytes && get(h + Header::MapFatMtime)==stamp.mtime && get(h + Header::MapNameCrc32)==nameCrc(map.substr(slash==std::string::npos?0:slash+1)) && get(h + Header::MapHeaderCrc32)==stamp.header_crc &&
          get(h + Header::ChunkCount)==metadata.count && get(h + Header::UnitInvX)==metadata.unit_x && get(h + Header::UnitInvY)==metadata.unit_y))
        throw std::runtime_error("Stale _mapindex.dat; reapply dataset");
    return info;
}
std::string DatasetIndex::checkWordBook(const DatasetWordBookIndex& info,const std::string& path,FileIoTiming* timing) const {
    SdAccessScope access(observer_);
    Stamp stamp;
    { FileIoTime elapsed(timing,&FileIoTiming::stat_us); if(timing)++timing->stats; stamp=statFile(drive_,path); }
    Source source(drive_,path,observer_,false,timing);
    std::array<Chunk,4> chunks{};
    const char* names[]={"GW00","TM00","TT00","TI00"};
    for(size_t i=0;i<chunks.size();++i) {chunks[i]=chunkHeader(source,names[i]);source.seek(chunks[i].end);}
    require(source.atEnd());
    stamp.header_crc=source.crc(0,chunks[0].end);
    const auto* w=info.header.data();const auto slash=path.find_last_of('/');
    if(!(info.section_offset && get(w + Wordbook::NameCrc32)==nameCrc(path.substr(slash==std::string::npos?0:slash+1)) &&
         get(w + Wordbook::SourceBytes)==stamp.bytes && get(w + Wordbook::FatMtime)==stamp.mtime && get(w + Wordbook::HeaderCrc32)==stamp.header_crc))
        throw std::runtime_error("Stale Wordbook index; reapply dataset");
    require(chunks[1].data==get(w + Wordbook::TmDataOffset) && chunks[1].bytes==get(w + Wordbook::TmDataBytes) &&
            chunks[2].data==get(w + Wordbook::TtDataOffset) && chunks[2].bytes==get(w + Wordbook::TtDataBytes) &&
            chunks[3].data==get(w + Wordbook::TiDataOffset) && chunks[3].bytes==get(w + Wordbook::TiDataBytes));
    source.seek(chunks[0].data);
    require(galuchat::detail::readBStrAscii(source,16)=="GisWordBook/0");
    const auto length=source.number();require(uint64_t(source.pos())+length==chunks[0].end);
    const auto metadata=galuchat::detail::readAscii(source,length);
    source.seek(chunks[1].data);
    const auto tokens=source.number(),token_pages=source.number();
    require(tokens==get(w + Wordbook::TokenCount) && token_pages==get(w + Wordbook::TokenPageCount) && source.pos()<chunks[1].end);
    source.seek(chunks[2].data);
    const auto components=source.number(),page_size=source.number(),pages=source.number(),token_bits=source.number();
    require(components==get(w + Wordbook::ComponentCount) && page_size>0 && pages==get(w + Wordbook::TextPageCount) && token_bits==w[Wordbook::TokenBits] && source.pos()<chunks[2].end);
    source.seek(chunks[3].data);
    const auto depth=source.number(),records=source.number(),bits=source.number(),roots=source.number(),stream=source.number();
    require(depth==get16(w + Wordbook::Depth) && records==get(w + Wordbook::RecordCount) && bits==w[Wordbook::CodeBits] && roots==get(w + Wordbook::RootCount) &&
            uint64_t(source.pos())+stream==chunks[3].end);
    return metadata;
}
DatasetMapIndex DatasetIndex::inspectMap(const std::string& map) const {
    Source origins(drive_,map,observer_); const auto metadata=mapHeader(origins);
    Source chunks(drive_,map,observer_); chunks.seek(metadata.chunk.end);
    const auto next=u32(chunks.pos());
    if (tag(chunks)=="LAYO") { const auto bytes=chunks.number(); chunks.skipInByte(bytes); }
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

bool DatasetIndex::validate(const std::string& path) const {
    try { Source source(drive_,path,observer_); Validation(source).run(); return true; }
    catch (const std::exception&) { return false; }
}
DatasetError DatasetIndex::apply(const std::string& directory,const DatasetMetadata& metadata,
                                 SaveSelection save,void* context) const {
    if(!save)return DatasetError::SaveFailed;
    return build(directory,metadata,IndexTarget::Reuse,save,context);
}
DatasetError DatasetIndex::rebuild(const std::string& directory,const DatasetMetadata& metadata,IndexTarget target) const {
    return build(directory,metadata,target,nullptr,nullptr);
}
DatasetError DatasetIndex::verifyAgainstDataset(const std::string& directory,const DatasetMetadata& metadata,IndexTarget target) const {
    Progress progress{progress_,context_};progress.verifying=true;
    try {
        progress.report(IndexPhase::Preparing,0,1);require(metadata.error==DatasetError::None);
        const auto books=bookSources(drive_,directory,metadata,observer_);
        const auto map=fingerprint(drive_,directory+"/"+metadata.map,"GLCH",observer_);
        const auto path=directory+"/_mapindex.dat";require(validate(path));
        // Copying the unselected part compares it with itself; selected parts are regenerated from source.
        const auto reuse=reusePlan(drive_,directory,metadata,books,target==IndexTarget::Map?IndexTarget::Map:
            target==IndexTarget::WordBooks?IndexTarget::WordBooks:IndexTarget::All,observer_);
        progress.verifying=true;progress.phase=IndexPhase::Idle;
        generatePlanned(drive_,path,directory,metadata,map,books,observer_,reuse,&progress,true);
        checkSources(drive_,directory,metadata,map,books,observer_);
        progress.report(IndexPhase::Complete,1,1);return DatasetError::None;
    }catch(const std::bad_alloc&) {return DatasetError::NoMemory;}
    catch(const std::exception&) {return DatasetError::IndexMismatch;}
}
DatasetError DatasetIndex::build(const std::string& directory,const DatasetMetadata& metadata,IndexTarget target,
                                 SaveSelection save,void* context) const {
    Progress progress{progress_,context_};
    std::string temporary,final,backup;
    bool old_moved=false,published=false;
    auto rollback=[&]() noexcept {
        try {
            SdAccessScope access(observer_);
            if (published && !removeFile(drive_,final)) return false;
            published=false;
            if (old_moved && !renameFile(drive_,backup,final)) return false;
            old_moved=false; return true;
        } catch (const std::exception&) { return false; }
    };
    auto cleanup=[&](const std::string& path) noexcept {
        try { if (!path.empty()) { SdAccessScope access(observer_); (void)removeFile(drive_,path); } }
        catch (const std::exception&) {} // A leftover is never automatically adopted.
    };
    try {
        temporary=directory+"/_mapindex.tmp"; final=directory+"/_mapindex.dat"; backup=directory+"/_mapindex.bak";
        progress.report(IndexPhase::Preparing,0,1);require(metadata.error==DatasetError::None);
        { SdAccessScope access(observer_); require(removeFile(drive_,temporary)); }
        const auto books=bookSources(drive_,directory,metadata,observer_);
        const auto map_stamp=fingerprint(drive_,directory+"/"+metadata.map,"GLCH",observer_);
        const auto reuse=reusePlan(drive_,directory,metadata,books,target,observer_);
        generatePlanned(drive_,temporary,directory,metadata,map_stamp,books,observer_,reuse,&progress);
        progress.report(IndexPhase::Structure,0,1);require(validate(temporary));
        // A second, read-only pass regenerates expected records and compares them before publication.
        progress.verifying=true;progress.phase=IndexPhase::Idle;
        generatePlanned(drive_,temporary,directory,metadata,map_stamp,books,observer_,reuse,&progress,true);
        checkSources(drive_,directory,metadata,map_stamp,books,observer_);
        progress.verifying=false;progress.report(IndexPhase::Publish,0,1);
        {
            SdAccessScope access(observer_);
            // Explicit Apply supersedes stale leftovers only after a new index is valid.
            if (exists(drive_,final)) {
                require(removeFile(drive_,backup));
                require(renameFile(drive_,final,backup)); old_moved=true;
            }
            require(renameFile(drive_,temporary,final)); published=true;
        }
        if (save && !save(context)) {
            return rollback() ? DatasetError::SaveFailed : DatasetError::IndexRestoreFailed;
        }
        // Selection is committed; a leftover backup may be removed on the next Apply.
        old_moved=false; published=false;
        cleanup(backup);
        progress.report(IndexPhase::Complete,1,1);
        return DatasetError::None;
    } catch (const std::bad_alloc&) {
        const bool restored=rollback();
        cleanup(temporary);
        return restored ? DatasetError::NoMemory : DatasetError::IndexRestoreFailed;
    } catch (const std::exception&) {
        const bool restored=rollback();
        cleanup(temporary);
        return restored ? DatasetError::IndexFailed : DatasetError::IndexRestoreFailed;
    }
}
} // namespace geoputer

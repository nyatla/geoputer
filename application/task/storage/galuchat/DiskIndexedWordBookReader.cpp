#include "DiskIndexedWordBookReader.h"
#include "../dataset/DatasetIndexFormat.h"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include "../../../Config.h"
#include "PlaceLineOutput.h"

namespace geoputer {
namespace {
using namespace index_format;
using Decoder = galuchat::GaluchatGisWordBookReader;
void check(bool value) {
    if (!value) throw std::runtime_error("Wordbook index changed or invalid; revalidate dataset");
}

size_t size(uint64_t value) { check(value <= SIZE_MAX); return static_cast<size_t>(value); }
// Each phase owns a bounded stack plan. The plan's index Reader ends before
// any body Reader opens. Larger selections rescan just that section in batches.
struct PageRange {
    // TT stores page bytes; TM stores the fixed byte width of each fragment.
    uint32_t first, count, offset, width_or_bytes;
    uint32_t begin, end;
};
constexpr size_t BatchPages = config::WordBookReadPageCacheEntries;
static_assert(sizeof(PageRange) == 24, "References have six uint32 fields");
static_assert(sizeof(PageRange) * BatchPages <= 768, "Review phase stack budget");
struct PagePlan {
    std::array<PageRange, BatchPages> pages;
    size_t count = 0;
    bool more = false;
};
static_assert(sizeof(PagePlan) == 24 * BatchPages + 2 * sizeof(size_t),
              "Review phase metadata and padding");
void remember(PagePlan& plan,uint64_t& matches,uint64_t skip,const PageRange& page) {
    if(matches++<skip)return;
    if(plan.count==plan.pages.size()){plan.more=true;return;}
    plan.pages[plan.count++]=page;
}
size_t readPath(const DatasetWordBookIndex& info,const ScopedReaderSource& index_source,
                const ScopedReaderSource& book,std::array<uint32_t,config::IndexMaxDepth>& codes,uint32_t code) {
    const auto* w=info.header.data();const uint32_t roots=get(w+Wordbook::RootCount);
    const auto depth=get16(w+Wordbook::Depth);
    check(depth && depth<=codes.size() && roots && w[Wordbook::CodeBits]>0 &&
          w[Wordbook::CodeBits]<=31 && w[Wordbook::TokenBits]>0 && w[Wordbook::TokenBits]<=16);
    const uint64_t ti_end=uint64_t(get(w+Wordbook::TiDataOffset))+get(w+Wordbook::TiDataBytes);
    uint64_t covered=0,previous=0,selected_bit=0,end_bit=ti_end*8;
    uint32_t selected_count=0,selected_first=0;bool await_end=false,final=false;
    index_source.withRange(info.section_offset,size(uint64_t(Wordbook::Bytes)+uint64_t(roots)*TiRoot::Bytes),[&](galuchat::ByteReader& reader) {
        check(readRecord<Wordbook::Bytes>(reader)==info.header);
        for(uint32_t i=0;i<roots;++i) {
            const auto r=readRecord<TiRoot::Bytes>(reader);
            const auto first=get(r.data()),count=get(r.data()+TiRoot::RecordCount),offset=get(r.data()+TiRoot::DataOffset);
            const uint64_t bit=uint64_t(offset)*8+r[TiRoot::StartBit];
            check(first==covered && count && count<=uint64_t(get(w+Wordbook::RecordCount))-covered &&
                  r[TiRoot::StartBit]<8 && !r[13] && !r[14] && !r[15] &&
                  bit>=uint64_t(get(w+Wordbook::TiDataOffset))*8 && bit<ti_end*8 && (!i || bit>previous));
            if(await_end){end_bit=bit;await_end=false;}
            if(uint64_t(code)-1>=first && uint64_t(code)-1-first<count) {
                selected_first=first;selected_count=count;selected_bit=bit;await_end=true;final=i+1==roots;
            }
            covered+=count;previous=bit;
        }
        check(covered==get(w+Wordbook::RecordCount) && selected_count && end_bit>selected_bit && reader.atEnd());
    });
    const uint64_t offset=selected_bit/8;
    book.withRange(size(offset),size((end_bit+7)/8-offset),[&](galuchat::ByteReader& reader) {
        Decoder::readCodeSetFromRootInto(reader,{depth,selected_count,int(w[Wordbook::CodeBits]),
            size(selected_bit%8),size(end_bit-selected_bit),final},code-1-selected_first,
            codes.data(),codes.size());
    });
    for(unsigned i=0;i<depth;++i)check(codes[i]<get(w+Wordbook::ComponentCount));
    return depth;
}
size_t readComponentTokens(const DatasetWordBookIndex& info,const ScopedReaderSource& index,
                           const ScopedReaderSource& book,uint32_t code,WordBookWorkspace& scratch) {
    const auto* w=info.header.data();const auto roots=get(w+Wordbook::RootCount),pages=get(w+Wordbook::TextPageCount);
    const uint64_t tt_end=uint64_t(get(w+Wordbook::TtDataOffset))+get(w+Wordbook::TtDataBytes);
    const uint64_t offset=uint64_t(info.section_offset)+Wordbook::Bytes+uint64_t(roots)*TiRoot::Bytes;
    PageRange selected{};
    index.withRange(size(offset),size(uint64_t(pages)*TtPage::Bytes),[&](galuchat::ByteReader& reader) {
        check(pages);auto current=readRecord<TtPage::Bytes>(reader);
        for(uint32_t i=0;i<pages;++i) {
            const auto first=get(current.data()),start=get(current.data()+TtPage::DataOffset);
            const auto next=i+1<pages?readRecord<TtPage::Bytes>(reader):std::array<uint8_t,TtPage::Bytes>{};
            const uint64_t next_code=i+1<pages?get(next.data()):get(w+Wordbook::ComponentCount);
            const uint64_t next_offset=i+1<pages?get(next.data()+TtPage::DataOffset):tt_end;
            check((i || !first) && first<next_code && next_code<=get(w+Wordbook::ComponentCount) &&
                  start>=get(w+Wordbook::TtDataOffset) && start<next_offset && next_offset<=tt_end);
            if(code>=first && code<next_code) {
                selected={first,uint32_t(next_code-first),start,uint32_t(next_offset-start),0,0};return;
            }
            current=next;
        }
    });
    check(selected.count);size_t count=0;
    book.withRange(selected.offset,selected.width_or_bytes,[&](galuchat::ByteReader& reader) {
        count=Decoder::readTokenIdsFromPageInto(reader,selected.width_or_bytes,selected.first,selected.count,
            w[Wordbook::TokenBits],code,scratch.tokens.data(),scratch.tokens.size());
    });
    for(size_t i=0;i<count;++i) {
        check(scratch.tokens[i]<get(w+Wordbook::TokenCount));scratch.order[i]=static_cast<uint16_t>(i);
    }
    std::sort(scratch.order.begin(),scratch.order.begin()+count,[&](uint16_t a,uint16_t b){return scratch.tokens[a]<scratch.tokens[b];});
    return count;
}
void planComponent(const DatasetWordBookIndex& info,const ScopedReaderSource& source,WordBookWorkspace& scratch,
                   size_t count,uint64_t skip,PagePlan& plan,std::array<uint16_t,config::WordBookLookupTokens>* widths=nullptr) {
    const auto* w=info.header.data();const auto roots=get(w+Wordbook::RootCount),pages=get(w+Wordbook::TextPageCount),tokens=get(w+Wordbook::TokenPageCount);
    const uint64_t end=uint64_t(get(w+Wordbook::TmDataOffset))+get(w+Wordbook::TmDataBytes);
    const uint64_t offset=uint64_t(info.section_offset)+Wordbook::Bytes+uint64_t(roots)*TiRoot::Bytes+uint64_t(pages)*TtPage::Bytes;
    source.withRange(size(offset),size(uint64_t(tokens)*TmPage::Bytes),[&](galuchat::ByteReader& reader) {
        uint64_t covered=0,previous_end=0,matches=0;uint32_t previous_width=0;size_t position=0;
        plan.count=0;plan.more=false;
        for(uint32_t i=0;i<tokens;++i) {
            const auto r=readRecord<TmPage::Bytes>(reader);
            const auto first=get(r.data()),entries=get(r.data()+TmPage::TokenCount),width=get(r.data()+TmPage::TokenBytes),start=get(r.data()+TmPage::DataOffset);
            const uint64_t next=uint64_t(start)+uint64_t(entries)*width;
            check(first==covered && entries && entries<=uint64_t(get(w+Wordbook::TokenCount))-covered &&
                  width && width>=previous_width && start>=get(w+Wordbook::TmDataOffset) && start>previous_end && next<=end);
            const size_t begin=position;
            while(position<count && scratch.tokens[scratch.order[position]]<uint64_t(first)+entries) {
                check(scratch.tokens[scratch.order[position]]>=first);
                if(widths)(*widths)[scratch.order[position]]=static_cast<uint16_t>(std::min<uint32_t>(width,config::PlaceResultLineBytes));
                ++position;
            }
            if(begin!=position) {
                const uint32_t low=scratch.tokens[scratch.order[begin]],high=scratch.tokens[scratch.order[position-1]];
                const uint64_t seek=uint64_t(start)+uint64_t(low-first)*width;check(seek<=UINT32_MAX);
                remember(plan,matches,skip,{low,high-low+1,uint32_t(seek),width,uint32_t(begin),uint32_t(position)});
            }
            covered+=entries;previous_width=width;previous_end=next;
        }
        check(covered==get(w+Wordbook::TokenCount) && previous_end==end && position==count && reader.atEnd());
    });
}
// TM is still read in token-number order. A small offset array directs each
// fragment into its final row position; fragment strings are never retained.
[[gnu::noinline]] void emitComponent(const DatasetWordBookIndex& info,const ScopedReaderSource& index,
    const ScopedReaderSource& book,WordBookWorkspace& scratch,size_t count,PlaceLineOutput& row) {
    if(!count || row.full())return;
    std::array<uint16_t,config::WordBookLookupTokens> positions;
    PagePlan plan;planComponent(info,index,scratch,count,0,plan,&positions);
    const size_t capacity=row.available();size_t bytes=0;bool more=false;
    for(size_t i=0;i<count;++i) {
        const auto width=positions[i];
        if(bytes==capacity){positions[i]=UINT16_MAX;more=true;}
        else {positions[i]=static_cast<uint16_t>(bytes);const auto n=std::min<size_t>(width,capacity-bytes);bytes+=n;more|=n<width;}
    }
    uint64_t processed=0;
    do {
        for(size_t i=0;i<plan.count;++i) {
            const auto& p=plan.pages[i];bool needed=false;
            for(size_t j=p.begin;j<p.end;++j)needed|=positions[scratch.order[j]]!=UINT16_MAX;
            if(!needed)continue;
            book.withRange(p.offset,size(uint64_t(p.count)*p.width_or_bytes),[&](galuchat::ByteReader& reader) {
                for(size_t j=p.begin;j<p.end;) {
                    size_t next=j+1;const auto token=scratch.tokens[scratch.order[j]];
                    while(next<p.end && scratch.tokens[scratch.order[next]]==token)++next;
                    size_t target=0,length=0;
                    for(size_t k=j;k<next;++k) {
                        const auto destination=positions[scratch.order[k]];
                        if(destination==UINT16_MAX)continue;
                        const auto n=std::min<size_t>(p.width_or_bytes,capacity-destination);
                        if(n>length){target=destination;length=n;}
                    }
                    if(length) {
                        const size_t offset=size(uint64_t(token-p.first)*p.width_or_bytes);check(reader.pos()<=offset);
                        reader.skipInByte(offset-reader.pos());
                        for(size_t k=0;k<length;++k)row.tail()[target+k]=static_cast<char>(reader.readByte());
                        for(size_t k=j;k<next;++k) {
                            const auto destination=positions[scratch.order[k]];
                            if(destination==UINT16_MAX || destination==target)continue;
                            std::memcpy(row.tail()+destination,row.tail()+target,std::min<size_t>(p.width_or_bytes,capacity-destination));
                        }
                    }
                    j=next;
                }
            });
        }
        processed+=plan.count;
        if(!plan.more)break;
        planComponent(info,index,scratch,count,processed,plan);
    }while(true);
    row.accept(bytes,more);
}
} // namespace

unsigned DiskIndexedWordBookReader::pathDepth()const {return get16(info_.header.data()+Wordbook::Depth);}
bool DiskIndexedWordBookReader::readRegionPath(uint32_t code,unsigned level,alarm::Region& out,BorrowedReadBuffer* work)const {
    out={};if(!code || code>get(info_.header.data()+Wordbook::RecordCount) || !level || level>pathDepth() || level>config::IndexMaxDepth)return false;
    const auto book=book_.borrow(work),index=index_.borrow(work);
    std::array<uint32_t,config::IndexMaxDepth> codes;
    const auto depth=readPath(info_,index,book,codes,code);
    if(level>depth)return false;
    std::copy_n(codes.begin(),level,out.path.begin());out.depth=level;return true;
}

bool DiskIndexedWordBookReader::readPlaceLinesByCode(int64_t code,
    char (&lines)[2][config::PlaceResultLineBytes],const PlaceDisplayFormat& format,
    WordBookWorkspace& scratch,BorrowedReadBuffer* work) const {
    std::memset(lines,0,sizeof(lines));if(code<=0)return false;
    check(uint64_t(code)<=get(info_.header.data()+Wordbook::RecordCount));
    const auto book=book_.borrow(work),index=index_.borrow(work);
    std::array<uint32_t,config::IndexMaxDepth> codes;
    const auto depth=readPath(info_,index,book,codes,static_cast<uint32_t>(code));
    PlaceLineOutput rows[2]={{lines[0],sizeof(lines[0])},{lines[1],sizeof(lines[1])}};
    if(format.specified) {
        for(unsigned line=0;line<2;++line) {
            const auto pattern=format.line(line);check(validPlaceFormat(pattern));
            for(size_t i=0;i<pattern.size() && !rows[line].full();) {
                if(pattern[i]=='%') {
                    const char token=pattern[i+1];i+=2;
                    if(token=='%')rows[line].append("%");
                    else if(size_t(token-'0')<depth) {
                        const auto count=readComponentTokens(info_,index,book,codes[token-'0'],scratch);
                        emitComponent(info_,index,book,scratch,count,rows[line]);
                    }
                } else {
                    const auto end=pattern.find('%',i),count=(end==std::string_view::npos?pattern.size():end)-i;
                    rows[line].append({pattern.data()+i,count});i+=count;
                }
            }
        }
    } else {
        bool separator=false;
        for(size_t i=0;i<depth;++i) {
            const unsigned line=depth==6 && i>=4?1:0;
            if(rows[line].full())continue;
            const auto count=readComponentTokens(info_,index,book,codes[i],scratch);
            if(!i)separator=count!=0;
            if(i && !line && count && separator){rows[line].append(" ");separator=false;}
            emitComponent(info_,index,book,scratch,count,rows[line]);
        }
    }
    return true;
}
} // namespace geoputer

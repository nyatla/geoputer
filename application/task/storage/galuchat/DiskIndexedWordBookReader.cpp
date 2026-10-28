#include "DiskIndexedWordBookReader.h"
#include "../dataset/DatasetIndexFormat.h"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include "../../../Config.h"

namespace geoputer {
namespace {
using namespace index_format;
using Decoder = galuchat::GaluchatGisWordBookReader;
void check(bool value) {
    if (!value) throw std::runtime_error("Wordbook index changed or invalid; revalidate dataset");
}

size_t size(uint64_t value) { check(value <= SIZE_MAX); return static_cast<size_t>(value); }
template<class T> const T& find(const std::vector<std::pair<size_t,T>>& values, size_t key) {
    const auto it = std::lower_bound(values.begin(), values.end(), key,
        [](const auto& entry, size_t target) { return entry.first < target; });
    check(it != values.end() && it->first == key);
    return it->second;
}

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
void collectTextPages(const DatasetWordBookIndex& info,const galuchat::ReaderFactory& factory,
                      const std::vector<size_t>& ids,uint64_t skip,PagePlan& plan) {
    const auto* w=info.header.data();
    const uint32_t roots=get(w+Wordbook::RootCount),pages=get(w+Wordbook::TextPageCount);
    const uint64_t tt_end=uint64_t(get(w+Wordbook::TtDataOffset))+get(w+Wordbook::TtDataBytes);
    const uint64_t offset=uint64_t(info.section_offset)+Wordbook::Bytes+uint64_t(roots)*TiRoot::Bytes;
    auto index=factory.createRange(size(offset),size(uint64_t(pages)*TtPage::Bytes));
    auto current=readRecord<TtPage::Bytes>(*index);
    size_t position=0;uint64_t matches=0;plan.count=0;plan.more=false;
    for(uint32_t i=0;i<pages;++i) {
        const uint32_t first=get(current.data()),start=get(current.data()+TtPage::DataOffset);
        const auto next=i+1<pages?readRecord<TtPage::Bytes>(*index):std::array<uint8_t,TtPage::Bytes>{};
        const uint64_t next_code=i+1<pages?get(next.data()):get(w+Wordbook::ComponentCount);
        const uint64_t next_offset=i+1<pages?get(next.data()+TtPage::DataOffset):tt_end;
        check((i || first==0) && first<next_code && next_code<=get(w+Wordbook::ComponentCount)
              && start>=get(w+Wordbook::TtDataOffset) && start<next_offset && next_offset<=tt_end);
        const size_t begin=position;
        while(position<ids.size() && ids[position]<next_code){check(ids[position]>=first);++position;}
        if(begin!=position) {
            check(position<=UINT32_MAX && next_offset-start<=UINT32_MAX);
            remember(plan,matches,skip,{first,uint32_t(next_code-first),start,uint32_t(next_offset-start),uint32_t(begin),uint32_t(position)});
        }
        current=next;
    }
    check(position==ids.size() && index->atEnd());
}
// Keep this frame local: inlining can reserve both plans in the caller.
[[gnu::noinline]] void readTextPages(const DatasetWordBookIndex& info,const galuchat::ReaderFactory& index,
                   const galuchat::ReaderFactory& book,const std::vector<size_t>& ids,
                   std::vector<size_t>& targets,std::vector<std::pair<size_t,std::vector<size_t>>>& result) {
    // This frame ends before the next phase; it is never retained by a Reader.
    PagePlan plan;
    uint64_t processed=0;
    do {
        collectTextPages(info,index,ids,processed,plan);
        for(size_t i=0;i<plan.count;++i) {
            const auto& p=plan.pages[i];targets.assign(ids.begin()+p.begin,ids.begin()+p.end);
            auto source=book.createRange(p.offset,p.width_or_bytes);
            auto decoded=Decoder::readTokenIdSetsFromPage(*source,p.width_or_bytes,p.first,p.count,
                info.header[Wordbook::TokenBits],targets);
            for(auto& entry:decoded)result.push_back(std::move(entry));
        }
        processed+=plan.count;
    }while(plan.more);
}
void collectTokenPages(const DatasetWordBookIndex& info,const galuchat::ReaderFactory& factory,
                       const std::vector<size_t>& ids,uint64_t skip,PagePlan& plan) {
    const auto* w=info.header.data();
    const uint32_t roots=get(w+Wordbook::RootCount),pages=get(w+Wordbook::TextPageCount),tokens=get(w+Wordbook::TokenPageCount);
    const uint64_t tm_end=uint64_t(get(w+Wordbook::TmDataOffset))+get(w+Wordbook::TmDataBytes);
    const uint64_t offset=uint64_t(info.section_offset)+Wordbook::Bytes+uint64_t(roots)*TiRoot::Bytes+uint64_t(pages)*TtPage::Bytes;
    auto index=factory.createRange(size(offset),size(uint64_t(tokens)*TmPage::Bytes));
    uint32_t covered=0,previous_width=0;uint64_t previous_end=0,matches=0;
    size_t position=0;plan.count=0;plan.more=false;
    for(uint32_t i=0;i<tokens;++i) {
        const auto r=readRecord<TmPage::Bytes>(*index);
        const uint32_t first=get(r.data()),count=get(r.data()+TmPage::TokenCount),width=get(r.data()+TmPage::TokenBytes),start=get(r.data()+TmPage::DataOffset);
        const uint64_t end=uint64_t(start)+uint64_t(count)*width;
        check(first==covered && count && count<=uint64_t(get(w+Wordbook::TokenCount))-covered
              && width && width>=previous_width && start>=get(w+Wordbook::TmDataOffset) && start>previous_end && end<=tm_end);
        const size_t begin=position;
        while(position<ids.size() && ids[position]<uint64_t(first)+count){check(ids[position]>=first);++position;}
        if(begin!=position) {
            const uint64_t first_token=ids[begin],span=ids[position-1]-first_token+1;
            const uint64_t seek=uint64_t(start)+(first_token-first)*width;
            check(position<=UINT32_MAX && span<=UINT32_MAX && seek<=UINT32_MAX);
            remember(plan,matches,skip,{uint32_t(first_token),uint32_t(span),uint32_t(seek),width,uint32_t(begin),uint32_t(position)});
        }
        covered+=count;previous_end=end;previous_width=width;
    }
    check(covered==get(w+Wordbook::TokenCount) && previous_end==tm_end && position==ids.size() && index->atEnd());
}
// Keep this frame local: inlining can reserve both plans in the caller.
[[gnu::noinline]] void readTokenPages(const DatasetWordBookIndex& info,const galuchat::ReaderFactory& index,
                    const galuchat::ReaderFactory& book,const std::vector<size_t>& ids,
                    std::vector<size_t>& targets,std::vector<std::pair<size_t,std::string>>& result) {
    // This frame ends before the next phase; it is never retained by a Reader.
    PagePlan plan;
    uint64_t processed=0;
    do {
        collectTokenPages(info,index,ids,processed,plan);
        for(size_t i=0;i<plan.count;++i) {
            const auto& p=plan.pages[i];targets.assign(ids.begin()+p.begin,ids.begin()+p.end);
            auto source=book.createRange(p.offset,size(uint64_t(p.count)*p.width_or_bytes));
            auto decoded=Decoder::readTokensFromPage(*source,p.first,p.count,p.width_or_bytes,targets);
            for(auto& entry:decoded)result.push_back(std::move(entry));
        }
        processed+=plan.count;
    }while(plan.more);
}
}

bool DiskIndexedWordBookReader::readStringSetByCode(int64_t code, std::vector<std::string>& result) const {
    result.clear();
    if (code <= 0) return false;
    const auto* w = info_.header.data();
    check(uint64_t(code) <= get(w + Wordbook::RecordCount));
    const uint32_t roots = get(w + Wordbook::RootCount), pages = get(w + Wordbook::TextPageCount), token_pages = get(w + Wordbook::TokenPageCount);
    const uint64_t ti_end = uint64_t(get(w + Wordbook::TiDataOffset)) + get(w + Wordbook::TiDataBytes);
    check(roots && pages && token_pages &&
          uint64_t(128)+uint64_t(roots)*16+uint64_t(pages)*8+uint64_t(token_pages)*16 == get(w + Wordbook::SectionBytes));
    auto index = index_->createRange(info_.section_offset, size(uint64_t(Wordbook::Bytes)+uint64_t(roots)*TiRoot::Bytes));
    check(readRecord<Wordbook::Bytes>(*index) == info_.header);

    // TI00: stream all root entries, retaining just the selected root and its end.
    const uint64_t target = uint64_t(code)-1;
    uint64_t covered = 0, previous_bit = 0, selected_bit = 0, selected_end = ti_end*8;
    uint32_t selected_first = 0, selected_count = 0;
    bool await_end = false, final_root = false;
    for (uint32_t i = 0; i < roots; ++i) {
        const auto r = readRecord<TiRoot::Bytes>(*index);
        const uint32_t first = get(r.data()), count = get(r.data() + TiRoot::RecordCount), offset = get(r.data() + TiRoot::DataOffset);
        const uint64_t bit = uint64_t(offset)*8 + r[TiRoot::StartBit];
        check(first == covered && count && count <= uint64_t(get(w + Wordbook::RecordCount))-covered
              && r[TiRoot::StartBit]<8 && !r[13] && !r[14] && !r[15]
              && bit >= uint64_t(get(w + Wordbook::TiDataOffset))*8 && bit < ti_end*8 && (!i || bit>previous_bit));
        if (await_end) { selected_end = bit; await_end = false; }
        if (target >= first && target-first < count) {
            selected_first = first; selected_count = count; selected_bit = bit;
            await_end = true; final_root = i+1 == roots;
        }
        covered += count; previous_bit = bit;
    }
    check(covered == get(w + Wordbook::RecordCount) && selected_count && selected_end>selected_bit);
    // Release both the index FIL and its borrowed buffer before opening TI00.
    check(index->atEnd());
    index.reset();
    std::vector<size_t> codes;
    {
        const uint64_t offset = selected_bit/8;
        auto source = book_->createRange(size(offset), size((selected_end+7)/8-offset));
        codes = Decoder::readCodeSetFromRoot(*source,
            {get16(w + Wordbook::Depth), selected_count, int(w[Wordbook::CodeBits]),
             size(selected_bit%8), size(selected_end-selected_bit), final_root}, size(target-selected_first));
    }
    auto ids = codes;
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    check(!ids.empty() && ids.back() < get(w + Wordbook::ComponentCount));

    std::vector<std::pair<size_t,std::vector<size_t>>> token_sets;
    token_sets.reserve(ids.size());
    std::vector<size_t> targets;
    readTextPages(info_,*index_,*book_,ids,targets,token_sets);
    check(token_sets.size()==ids.size());
    // TT00 selection is finished. Size TM00 work once to avoid growing a
    // reused non-power-of-two capacity past the number of tokens we need.
    size_t token_count=0;
    for (const auto& entry : token_sets) {
        check(entry.second.size()<=ids.max_size()-token_count);
        token_count+=entry.second.size();
    }
    ids.clear();ids.reserve(token_count);
    for (const auto& entry : token_sets) for (auto token : entry.second) {
        check(token<get(w + Wordbook::TokenCount)); ids.push_back(token);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

    std::vector<std::pair<size_t,std::string>> fragments;
    fragments.reserve(ids.size());
    readTokenPages(info_,*index_,*book_,ids,targets,fragments);
    result.reserve(codes.size());
    for (auto component : codes) {
        std::string text;
        for (auto token : find(token_sets,component)) text+=find(fragments,token);
        result.push_back(std::move(text));
    }
    return true;
}
} // namespace geoputer

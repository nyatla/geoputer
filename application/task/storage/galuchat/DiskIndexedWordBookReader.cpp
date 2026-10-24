#include "DiskIndexedWordBookReader.h"
#include "../dataset/DatasetIndexFormat.h"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

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
}

bool DiskIndexedWordBookReader::readStringSetByCode(int64_t code, std::vector<std::string>& result) const {
    result.clear();
    if (code <= 0) return false;
    const auto* w = info_.header.data();
    check(uint64_t(code) <= get(w + Wordbook::RecordCount));
    const uint32_t roots = get(w + Wordbook::RootCount), pages = get(w + Wordbook::TextPageCount), token_pages = get(w + Wordbook::TokenPageCount);
    const uint64_t ti_end = uint64_t(get(w + Wordbook::TiDataOffset)) + get(w + Wordbook::TiDataBytes);
    const uint64_t tt_end = uint64_t(get(w + Wordbook::TtDataOffset)) + get(w + Wordbook::TtDataBytes);
    const uint64_t tm_end = uint64_t(get(w + Wordbook::TmDataOffset)) + get(w + Wordbook::TmDataBytes);
    check(roots && pages && token_pages &&
          uint64_t(128)+uint64_t(roots)*16+uint64_t(pages)*8+uint64_t(token_pages)*16 == get(w + Wordbook::SectionBytes));
    auto index = index_->createRange(info_.section_offset, get(w + Wordbook::SectionBytes));
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
    std::vector<size_t> codes;
    {
        const uint64_t offset = selected_bit/8;
        auto source = book_->createRange(size(offset), size((selected_end+7)/8-offset));
        codes = Decoder::readCodeSetFromRoot(*source,
            {get16(w + Wordbook::Depth), selected_count, int(w[Wordbook::CodeBits]),
             size(selected_bit%8), size(selected_end-selected_bit), final_root}, size(target-selected_first));
    }
    auto sorted = codes;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    check(!sorted.empty() && sorted.back() < get(w + Wordbook::ComponentCount));

    // TT00: a two-entry window supplies each page's record and byte ranges.
    std::vector<std::pair<size_t,std::vector<size_t>>> token_sets;
    size_t position = 0;
    auto current = readRecord<TtPage::Bytes>(*index);
    for (uint32_t i = 0; i < pages; ++i) {
        const uint32_t first = get(current.data()), offset = get(current.data() + TtPage::DataOffset);
        auto next = i+1 < pages ? readRecord<TtPage::Bytes>(*index) : std::array<uint8_t,TtPage::Bytes>{};
        const uint64_t next_code = i+1 < pages ? get(next.data()) : get(w + Wordbook::ComponentCount);
        const uint64_t next_offset = i+1 < pages ? get(next.data() + TtPage::DataOffset) : tt_end;
        check((i || first==0) && first<next_code && next_code<=get(w + Wordbook::ComponentCount)
              && offset>=get(w + Wordbook::TtDataOffset) && offset<next_offset && next_offset<=tt_end);
        const auto begin = position;
        while (position < sorted.size() && sorted[position] < next_code) {
            check(sorted[position]>=first); ++position;
        }
        if (begin != position) {
            std::vector<size_t> targets(sorted.begin()+begin, sorted.begin()+position);
            auto source = book_->createRange(offset, size(next_offset-offset));
            auto decoded = Decoder::readTokenIdSetsFromPage(*source, size(next_offset-offset),
                first, size(next_code-first), w[Wordbook::TokenBits], targets);
            for (auto& entry : decoded) token_sets.push_back(std::move(entry));
        }
        current = next;
    }
    check(position==sorted.size() && token_sets.size()==sorted.size());
    std::vector<size_t> tokens;
    for (const auto& entry : token_sets) for (auto token : entry.second) {
        check(token<get(w + Wordbook::TokenCount)); tokens.push_back(token);
    }
    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());

    // TM00: token pages are fixed width, so start at the first requested fragment.
    std::vector<std::pair<size_t,std::string>> fragments;
    position = 0; covered = 0;
    uint64_t previous_end = 0;
    uint32_t previous_width = 0;
    for (uint32_t i = 0; i < token_pages; ++i) {
        const auto r = readRecord<TmPage::Bytes>(*index);
        const uint32_t first = get(r.data()), count = get(r.data() + TmPage::TokenCount);
        const uint32_t width = get(r.data() + TmPage::TokenBytes), offset = get(r.data() + TmPage::DataOffset);
        const uint64_t end = uint64_t(offset)+uint64_t(count)*width;
        check(first==covered && count && count<=uint64_t(get(w + Wordbook::TokenCount))-covered
              && width && width>=previous_width && offset>=get(w + Wordbook::TmDataOffset) && offset>previous_end && end<=tm_end);
        const auto begin = position;
        while (position<tokens.size() && tokens[position]<uint64_t(first)+count) {
            check(tokens[position]>=first); ++position;
        }
        if (begin!=position) {
            const size_t first_token=tokens[begin], span=tokens[position-1]-first_token+1;
            const uint64_t start=uint64_t(offset)+uint64_t(first_token-first)*width;
            auto source=book_->createRange(size(start),size(uint64_t(span)*width));
            std::vector<size_t> targets(tokens.begin()+begin,tokens.begin()+position);
            auto decoded=Decoder::readTokensFromPage(*source,first_token,span,width,targets);
            for (auto& entry : decoded) fragments.push_back(std::move(entry));
        }
        covered+=count; previous_end=end; previous_width=width;
    }
    check(covered==get(w + Wordbook::TokenCount) && position==tokens.size() && previous_end==tm_end && index->atEnd());
    for (auto component : codes) {
        std::string text;
        for (auto token : find(token_sets,component)) text+=find(fragments,token);
        result.push_back(std::move(text));
    }
    return true;
}
} // namespace geoputer

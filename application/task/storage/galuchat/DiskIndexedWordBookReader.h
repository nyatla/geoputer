#pragma once
#include <memory>
#include <string>
#include <vector>
#include <galuchat/gis_wordbook_reader.hpp>
#include "../dataset/DatasetIndex.h"

namespace geoputer {
// Validated header only. Root/page tables remain on SD and are streamed per miss.
class DiskIndexedWordBookReader {
public:
    DiskIndexedWordBookReader(DatasetWordBookIndex info,
        std::shared_ptr<const galuchat::ReaderFactory> book,
        std::shared_ptr<const galuchat::ReaderFactory> index)
        : info_(info), book_(std::move(book)), index_(std::move(index)) {}
    bool readStringSetByCode(int64_t code, std::vector<std::string>& result) const;
private:
    DatasetWordBookIndex info_;
    std::shared_ptr<const galuchat::ReaderFactory> book_, index_;
};
} // namespace geoputer

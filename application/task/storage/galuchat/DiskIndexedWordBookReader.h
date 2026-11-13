#pragma once
#include <galuchat/gis_wordbook_reader.hpp>
#include "../dataset/DatasetIndex.h"
#include "ScopedReaderSource.h"
#include "WordBookWorkspace.h"
#include "TextEncoding.h"
#include "../../../PlaceText.h"

namespace geoputer {
// Owns validated header only. Input descriptions outlive this consumer.
// Index and body Readers end in turn, sharing one per-call work buffer.
class DiskIndexedWordBookReader {
public:
    DiskIndexedWordBookReader(DatasetWordBookIndex info,ScopedReaderSource book,ScopedReaderSource index)
        :info_(info),book_(book),index_(index){}
    bool readPlaceLinesByCode(int64_t code,char (&lines)[2][config::PlaceResultLineBytes],
                             const PlaceDisplayFormat& format,WordBookWorkspace& scratch,
                             BorrowedReadBuffer* work,PlaceTextMode mode=PlaceTextMode::Japanese16) const;
    unsigned pathDepth()const;
private:
    DatasetWordBookIndex info_;
    ScopedReaderSource book_,index_;
};
} // namespace geoputer

#pragma once
#include "ObservedFileReader.h"
#include <utility>
namespace geoputer {
// Non-owning SD source. Every read requires caller-owned work memory.
class ScopedReaderSource {
public:
    ScopedReaderSource(const FileReadSource& file):file_(file) {}
    ScopedReaderSource(FileReadSource&&)=delete;
    ScopedReaderSource borrow(BorrowedReadBuffer* work)const {
        if(!work)throw std::invalid_argument("Reader work memory is required");
        auto source=*this;source.work_=work;return source;
    }
    template<class Consumer>
    void withRange(size_t offset,size_t length,Consumer&& consume) const {
        file_.withRange(offset,length,std::forward<Consumer>(consume),work_);
    }
private:
    const FileReadSource& file_;
    BorrowedReadBuffer* work_=nullptr;
};
} // namespace geoputer

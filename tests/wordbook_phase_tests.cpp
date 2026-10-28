#include "task/storage/galuchat/DiskIndexedWordBookReader.h"
#include "task/storage/galuchat/ObservedFileReader.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
using namespace geoputer;
void check(bool value, const char* why) {
    if (!value) throw std::runtime_error(why);
}
struct Lifetime {
    unsigned live = 0, peak = 0;
};
class CountedReader final : public galuchat::ByteReader {
public:
    CountedReader(std::unique_ptr<galuchat::ByteReader> reader, Lifetime& life, bool fail)
        : reader_(std::move(reader)), life_(life), fail_(fail) {
        ++life_.live;
        life_.peak = std::max(life_.peak, life_.live);
    }
    ~CountedReader() override { reader_.reset(); --life_.live; }
    size_t pos() const override { return reader_->pos(); }
    bool atEnd() const override { return reader_->atEnd(); }
protected:
    uint8_t nextByte() override {
        if (fail_) throw std::runtime_error("injected body read failure");
        return reader_->readByte();
    }
    void skipByte(size_t count) override { reader_->skipInByte(count); }
private:
    std::unique_ptr<galuchat::ByteReader> reader_;
    Lifetime& life_;
    bool fail_;
};
class CountedFactory final : public galuchat::ReaderFactory {
public:
    CountedFactory(const std::string& path, FileIoTiming& timing, BorrowedReadBuffer& pool, Lifetime& life)
        : source_(path, nullptr, &timing, {4096, 4096}, 0, &pool), life_(life) {}
    size_t fail_offset = SIZE_MAX;
    std::unique_ptr<galuchat::ByteReader> create(size_t offset = 0) const override {
        check(!life_.live, "index/body Readers overlapped");
        return std::make_unique<CountedReader>(source_.create(offset), life_, offset == fail_offset);
    }
    std::unique_ptr<galuchat::ByteReader> createRange(size_t offset, size_t count) const override {
        check(!life_.live, "index/body Readers overlapped");
        return std::make_unique<CountedReader>(source_.createRange(offset, count), life_, offset == fail_offset);
    }
private:
    ObservedFileReaderFactory source_;
    Lifetime& life_;
};
}
int main(int argc, char** argv) {
    try {
        if (argc != 3) return 2;
        using namespace geoputer;
        namespace fmt = index_format;
        const auto directory = std::filesystem::path(argv[1]);
        const unsigned pages = std::stoul(argv[2]);
        const auto book_path = (directory / "book.giswordbook").string();
        const auto index_path = (directory / "index.dat").string();
        DatasetWordBookIndex info;
        std::ifstream(index_path, std::ios::binary).read(reinterpret_cast<char*>(info.header.data()), info.header.size());
        std::vector<std::string> expected;
        std::ifstream lines(directory / "expected.txt");
        for (std::string line; std::getline(lines, line);) expected.push_back(line);
        check(expected.size() == 32, "fixture expected maximum-depth name");
        const auto path = [](const std::string& native) {
#ifdef GEOPUTER_TEST_FATFS
            return "/sd" + native;
#else
            return native;
#endif
        };
        auto legacy_factory = std::make_shared<ObservedFileReaderFactory>(path(book_path), nullptr);
        galuchat::GaluchatGisWordBookReader legacy(legacy_factory, 0);
        check(legacy.readStringSet(0) == expected, "fixture differs from the sequential SDK Reader");
#ifdef GEOPUTER_TEST_FATFS
        check(!fatfs_test::live, "legacy Reader escaped query");
#endif
        std::array<uint8_t, 4096 + 32> guarded;
        guarded.fill(0xa5);
        BorrowedReadBuffer pool(guarded.data() + 16, 4096);
        FileIoTiming book_io, index_io;
        Lifetime life;
        auto book = std::make_shared<CountedFactory>(path(book_path), book_io, pool, life);
        auto index = std::make_shared<CountedFactory>(path(index_path), index_io, pool, life);
        DiskIndexedWordBookReader reader(info, book, index);
        std::vector<std::string> actual;
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            book_io = {}; index_io = {};
            check(reader.readStringSetByCode(1, actual) && actual == expected, "phased query name mismatch");
            check(!pool.inUse() && !life.live && life.peak == 1, "query retained a Reader/lease");
            check(index_io.opens == 2 + (pages + 31) / 32 && index_io.opens == index_io.closes,
                  "index must be closed before body reads, with bounded TM rescans");
            check(book_io.opens == 1 + 32 + pages && book_io.opens == book_io.closes,
                  "body page was omitted or decoded twice");
#ifdef GEOPUTER_TEST_FATFS
            check(!fatfs_test::live, "query leaked FIL");
#endif
        }
        // Fail while a body Reader owns the loan, once in each phase and once
        // in the final TM batch. The same Reader object must remain reusable.
        std::ifstream raw(index_path, std::ios::binary);
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(raw), {}};
        const auto ti = fmt::get(bytes.data() + 128 + fmt::TiRoot::DataOffset);
        const auto tt = fmt::get(bytes.data() + 128 + 16 + fmt::TtPage::DataOffset);
        const auto tm_start = 128 + 16 + 32 * 8;
        const auto tm = fmt::get(bytes.data() + tm_start + fmt::TmPage::DataOffset);
        const auto last_tm = fmt::get(bytes.data() + tm_start + (pages - 1) * 16 + fmt::TmPage::DataOffset);
        for (const auto offset : {ti, tt, tm, last_tm}) {
            book->fail_offset = offset;
            bool failed = false;
            try { reader.readStringSetByCode(1, actual); }
            catch (const std::runtime_error&) { failed = true; }
            check(failed && !pool.inUse() && !life.live, "body failure retained a Reader/lease");
#ifdef GEOPUTER_TEST_FATFS
            check(!fatfs_test::live, "body failure leaked FIL");
#endif
            book->fail_offset = SIZE_MAX;
            check(reader.readStringSetByCode(1, actual) && actual == expected && !pool.inUse(),
                  "query could not recover after failed body read");
        }
        check(!reader.readStringSetByCode(0, actual) && actual.empty(), "zero code behavior changed");
        for (unsigned i = 0; i < 16; ++i)
            check(guarded[i] == 0xa5 && guarded[4112 + i] == 0xa5, "shared buffer overran guards");
        std::cout << "PASS: depth=32 TT=32 TM=" << pages
                  << ", batch bounds, sequential name equality, one shared 4KiB, failures/recovery\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

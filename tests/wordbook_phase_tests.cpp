#include "support/PlaceFormatOracle.h"
#include <galuchat/std_file_reader.hpp>
#include "PlaceText.h"
#include "task/storage/galuchat/DiskIndexedWordBookReader.h"
#include "task/storage/galuchat/ObservedFileReader.h"
#include "task/storage/galuchat/TextEncoding.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
using namespace geoputer;
void check(bool value, const char* why) {
    if (!value) throw std::runtime_error(why);
}

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
        auto oracle=std::make_shared<galuchat::FileReaderFactory>(book_path);
        galuchat::GaluchatGisWordBookReader sdk(oracle,0);
        check(sdk.readStringSet(0)==expected,"independent SDK oracle differs");
        std::array<uint8_t,4096+32> guarded;guarded.fill(0xa5);
        BorrowedReadBuffer pool(guarded.data()+16,4096);
        FileIoTiming book_io,index_io;
        const auto native_book=path(book_path),native_index=path(index_path);
        ReadFileSlot slot;
        FileReadSource file_book(native_book,nullptr,&book_io,{4096,4096},0,&slot);
        FileReadSource file_index(native_index,nullptr,&index_io,{4096,0},0,&slot);
        DiskIndexedWordBookReader direct(info,file_book,file_index);
        // Caller-owned fixed output agrees with the independent SDK oracle.
        WordBookWorkspace scratch;char fixed[2][config::PlaceResultLineBytes];char expected_rows[2][64];
        PlaceDisplayFormat format{{"%2 %0 %% %2","%9%1"},true};
        for(const auto& setting:{PlaceDisplayFormat{},format}) {
            formatPlaceLines(expected,expected_rows,setting);
            bool exhausted=false;
            try {
                check(direct.readPlaceLinesByCode(1,fixed,setting,scratch,&pool),"bounded query failed");
            }catch(const std::length_error&){exhausted=true;}
            check(exhausted==(pages-31>128),"per-component capacity error mismatch");
            if(!exhausted) {char display[2][64];for(unsigned row=0;row<2;++row){PlaceLineBuilder b;b.append(fixed[row]);b.finish(display[row]);}check(!std::strcmp(display[0],expected_rows[0]) && !std::strcmp(display[1],expected_rows[1]),"bounded rows differ from oracle formatting");}
            check(!pool.inUse() && !slot.inUse(),"bounded query retained Reader resources");
        }
        // Page-local capacity is per component. The last component has pages-31 tokens.
        const auto tt_start=fmt::get(info.header.data()+fmt::Wordbook::TtDataOffset);
        const auto tt_end=uint64_t(tt_start)+fmt::get(info.header.data()+fmt::Wordbook::TtDataBytes);
        const auto tt_index=128+16+31*8;
        std::ifstream table(index_path,std::ios::binary);std::array<uint8_t,8> last{};
        table.seekg(tt_index);table.read(reinterpret_cast<char*>(last.data()),last.size());
        const auto last_start=fmt::get(last.data()+4);std::array<uint16_t,130> bounded;bounded.fill(0xa5a5);
        bool capacity_failed=false;size_t decoded=0;
        try {
            file_book.withRange(last_start,tt_end-last_start,[&](galuchat::ByteReader& source) {
                decoded=galuchat::GaluchatGisWordBookReader::readTokenIdsFromPageInto(source,tt_end-last_start,31,1,info.header[fmt::Wordbook::TokenBits],31,bounded.data()+1,128);
            },&pool);
        }catch(const std::length_error&){capacity_failed=true;}
        check(capacity_failed==(pages-31>128),"128-token capacity boundary mismatch");
        if(!capacity_failed) {
            check(decoded==pages-31,"bounded TT count differs");
            for(size_t i=0;i<decoded;++i)check(bounded[i+1]==i+31,"bounded TT token order differs");
        }
        check(bounded.front()==0xa5a5 && bounded.back()==0xa5a5 && !pool.inUse() && !slot.inUse(),"capacity failure overran output or retained a loan");

#ifdef GEOPUTER_TEST_FATFS
        for(bool* flag:{&fatfs_test::fail_open,&fatfs_test::fail_seek,&fatfs_test::fail_read}) {
            *flag=true;bool failed=false;
            try{direct.readPlaceLinesByCode(1,fixed,{},scratch,&pool);}catch(const std::runtime_error&){failed=true;}
            *flag=false;
            check(failed && !pool.inUse() && !slot.inUse() && !fatfs_test::live,"failure leaked resource");
        }
#endif
        check(!direct.readPlaceLinesByCode(0,fixed,{},scratch,&pool) && !fixed[0][0] && !fixed[1][0],"zero code behavior changed");
        for (unsigned i = 0; i < 16; ++i)
            check(guarded[i] == 0xa5 && guarded[4112 + i] == 0xa5, "shared buffer overran guards");
        std::cout << "PASS: depth=32 TT=32 TM=" << pages
                  << ", batch bounds, sequential name equality, one shared 4KiB, failures/recovery\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

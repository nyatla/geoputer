#include "support/StorageFixture.h"
#include <filesystem>
#include "support/MapIndexFixture.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <new>
#include "task/storage/galuchat/PlaceLookup.h"
#include "task/storage/DatasetSelection.h"

namespace candidate_heap {bool track=false;size_t calls=0,bytes=0;}
void* operator new(std::size_t bytes) {
    if(candidate_heap::track){++candidate_heap::calls;candidate_heap::bytes+=bytes;}
    if(auto* p=std::malloc(bytes?bytes:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) {return ::operator new(bytes);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}

using namespace geoputer;
namespace fs = std::filesystem;
static void check(bool value, const char* detail) { if (!value) throw std::runtime_error(detail); }
struct Activity : StorageObserver {
    unsigned accesses = 0, depth = 0, transitions = 0;
    RgCacheState cache = RgCacheState::Unprepared;
    void sdBegin() override { if (!depth++) ++accesses; }
    void sdEnd() override { check(depth > 0,"balanced observer scope"); --depth; }
    void cacheState(RgCacheState state, bool) override { cache=state; ++transitions; }
};
void testMissingCacheRelease(const fs::path& root,const fs::path& map,const fs::path& book) {
    const auto dataset_root=(root/"missing-index").string();const auto directory=fs::path(dataset_root)/"a01";fs::create_directories(directory);
    fs::copy_file(map,directory/"map.glc");fs::copy_file(book,directory/"names.giswordbook");
    std::ofstream(directory/"dataset.ini")<<"[dataset]\nversion=1\nmap=map.glc\ndefault_language=ja\n[ja]\ntitle=Test\nwordbook=names.giswordbook\n";
    SettingsStore store;check(store.begin(),"real-file test NVS opens");
    StorageSnapshot state{};state.settings=defaultSettings();state.settings.dataset=1;std::strcpy(state.settings.wordbook_language,"ja");
    DatasetId id{};id.assign("a01");check(store.save(state.settings,id),"selection persisted before cache removal");
    Activity activity;CodeCacheBuffer lookup_pixels{};SelectedDataset lookup_dataset;test_storage::Lookup lookup(lookup_pixels,lookup_dataset,"",dataset_root);lookup.setObserver(&activity);lookup.configure("a01","ja");
    const auto failed=lookup.prepare();check(failed.state==PlaceState::DataError,"missing saved index fails startup preparation");
    check(releaseUnusableDataset(store,state,failed.state),"failed startup clears selected dataset");
    lookup.configure("");const auto accesses=activity.accesses;
    for(unsigned n=0;n<3;++n)check(lookup.prepare().state==PlaceState::Unselected && lookup.search({}).state==PlaceState::Unselected,
        "released dataset no longer tries to reopen missing index");
    check(activity.accesses==accesses && lookup.cacheStats().bytes==0,"automatic release performs no later dataset I/O and retains no raster");
    Settings restored{};SettingsStore reboot;check(reboot.begin() && reboot.load(restored)==SettingsState::Loaded && !restored.dataset,
        "a reboot after missing cache restores None");
    CodeCacheBuffer next_boot_pixels{};SelectedDataset next_boot_dataset;test_storage::Lookup next_boot(next_boot_pixels,next_boot_dataset,"",dataset_root);next_boot.setObserver(&activity);next_boot.configure(restored.dataset?"a01":"");
    check(next_boot.prepare().state==PlaceState::Unselected && activity.accesses==accesses,"next boot skips broken dataset completely");
    test_index::generate(directory.string(),"map.glc","names.giswordbook");
    check(reboot.load(restored)==SettingsState::Loaded && !restored.dataset,"cache regeneration does not select the repaired dataset");
    lookup.configure("a01","ja");check(lookup.prepare().state==PlaceState::Ready,"explicit selection can use the repaired index");
}
void testLiveDatasetSwitch(const fs::path& root,const fs::path& map,const fs::path& book) {
    const auto dataset_root=(root/"live").string();const auto first=fs::path(dataset_root)/"a01",second=fs::path(dataset_root)/"a02";
    for(const auto& directory:{first,second}) {
        fs::create_directories(directory);
        const std::string map_name=directory==first?"first.glc":"second.glc",book_name=directory==first?"first.giswordbook":"second.giswordbook";
        fs::copy_file(map,directory/map_name);fs::copy_file(book,directory/book_name);
        std::ofstream(directory/"dataset.ini")<<"[dataset]\nversion=1\nmap="<<map_name<<"\ndefault_language=ja\n[ja]\ntitle=Test\nwordbook="<<book_name<<"\ndisplay_line1="
            <<(directory==first?"FIRST %0":"SECOND %1%2%3%4")<<"\ndisplay_line2=\n";
        test_index::generate(directory.string(),map_name,book_name);
    }
    Activity activity;CodeCacheBuffer lookup_pixels{};SelectedDataset lookup_dataset;test_storage::Lookup lookup(lookup_pixels,lookup_dataset,"",dataset_root);lookup.setObserver(&activity);
    PlaceRequest request{7,31,{35.681641,140.027194,1000,true}};
    lookup.configure("a01","ja");
    check(lookup.prepare().state==PlaceState::Ready && !std::strcmp(lookup.search(request).lines[0],"FIRST 千葉県"),
          "first selected dataset fills the raster and formatted name caches");
    const auto first_generation=lookup.datasetInfo().generation;
    lookup.configure("a02","ja");
    check(lookup.datasetInfo().state==PlaceState::Waiting && lookup.datasetInfo().generation>first_generation &&
          !lookup.cacheStats().bytes && !lookup.wordBookCacheStats().entries && activity.cache==RgCacheState::Unprepared,
          "live selection releases both caches and advances the dataset generation before search");
    check(lookup.prepare().state==PlaceState::Ready && !std::strcmp(lookup.search(request).lines[0],"SECOND 習志野市"),
          "same running lookup resolves the new dataset without stale formatted text");
    const auto second_generation=lookup.datasetInfo().generation;
    lookup.configure("a02","ja");
    check(lookup.datasetInfo().generation>second_generation && !lookup.cacheStats().bytes && !lookup.wordBookCacheStats().entries &&
          lookup.prepare().state==PlaceState::Ready && !std::strcmp(lookup.search(request).lines[0],"SECOND 習志野市"),
          "same-selection reapplication also reopens readers and repopulates caches");
    lookup.configure("");check(lookup.prepare().state==PlaceState::Unselected && !lookup.cacheStats().bytes,
          "live clear closes readers without restarting the lookup object");
}
void testMapCache();
void testDatasetFiles(const std::string& root,const std::string& map,const std::string& book);
void testMapCacheFile(const std::string& path, const std::string& book, const std::string& temporary);
std::vector<uint8_t> oversizedMapFixture(unsigned count);

int main(int argc, char** argv) {
    try {
        if (argc != 4 && argc != 5) throw std::runtime_error(
            "usage: test <temporary directory> <mapset> <UTF-8 wordbook> [unsupported wordbook]");
        testMissingCacheRelease(argv[1],argv[2],argv[3]);
        testLiveDatasetSwitch(argv[1],argv[2],argv[3]);
        testDatasetFiles(argv[1],argv[2],argv[3]);
        testMapCache();
        testMapCacheFile(argv[2],argv[3],argv[1]);
        const fs::path root(argv[1]);
        const auto data = root / "dataset/a01";const auto dataset_root=(root/"dataset").string();
        PlaceRequest request{7, 31, {35.681641, 140.027194, 1000, true}};
        Activity activity;
        {
            CodeCacheBuffer disabled_pixels{};SelectedDataset disabled_dataset;test_storage::Lookup disabled(disabled_pixels,disabled_dataset,"",dataset_root);disabled.setObserver(&activity);disabled.configure("");
            const auto accesses=activity.accesses;
            const auto generation=disabled.datasetInfo().generation;
            for(unsigned i=0;i<3;++i) {
                check(disabled.prepare().state==PlaceState::Unselected && disabled.search(request).state==PlaceState::Unselected &&
                      disabled.search({}).state==PlaceState::Unselected,"unselected bypasses data and GPS validation");
                disabled.close();
            }
            check(activity.accesses==accesses && disabled.cacheStats().bytes==0 && disabled.selectedLanguage().empty() &&
                  disabled.datasetInfo().generation>generation,"unselected performs no file I/O or raster allocation, including remount/recheck");
            disabled.configure("a01");
            check(disabled.prepare().state==PlaceState::DatasetMissing && activity.accesses>accesses,"explicit selection re-enables dataset validation");
            disabled.configure("");
            check(disabled.prepare().state==PlaceState::Unselected,"unselection clears even a latched dataset fault");
        }
        CodeCacheBuffer lookup_pixels{};SelectedDataset lookup_dataset;test_storage::Lookup lookup(lookup_pixels,lookup_dataset,"a01",dataset_root);
        lookup.setObserver(&activity);
        check(lookup.prepare().state == PlaceState::DatasetMissing, "startup detects missing dataset without GPS");
        check(lookup.search(request).state == PlaceState::DatasetMissing, "missing dataset");
        const auto missing_accesses=activity.accesses;
        check(lookup.prepare().state==PlaceState::DatasetMissing && lookup.search(request).dataset.state==PlaceState::DatasetMissing && activity.accesses==missing_accesses,
              "missing dataset is latched without repeated directory access");
        lookup.close();
        fs::create_directories(data);
        std::ofstream(data / "dataset.ini") << "[dataset]\nversion=1\nmap=area.glc\ndefault_language=ja\n[ja]\ntitle=Test\nwordbook=names.giswordbook\n";
        // Invalid dictionary content proves that oversized maps are rejected
        // before opening the dictionary or allocating a raster.
        { std::ofstream book(data / "names.giswordbook"); book << "invalid"; }
        for (const unsigned count : {1501u, 2000u}) {
            const auto bytes = oversizedMapFixture(count);
            { std::ofstream map(data / "area.glc", std::ios::binary);
              map.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); }
            const auto rejected = lookup.prepare();
            check(rejected.state == PlaceState::TooManyChunks && rejected.dataset.chunks == count &&
                  rejected.dataset.limit == config::MaxDatasetChunks,
                  "startup reports actual chunk count and limit before opening the invalid dictionary");
            check(lookup.cacheStats().bytes == 0 && lookup.readTiming().wordbook_io.opens == 0,
                  "an oversized map allocates no raster and never opens the wordbook");
            const auto rejected_accesses = activity.accesses;
            check(lookup.search(request).state == PlaceState::TooManyChunks &&
                  lookup.prepare().state == PlaceState::TooManyChunks && activity.accesses == rejected_accesses,
                  "GPS updates and repeated checks do not re-read a rejected dataset");
            lookup.close();
        }
        fs::remove(data / "area.glc");
        fs::remove(data / "names.giswordbook");
        fs::copy_file(argv[2], data / "area.glc");
        fs::copy_file(argv[3], data / "names.giswordbook");
        test_index::generate(data.string(),"area.glc","names.giswordbook");
        check(lookup.prepare().state == PlaceState::Ready, "startup opens real file headers without a GPS fix");
        check(lookup.cacheStats().bytes == 0 && !lookup.cacheStats().valid, "Boot binds the fixed array without reading a logical cache image");
        check(activity.cache == RgCacheState::Unprepared,"dataset headers alone do not prepare the image");
        check(lookup.search(request).state == PlaceState::Ready && activity.cache == RgCacheState::Ready,
              "completed rectangle rebuild reports persistent ready state");
        const auto cold_time = lookup.readTiming();
        for (const auto& io : {cold_time.map_io, cold_time.wordbook_io})
            check(io.elapsed_us == io.read_us + io.seek_us + io.open_us + io.close_us + io.stat_us,
                  "file read, seek, open, close and stat times partition the I/O total exactly");
        check(cold_time.rect_us <= cold_time.map_us && cold_time.map_us <= cold_time.total_us &&
              cold_time.wordbook_us <= cold_time.total_us && cold_time.map_io.reads && cold_time.map_io.bytes &&
              cold_time.wordbook_io.reads && cold_time.wordbook_io.bytes,
              "cold timing separates rectangle, map I/O and dictionary I/O");
        check(lookup.wordBookCacheStats().entries == 1 && lookup.wordBookCacheStats().misses == 1,
              "first successful name lookup fills the wordbook cache");
        const auto transitions = activity.transitions;
        const auto accesses = activity.accesses;
        check(lookup.search(request).state == PlaceState::Ready && activity.transitions == transitions,
              "cache hit leaves RG ready without pulse or transition");
        check(activity.accesses == accesses && activity.depth == 0 && lookup.wordBookCacheStats().hits == 1,
              "map and wordbook cache hits require no actual SD I/O");
        const auto hit_time = lookup.readTiming();
        check(hit_time.rect_us == 0 && hit_time.map_io.bytes == 0 && hit_time.map_io.reads == 0 &&
              hit_time.map_io.opens == 0 && hit_time.map_io.seeks == 0 && hit_time.map_io.closes == 0 &&
              hit_time.map_io.elapsed_us == 0 && hit_time.wordbook_io.reads == 0 && hit_time.wordbook_io.elapsed_us == 0 &&
              hit_time.map_io.read_us == 0 && hit_time.map_io.seek_us == 0 &&
              hit_time.wordbook_io.read_us == 0 && hit_time.wordbook_io.seek_us == 0 &&
              hit_time.text_us == 0 && hit_time.map_us <= hit_time.total_us,
              "a hit resets this-request timing instead of reporting the previous rectangle or file read");
        auto moved = request;
        moved.fix.longitude += 0.000001;
        moved.fix.utc_ms += 1000;
        moved.screen_generation += 1;
        moved.sample_seq += 1;
        const auto fresh = lookup.search(moved);
        check(fresh.state == PlaceState::Ready && fresh.request.fix.longitude == moved.fix.longitude &&
              fresh.request.fix.utc_ms == moved.fix.utc_ms && fresh.request.screen_generation == moved.screen_generation &&
              fresh.request.sample_seq == moved.sample_seq && activity.accesses == accesses,
              "cached names return the current coordinates, time, generation and sample, with no SD read");
        for (unsigned i = 0; i < 4; ++i) {
            const auto result = lookup.search(request);
            if (result.state != PlaceState::Ready) throw std::runtime_error(result.detail);
            check(std::strcmp(result.lines[0], "千葉県 習志野市") == 0 && !result.lines[1][0],
                  "real UTF-8 dataset resolves Narashino City Hall");
            check(result.request.fix.longitude == request.fix.longitude && result.request.sample_seq == 31,
                  "result retains the coordinate snapshot");
            auto tokyo = request;
            tokyo.fix = {35.6852, 139.7528, 1000, true};
            const auto second = lookup.search(tokyo);
            check(second.state == PlaceState::Ready && std::strcmp(second.lines[0], "東京都 千代田区") == 0 && !second.lines[1][0], "repeated seeks resolve another region");
        }
        check(lookup.wordBookCacheStats().entries == 2 && lookup.wordBookCacheStats().misses == 2,
              "rectangle replacement preserves both region-name entries");
        auto outside = request;
        outside.fix = {-30, 0, 0, true};
        check(lookup.search(outside).state == PlaceState::NotFound, "no region differs from read failure");
        auto invalid = request;
        invalid.fix.valid = false;
        check(lookup.search(invalid).state == PlaceState::NoFix, "invalid GPS cannot trigger lookup");
        const auto hits_before = lookup.wordBookCacheStats().hits;
        check(lookup.search(request).state == PlaceState::Ready && lookup.wordBookCacheStats().hits == hits_before + 1,
              "no-fix and region-zero results do not discard name-cache entries");
        lookup.close();
        check(activity.cache == RgCacheState::Unprepared && activity.depth == 0,"closing dataset invalidates RG and balances I/O");
        check(lookup.cacheStats().bytes == 0 && !lookup.cacheStats().valid, "closing the dataset releases and invalidates its cache");
        check(!lookup.wordBookCacheStats().entries && !lookup.wordBookCacheStats().hits && !lookup.wordBookCacheStats().misses,
              "closing the dataset clears the wordbook cache and counters");
        fs::copy_file(argv[2], data / "duplicate.glc");
        check(lookup.prepare().state == PlaceState::Ready && lookup.search(request).state==PlaceState::Ready,
              "INI selects the exact file even when other GLC files are present");
        fs::remove(data / "duplicate.glc");
        lookup.close();
        { std::ofstream bad(data / "area.glc", std::ios::binary | std::ios::trunc); bad << "bad"; }
        check(lookup.prepare().state == PlaceState::DataError, "startup reports corrupt headers");
        check(lookup.search(request).state == PlaceState::DataError, "corrupt file is recoverable error");
        fs::copy_file(argv[2], data / "area.glc", fs::copy_options::overwrite_existing);
        const auto failed_accesses=activity.accesses;
        check(lookup.search(request).state==PlaceState::DataError && lookup.prepare().state==PlaceState::DataError && activity.accesses==failed_accesses,
              "repairing file bytes does not silently retry or clear the rejection");
        test_index::generate(data.string(),"area.glc","names.giswordbook");
        lookup.close();
        check(lookup.search(request).state == PlaceState::Ready, "explicit reapply and reopen validate corrected dataset");
        fs::remove(data / "names.giswordbook");
        const auto cached_accesses = activity.accesses;
        check(lookup.search(request).state == PlaceState::Ready && activity.accesses == cached_accesses,
              "an already cached result does not probe dictionary media on a hit");
        auto osaka = request;
        osaka.fix = {34.6937, 135.5023, 1000, true};
        check(lookup.search(osaka).state == PlaceState::DataError, "uncached dictionary read detects file disappearance");
        check(!lookup.wordBookCacheStats().entries, "read failure closes the dataset and clears old cached names");
        check(lookup.search(request).state == PlaceState::DataError, "failed reader is released but original rejection is retained");
        fs::copy_file(argv[3], data / "names.giswordbook");
        {
            std::ifstream input(data / "names.giswordbook", std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            input.close();
            const auto token = bytes.find("千");
            check(token != std::string::npos, "test source contains the UTF-8 prefecture token");
            bytes[token] = static_cast<char>(0xFF); // Same size, invalid UTF-8 in the token table.
            std::ofstream output(data / "names.giswordbook", std::ios::binary | std::ios::trunc);
            output.write(bytes.data(), bytes.size());
        }
        lookup.close();
        test_index::generate(data.string(),"area.glc","names.giswordbook");
        check(lookup.search(request).state == PlaceState::EncodingError && !lookup.wordBookCacheStats().entries,
              "invalid text is never inserted into the wordbook cache");
        const auto encoding_accesses=activity.accesses;
        check(lookup.search(request).state == PlaceState::EncodingError && lookup.prepare().state==PlaceState::EncodingError && activity.accesses==encoding_accesses,
              "invalid UTF-8 rejection performs no further dictionary reads");
        lookup.close();
        fs::copy_file(argv[3], data / "names.giswordbook", fs::copy_options::overwrite_existing);
        lookup.close();
        test_index::generate(data.string(),"area.glc","names.giswordbook");
        check(lookup.search(request).state == PlaceState::Ready && lookup.wordBookCacheStats().misses == 1,
              "reopening a repaired dataset resolves names again without a stale cache entry");
        lookup.close();
        for (const std::string metadata : {
                "{\"text_encoding\":\"utf8\",\"token_encoding\":\"UTF-8\"}",
                "{\"text_encoding\":\"utf8\",\"token_encoding\":\"utf8\"}",
                "{\"text_encoding\":\"UTF-8\"}",
                "{\"text_encoding\":\"utf8\",\"token_encoding\":\"\"}",
                "{\"text_encoding\":\"utf8\",\"token_encoding\":\"cp932\"}",
                "{\"token_encoding\":\"sjis\"}", "{\"token_encoding\":\"shift_jis\"}",
                "{\"token_encoding\":\"shift-jis\"}", "{\"token_encoding\":\"utf16\"}", "{}", "", "arbitrary metadata, not JSON"}) {
            lookup.close();
            fs::copy_file(argv[3], data / "names.giswordbook", fs::copy_options::overwrite_existing);
            std::ifstream input(data / "names.giswordbook", std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            input.close();
            const auto start = bytes.find('{'), end = bytes.find('}', start);
            check(start != std::string::npos && end != std::string::npos && metadata.size() <= end-start+1,
                  "test metadata fits the original header framing");
            bytes.replace(start, end-start+1, metadata + std::string(end-start+1-metadata.size(), ' '));
            std::ofstream output(data / "names.giswordbook", std::ios::binary | std::ios::trunc);
            output.write(bytes.data(), bytes.size());
            output.close();
            test_index::generate(data.string(),"area.glc","names.giswordbook");
            check(lookup.prepare().state == PlaceState::Ready && lookup.search(request).state == PlaceState::Ready,
                  "optional encoding declarations, empty and non-JSON metadata do not control UTF-8 text reads");
            check(activity.depth==0,"opaque metadata preserves balanced SD access scopes");
        }
        if (argc == 5) {
            lookup.close();
            fs::copy_file(argv[4], data / "names.giswordbook", fs::copy_options::overwrite_existing);
            test_index::generate(data.string(),"area.glc","names.giswordbook");
            check(lookup.prepare().state == PlaceState::Ready &&
                  lookup.search(request).state == PlaceState::EncodingError &&
                  !lookup.wordBookCacheStats().entries && activity.depth == 0,
                  "real legacy Wordbook is structurally accepted but invalid UTF-8 text is rejected on read");
            std::cout << "PASS: invalid UTF-8 in legacy Wordbook is rejected on text read without conversion\n";
        }
        std::cout << "PASS: real file-based SDK lookup, UTF-8 names/encoding rejection, repeated seeks, missing/explicit-selection/corrupt data and recovery\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}

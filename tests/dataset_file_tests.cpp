#include "support/StorageFixture.h"
#include <filesystem>
#include "support/MapIndexFixture.h"
#include <fstream>
#include <stdexcept>
#include <cstring>
#include "task/storage/dataset/DatasetCatalog.h"
#include "task/storage/StorageIndexLease.h"
#include "task/storage/StorageCodeImages.h"
#include "task/storage/galuchat/PlaceLookup.h"
using namespace geoputer;
namespace fs=std::filesystem;
namespace candidate_heap {extern bool track;extern size_t calls,bytes;}
std::vector<uint8_t> oversizedMapFixture(unsigned count);
namespace {void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}}
void testDatasetFiles(const std::string& temporary,const std::string& map,const std::string& book) {
    const auto root=fs::path(temporary)/"catalog";const auto dataset_root=root.string();fs::create_directories(root);
    auto manifest=[&](unsigned number,const std::string& text){char name[12];std::snprintf(name,sizeof(name),"a%02u",number);const auto dir=root/name;fs::create_directories(dir);std::ofstream(dir/"dataset.ini")<<text;return dir;};
    const std::string ini="[dataset]\nversion=1\nmap=map.glc\ndefault_language=ja\n[ja]\ntitle=日本\ndescription=市区町村\nwordbook=ja.giswordbook\n[en]\ntitle=Japan\nwordbook=en.giswordbook\n";
    auto dir=manifest(1,ini);
    fs::copy_file(map,dir/"map.glc");fs::copy_file(book,dir/"ja.giswordbook");
    // Same dictionary fixture under another language name tests explicit selection;
    // it is intentionally not asserted to contain translated place names.
    fs::copy_file(book,dir/"en.giswordbook");
    {std::ofstream(dir/"unselected.glc")<<"invalid";std::ofstream(dir/"unselected.giswordbook")<<"invalid";}
    for(unsigned n:{2u,10u,21u,55u,99u})manifest(n,ini);
    fs::create_directories(root/"00");fs::create_directories(root/"A100");fs::create_directories(root/"a--bad");
    test_storage::Resources catalog_resources;
    DatasetCatalog catalog(dataset_root,nullptr,0,&catalog_resources.files,&catalog_resources.work);DatasetRequest request{};request.action=DatasetAction::Open;
    auto list=catalog.read(request,UiLanguage::English);
    check(list.count==4 && !list.rows[0].id && list.rows[3].id.view()=="a10" && list.has_newer && !list.has_older,"named ID list is ordered, bounded and paginated without opening GLCs");
    check(!std::strcmp(list.rows[1].title,"Japan"),"catalog chooses the UI title independently");
    request.action=DatasetAction::Newer;request.id.assign("a10");list=catalog.read(request,UiLanguage::English);
    check(list.count==3 && list.rows[0].id.view()=="a21" && list.rows[2].id.view()=="a99" && list.has_older && !list.has_newer,"second page skips sparse IDs and invalid names");
    request.action=DatasetAction::Older;request.id.assign("a21");request.page=1;list=catalog.read(request,UiLanguage::Japanese);
    check(list.count==4 && !list.rows[0].id && list.selected==3,"previous page restores the preceding selection");
    for(const auto purpose:{DatasetPurpose::Rebuild,DatasetPurpose::Verify}) {
        request={};request.action=DatasetAction::Open;request.purpose=purpose;
        list=catalog.read(request,UiLanguage::English);
        check(list.purpose==purpose && list.count==4 && list.rows[0].id.view()=="a01" && list.rows[3].id.view()=="a21","maintenance lists omit None and read metadata only, even where data files are absent");
    }
    manifest(99,"broken metadata");request={};request.action=DatasetAction::Open;request.id.assign("a99");
    list=catalog.read(request,UiLanguage::English);
    check(list.count==1 && list.rows[0].error!=DatasetError::None,"known bad metadata is exposed on the row for pre-emptive disabling");
    manifest(99,ini);
    request={};request.action=DatasetAction::Detail;request.id.assign("a01");
    auto detail=catalog.read(request,UiLanguage::English);
    check(detail.error==DatasetError::None && detail.detail && detail.info.chunks>0 && detail.info.chunks<=1500 && detail.info.unit_inv_x && detail.info.east>detail.info.west,"real GLC supplies resolution/coverage/chunk count");
    check(!std::strcmp(detail.info.language,"ja") && !std::strcmp(detail.info.title,"Japan"),"first choice defaults to dataset language, not UI language");
    request.action=DatasetAction::Language;std::strcpy(request.language,"ja");request.direction=1;
    detail=catalog.read(request,UiLanguage::Japanese);
    check(detail.error==DatasetError::None && !std::strcmp(detail.info.language,"en"),"next language selects the exact named Wordbook");
    {
        static StorageCodeImages images;StorageScreenResources screens(images.map.image());
        auto* address=images.map.image().data();images.point[0]=0x1234abcd;
        screens.live_map.valid=screens.log_map.valid=true;
        ReadFileSlots files;
        {
            StorageIndexLease lease(images.map,screens);check(bool(lease),"candidate can borrow the existing 96KiB");
            DatasetCatalog borrowed(dataset_root,nullptr,0,&files,lease.work());
            DatasetView received{};DatasetRequest query{};query.action=DatasetAction::Detail;query.id.assign("a01");
            candidate_heap::calls=candidate_heap::bytes=0;candidate_heap::track=true;
            borrowed.readInto(query,UiLanguage::English,received);candidate_heap::track=false;
            check(received.error==DatasetError::None && !candidate_heap::calls && !lease.work()->inUse() && !files.inUse(),
                  "candidate detail uses stack Readers, borrowed buffers and zero C++ allocations");
            check(received.info.chunks==detail.info.chunks && received.info.west==detail.info.west && received.info.east==detail.info.east &&
                  received.info.unit_inv_x==detail.info.unit_inv_x && !std::strcmp(received.info.title,"Japan"),"borrowed map coverage and direct text equal the compatibility result");
            query.action=DatasetAction::Language;std::strcpy(query.language,"ja");query.direction=1;
            candidate_heap::calls=candidate_heap::bytes=0;candidate_heap::track=true;
            borrowed.readInto(query,UiLanguage::Japanese,received);candidate_heap::track=false;
            check(received.error==DatasetError::None && !candidate_heap::calls && !std::strcmp(received.info.language,"en") &&
                  !lease.work()->inUse() && !files.inUse(),"language candidate checks use the same zero-allocation SD path");
            // A bad candidate cannot retain either FIL or borrowed buffer.
            query.id.assign("a02");query.action=DatasetAction::Detail;
            borrowed.readInto(query,UiLanguage::Japanese,received);
            check(received.error==DatasetError::Missing && !lease.work()->inUse() && !files.inUse(),"missing candidate returns all borrowed resources");
        }
        check(!images.map.indexing() && !screens.live_map.valid && !screens.log_map.valid && screens.live_map.pixels==address &&
              screens.log_map.pixels==address && images.point[0]==0x1234abcd,
              "candidate borrowing invalidates retained maps, rebinds the raster and preserves the point cache");
    }
    fs::remove(dir/"en.giswordbook");request.action=DatasetAction::Detail;std::strcpy(request.language,"en");
    check(catalog.read(request,UiLanguage::Japanese).error==DatasetError::Missing,"missing selected Wordbook does not fall back to another file");
    CodeCacheBuffer lookup_pixels{};SelectedDataset lookup_dataset;test_storage::Lookup lookup(lookup_pixels,lookup_dataset,"",dataset_root);lookup.configure("a01","ja");
    check(lookup.prepare().state==PlaceState::DataError,"missing saved index is a dataset error without automatic generation");
    test_index::generate(dir.string(),"map.glc","ja.giswordbook");lookup.close();
    check(lookup.prepare().state==PlaceState::Ready && lookup.selectedLanguage()=="ja","strict manifest tolerates unselected extra files");
    PlaceRequest point{1,1,{35.681641,140.027194,1000,true}};
    check(lookup.search(point).state==PlaceState::Ready && lookup.wordBookCacheStats().entries==1,"configured production lookup resolves coordinates and fills its caches");
    const auto before=lookup.cacheStats();
    DatasetRequest missing{};missing.action=DatasetAction::Detail;missing.id.assign("a02");
    check(catalog.read(missing,UiLanguage::Japanese).error==DatasetError::Missing &&
          lookup.search(point).state==PlaceState::Ready && lookup.cacheStats().fills==before.fills,
          "candidate failure leaves the active lookup and its cached image usable");
    fs::copy_file(book,dir/"en.giswordbook");
    {
        std::ifstream input(dir/"en.giswordbook",std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
        const auto encoding=bytes.find("\"token_encoding\":\"utf-8\"");
        check(encoding!=std::string::npos,"UTF-8 fixture declares token encoding");
        bytes.replace(encoding+std::strlen("\"token_encoding\":\""),5,"cp932");
        std::ofstream output(dir/"en.giswordbook",std::ios::binary|std::ios::trunc);
        output.write(bytes.data(),bytes.size());
    }
    request.action=DatasetAction::Apply;request.id.assign("a01");std::strcpy(request.language,"en");
    check(catalog.read(request,UiLanguage::Japanese).error==DatasetError::None &&
          lookup.search(point).state==PlaceState::Ready && lookup.cacheStats().fills==before.fills,
          "opaque metadata does not reject a candidate or disturb the active UTF-8 caches");
    lookup.close();
    manifest(1,ini+"display_line1=%1%2%3%4\ndisplay_line2=%0 %%\n");
    lookup.configure("a01","ja");
    const auto defaults=lookup.search(point);
    check(defaults.state==PlaceState::Ready && !std::strcmp(defaults.lines[0],"千葉県 習志野市"),
          "unselected English format does not affect the selected Japanese Wordbook");
    const auto ja_end=ini.find("[en]");
    manifest(1,ini.substr(0,ja_end)+"display_line1=%1%2%3%4\ndisplay_line2=%0 %%\n"+ini.substr(ja_end));
    lookup.configure("a01","ja");
    const auto formatted=lookup.search(point);
    check(formatted.state==PlaceState::Ready && !std::strcmp(formatted.lines[0],"習志野市") &&
          !std::strcmp(formatted.lines[1],"千葉県 %"), "real dictionary fields expand into the configured final two rows");
    auto cached=lookup.search(point);
    check(!std::memcmp(cached.lines,formatted.lines,sizeof(cached.lines)) && lookup.wordBookCacheStats().hits==1,
          "Wordbook cache returns the final formatted rows without re-expansion");
    manifest(1,ini.substr(0,ja_end)+"display_line1=%0\ndisplay_line2=%A\n"+ini.substr(ja_end));
    request.action=DatasetAction::Apply;std::strcpy(request.language,"ja");
    check(catalog.read(request,UiLanguage::Japanese).error==DatasetError::Metadata &&
          !std::memcmp(lookup.search(point).lines,formatted.lines,sizeof(formatted.lines)),
          "malformed candidate format cannot replace a running formatted cache");
    fs::remove(dir/"dataset.ini");lookup.close();
    check(lookup.prepare().state==PlaceState::DatasetMissing,"reopen cannot fall back to legacy extension detection");
    manifest(1,ini);std::strcpy(request.language,"ja");
    const auto oversized=oversizedMapFixture(1501);
    {std::ofstream file(dir/"map.glc",std::ios::binary);file.write(reinterpret_cast<const char*>(oversized.data()),oversized.size());}
    detail=catalog.read(request,UiLanguage::Japanese);
    check(detail.error==DatasetError::TooManyChunks && detail.info.chunks==1501,"candidate is rejected at exact 1500/1501 boundary before applying");
}

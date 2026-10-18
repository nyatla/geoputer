#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include "task/storage/dataset/DatasetCatalog.h"
#include "task/storage/galuchat/PlaceLookup.h"
using namespace geoputer;
namespace fs=std::filesystem;
std::vector<uint8_t> oversizedMapFixture(unsigned count);
namespace {void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}}
void testDatasetFiles(const std::string& temporary,const std::string& map,const std::string& book) {
    const auto root=fs::path(temporary)/"catalog";fs::create_directories(root);
    auto manifest=[&](unsigned number,const std::string& text){char name[12];std::snprintf(name,sizeof(name),"%02u",number);const auto dir=root/name;fs::create_directories(dir);std::ofstream(dir/"dataset.ini")<<text;return dir;};
    const std::string ini="[dataset]\nversion=1\nmap=map.glc\ndefault_language=ja\n[ja]\ntitle=日本\ndescription=市区町村\nwordbook=ja.giswordbook\n[en]\ntitle=Japan\nwordbook=en.giswordbook\n";
    auto dir=manifest(1,ini);
    fs::copy_file(map,dir/"map.glc");fs::copy_file(book,dir/"ja.giswordbook");
    // Same dictionary fixture under another language name tests explicit selection;
    // it is intentionally not asserted to contain translated place names.
    fs::copy_file(book,dir/"en.giswordbook");
    {std::ofstream(dir/"unselected.glc")<<"invalid";std::ofstream(dir/"unselected.giswordbook")<<"invalid";}
    for(unsigned n:{2u,10u,21u,55u,99u})manifest(n,ini);
    fs::create_directories(root/"00");fs::create_directories(root/"100");fs::create_directories(root/"abc");
    DatasetCatalog catalog(root.string());DatasetRequest request{};request.action=DatasetAction::Open;
    auto list=catalog.read(request,UiLanguage::English);
    check(list.count==4 && list.rows[0].number==1 && list.rows[3].number==21 && list.has_newer && !list.has_older,"numeric ID list is ordered, bounded and paginated without opening GLCs");
    check(!std::strcmp(list.rows[0].title,"Japan"),"catalog chooses the UI title independently");
    request.action=DatasetAction::Newer;request.number=21;list=catalog.read(request,UiLanguage::English);
    check(list.count==2 && list.rows[0].number==55 && list.rows[1].number==99 && list.has_older && !list.has_newer,"second page skips sparse IDs and excludes 00/100/abc");
    request.action=DatasetAction::Older;request.number=55;list=catalog.read(request,UiLanguage::Japanese);
    check(list.count==4 && list.rows[0].number==1 && list.selected==3,"previous page restores the preceding selection");
    request={};request.action=DatasetAction::Detail;request.number=1;
    auto detail=catalog.read(request,UiLanguage::English);
    check(detail.error==DatasetError::None && detail.detail && detail.info.chunks>0 && detail.info.chunks<=1500 && detail.info.unit_inv_x && detail.info.east>detail.info.west,"real GLC supplies resolution/coverage/chunk count");
    check(!std::strcmp(detail.info.language,"ja") && !std::strcmp(detail.info.title,"Japan"),"first choice defaults to dataset language, not UI language");
    request.action=DatasetAction::Language;std::strcpy(request.language,"ja");request.direction=1;
    detail=catalog.read(request,UiLanguage::Japanese);
    check(detail.error==DatasetError::None && !std::strcmp(detail.info.language,"en"),"next language selects the exact named Wordbook");
    fs::remove(dir/"en.giswordbook");request.action=DatasetAction::Detail;std::strcpy(request.language,"en");
    check(catalog.read(request,UiLanguage::Japanese).error==DatasetError::Missing,"missing selected Wordbook does not fall back to another file");
    PlaceLookup lookup(dir.string());lookup.configure(dir.string(),"ja");
    check(lookup.prepare().state==PlaceState::Ready && lookup.selectedLanguage()=="ja","strict manifest tolerates unselected extra files");
    PlaceRequest point{1,1,{35.681641,140.027194,1000,true}};
    check(lookup.search(point).state==PlaceState::Ready && lookup.wordBookCacheStats().entries==1,"configured production lookup resolves coordinates and fills its caches");
    const auto before=lookup.cacheStats();
    DatasetRequest missing{};missing.action=DatasetAction::Detail;missing.number=2;
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
    request.action=DatasetAction::Apply;request.number=1;std::strcpy(request.language,"en");
    check(catalog.read(request,UiLanguage::Japanese).error==DatasetError::Encoding &&
          lookup.search(point).state==PlaceState::Ready && lookup.cacheStats().fills==before.fills,
          "unsupported candidate language fails validation without disturbing the active UTF-8 caches");
    lookup.close();
    manifest(1,ini+"display_line1=%1%2%3%4\ndisplay_line2=%0 %%\n");
    lookup.configure(dir.string(),"ja");
    const auto defaults=lookup.search(point);
    check(defaults.state==PlaceState::Ready && !std::strcmp(defaults.lines[0],"千葉県 習志野市"),
          "unselected English format does not affect the selected Japanese Wordbook");
    const auto ja_end=ini.find("[en]");
    manifest(1,ini.substr(0,ja_end)+"display_line1=%1%2%3%4\ndisplay_line2=%0 %%\n"+ini.substr(ja_end));
    lookup.configure(dir.string(),"ja");
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

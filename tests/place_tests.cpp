#include "support/PlaceFormatOracle.h"
#include <cstring>
#include <stdexcept>
#include <string>
#include "task/storage/galuchat/TextEncoding.h"
#include "task/storage/galuchat/PlaceLineOutput.h"
#include "task/storage/PlaceSearchFlow.h"
#include "task/ui/PlacePresentation.h"
#include "task/ui/GpsUiModel.h"

using namespace geoputer;
namespace {
void check(bool value, const char* detail) { if (!value) throw std::runtime_error(detail); }
}

static void testPlaceLoans() {
    UiData data{};data.settings=defaultSettings();data.settings.dataset=1;
    data.dataset={PlaceState::Ready,1,1500,4};
    GpsUiModel ui;check(ui.setScreen({ScreenId::LivePlace,DisplayMode::View,1,data,0,ActionResult::None})==OperationStatus::Ok,"place loan root screen");
    PlaceRequest request{1,17,{35.68,140.02,0,true}};
    request.token=100;request.dataset_generation=4;request.work_bytes=config::IndexReadBufferBytes;
    check(ui.beginPlaceSearch(request,request.buffer)==OperationStatus::Ok && validPlaceMemory(request),"main screen lends aligned scratch and result space");
    auto malformed=request;++malformed.buffer.data;--malformed.buffer.capacity;
    check(!validPlaceMemory(malformed),"misaligned place layout must be rejected");
    malformed=request;malformed.buffer.capacity=sizeof(PlaceReadWorkspace)-1;
    check(!validPlaceMemory(malformed),"undersized place layout must be rejected");
    auto* result=placeResultMemory(request);
    check(result->region_code==0 && !result->lines[0][0] && !result->detail[0],"only the new result starts empty");
    auto& reader=placeReaderMemory(request);
    const auto reader_offset=reinterpret_cast<uint8_t*>(&reader)-request.buffer.data;
    check(samePlaceRequest(result->request,request) && reinterpret_cast<uint8_t*>(result)==request.buffer.data &&
          reinterpret_cast<uint8_t*>(&reader)>=reinterpret_cast<uint8_t*>(result+1) &&
          reinterpret_cast<uint8_t*>(&reader)+sizeof(reader)<=request.buffer.data+request.buffer.capacity,"reply is first and scratch is disjoint and bounded");
    std::memset(reader.data(),0xa5,reader.size());
    check(samePlaceRequest(result->request,request),"index scratch cannot overwrite result identity");
    auto next=request;next.buffer={};++next.token;map::BufferLease denied{};
    check(ui.beginPlaceSearch(next,denied)==OperationStatus::Unavailable,"one outstanding work loan at a time");
    result->state=PlaceState::Ready;result->dataset=data.dataset;result->dataset.place_text_mode=PlaceTextMode::Ascii8x16;
    result->region_code=42;std::strcpy(result->lines[0],"AREA ONE");
    check(ui.commitPlaceSearch(request)==OperationStatus::Ok && !std::strcmp(ui.data().place[0],"AREA ONE") &&
          ui.data().dataset.place_text_mode==PlaceTextMode::Ascii8x16 && !ui.mapFrame().leased(),
          "UI commits name, font mode, and returns workspace");
    next.screen_generation=1;check(ui.beginPlaceSearch(next,next.buffer)==OperationStatus::Ok,"next main lookup");
    const auto stale=next;
    ui.setScreen({ScreenId::Settings,DisplayMode::View,2,data,0,ActionResult::None});
    auto* late=placeResultMemory(next);late->state=PlaceState::Ready;std::strcpy(late->lines[0],"LATE NAME");
    check(ui.commitPlaceSearch(next)==OperationStatus::StaleContent && ui.mapFrame().leased(),"screen switch preserves storage memory and rejects publication");
    ui.setScreen({ScreenId::Map,DisplayMode::View,3,data,0,ActionResult::None});
    map::Request map_request{};map_request.token=200;map_request.screen_generation=3;map_request.dataset_generation=4;
    check(ui.beginMapFrame(map_request,denied)==OperationStatus::Unavailable,"map cannot overwrite an unfinished name lookup");
    check(ui.discardPlaceSearch(next)==OperationStatus::Ok,"return after writer completion regardless of current screen");
    check(ui.beginMapFrame(map_request,map_request.buffer)==OperationStatus::Ok,"map can use returned scratch slot");
    map::FrameInfo frame{};frame.request=map_request;frame.width=240;frame.height=101;frame.meters_per_pixel=1;
    check(map_request.buffer.data[reader_offset]==0xa5,"lending must not clear the writer scratch");
    std::memset(map_request.buffer.data,0,map::BoundaryBytes); // Completed writer initializes every pixel.
    map_request.buffer.data[0]=0x60;
    check(ui.commitMapFrame(frame)==OperationStatus::Ok,"publish map before name lookup");
    next={3,18,{35.68,140.02,0,true}};next.token=101;next.dataset_generation=4;next.work_bytes=config::IndexReadBufferBytes;
    check(ui.beginPlaceSearch(next,next.buffer)==OperationStatus::Ok,"map screen lends only unused back");
    auto& next_reader=placeReaderMemory(next);std::memset(next_reader.data(),0xff,next_reader.size());
    check(ui.mapFrame().image().data()[0]==0x60 && ui.mapFrame().info().request.token==200,"name scratch cannot change visible map");
    check(ui.discardPlaceSearch(stale)==OperationStatus::StaleContent && ui.mapFrame().leased(),"old return cannot revoke new loan");
    auto* found=placeResultMemory(next);found->state=PlaceState::Ready;found->dataset=data.dataset;
    std::strcpy(found->lines[0],"AREA TWO");
    check(ui.commitPlaceSearch(next)==OperationStatus::Ok && ui.mapFrame().info().request.token==200 &&
          !std::strcmp(ui.data().place[0],"AREA TWO"),"name commit leaves map front unchanged");
}

void testPlace() {
    testPlaceLoans();
    char direct[config::PlaceResultLineBytes]{};char expected_direct[2][config::PlaceCachedLineBytes];
    const std::string long_name="Comunidad de Palacios de la Sierra, Vilviestre del Pinar y San Leonardo de Yagüe";
    formatPlaceLines({long_name},expected_direct);
    PlaceLineOutput direct_row(direct,sizeof(direct));direct_row.append(long_name);
    check(direct_row.full() && !std::strcmp(direct,expected_direct[0]),"direct row uses the existing display clipping");
    char narrow[config::PlaceResultLineBytes]{};
    PlaceLineOutput narrow_row(narrow,sizeof(narrow),PlaceTextMode::Ascii8x16);
    narrow_row.append("12345678901234567890123456789");
    check(!narrow_row.full() && std::strlen(narrow)==29,"8x16 place row accepts 29 ASCII glyphs");
    PlaceLineOutput narrow_clipped(narrow,sizeof(narrow),PlaceTextMode::Ascii8x16);
    narrow_clipped.append("123456789012345678901234567890");
    check(narrow_clipped.full() && !std::strcmp(narrow,"1234567890123456789012345678…"),
          "8x16 place row reserves one cell for the ellipsis");
    check(placeCharacterWidth('A',PlaceTextMode::Ascii8x16)==8 &&
          placeCharacterWidth(0xe5,PlaceTextMode::Ascii8x16)==16 &&
          placeCharacterWidth('A',PlaceTextMode::Japanese16)==16,
          "non-ASCII remains wide and Japanese ASCII remains full width");
    const std::string fifty_eight(58,'x');
    char logical[config::PlaceResultLineBytes]{};
    PlaceLineOutput logical_row(logical,sizeof(logical),PlaceTextMode::Ascii8x16,placeLogicalLinePixels());
    logical_row.append(fifty_eight);
    check(!logical_row.full() && std::strcmp(logical,fifty_eight.c_str())==0,
          "two 8x8 rows retain 58 ASCII characters before UI layout");
    PlaceLineOutput logical_overflow(logical,sizeof(logical),PlaceTextMode::Ascii8x16,placeLogicalLinePixels());
    logical_overflow.append(fifty_eight+"x");
    check(logical_overflow.full() && std::string(logical)==std::string(57,'x')+EllipsisUtf8,
          "logical row reserves the second 8x8 row's ellipsis");
    char visual[2][config::PlaceLinePixels/8+EllipsisBytes]{};
    wrapAsciiPlaceLine(logical,visual);
    check(std::string(visual[0])==std::string(29,'x') &&
          std::string(visual[1])==std::string(28,'x')+EllipsisUtf8,
          "long ASCII stays within two independent 29-cell visual rows");
    wrapAsciiPlaceLine("New York State Administrative Region",visual);
    check(!std::strcmp(visual[0],"New York State Administrative") &&
          !std::strcmp(visual[1],"Region"),
          "word boundary is preferred when the second visual row fits");
    char display[config::PlaceCachedLineBytes]{};
    fitPlaceDisplayLine(fifty_eight.c_str(),display,PlaceTextMode::Ascii8x16);
    check(std::string(display)==std::string(28,'x')+EllipsisUtf8,
          "map and point views still shorten one-row ASCII names");
    char names[2][config::PlaceCachedLineBytes]{};
    std::strcpy(names[0],fifty_eight.c_str());std::strcpy(names[1],"Town");
    check(liveAsciiWrapNeeded(names,PlaceTextMode::Ascii8x16) &&
          !liveAsciiWrapNeeded(names,PlaceTextMode::Japanese16),
          "only ASCII names in the non-Japanese Wordbook use Live's four-row layout");
    const std::string twenty_kanji = [] {std::string value;for(int i=0;i<20;++i)value+="日";return value;}();
    PlaceLineOutput japanese_logical(logical,sizeof(logical),PlaceTextMode::Japanese16,placeLogicalLinePixels());
    japanese_logical.append(twenty_kanji);
    check(!japanese_logical.full() && std::strcmp(logical,twenty_kanji.c_str())==0,
          "Japanese logical row keeps more than Live's fourteen wide glyphs");
    fitPlaceDisplayLine(logical,display,PlaceTextMode::Japanese16,true);
    check(std::strcmp(display,twenty_kanji.c_str())==0,
          "the map's 8x8 first row can display twenty Japanese glyphs");
    fitPlaceDisplayLine(logical,display,PlaceTextMode::Japanese16);
    check(std::string(display)==std::string("日日日日日日日日日日日日日…"),
          "the 16x16 Japanese row clips independently of the map's 8x8 row");
    for(size_t i=std::strlen(direct)+1;i<sizeof(direct);++i)check(!direct[i],"clipped reply tail must stay empty");
    char unicode[config::PlaceResultLineBytes]{};PlaceLineOutput unicode_row(unicode,sizeof(unicode));
    unicode_row.append("千葉県");unicode_row.append(" ");unicode_row.append("習志野市");
    check(!std::strcmp(unicode,"千葉県 習志野市") && validUtf8(unicode),"direct UTF-8 output preserves complete characters");
    char prefix[8]{};PlaceLineOutput prefix_row(prefix,sizeof(prefix));
    std::memcpy(prefix_row.tail(),"千葉県",7);prefix_row.accept(7,true);
    check(validUtf8(prefix) && prefix_row.full(),"byte capacity never publishes a split UTF-8 character");
    check(validUtf8("千葉県習志野市鷺沼二丁目") && validUtf8("髙﨑ｱA") && validUtf8("\xF0\x9F\x9A\x80"),
          "UTF-8 Japanese, compatibility characters, half-width kana, ASCII and supplementary characters");
    char lines[2][config::PlaceCachedLineBytes];
    for (const auto& bad : {std::string("\x81"), std::string("\x90\xE7\x97\x74"),
                           std::string("\xC0\xAF"), std::string("\xED\xA0\x80"), std::string("\xE3\x81"),
                           std::string("\xF4\x90\x80\x80"), std::string("\xF0\x80\x80\x80"),
                           std::string("a\0b", 3)}) {
        check(!validUtf8(bad), "reject legacy bytes, NUL and malformed UTF-8");
        for (unsigned component = 0; component < 6; ++component) {
            std::vector<std::string> parts(6); parts[component] = bad;
            bool rejected = false;
            try { formatPlaceLines(parts, lines); } catch (const std::runtime_error&) { rejected = true; }
            check(rejected, "all hierarchy slots reject invalid UTF-8 without conversion");
        }
    }
    formatPlaceLines({"千葉県", "", "習志野市", "", "鷺沼", "二丁目"}, lines);
    check(std::strcmp(lines[0], "千葉県 習志野市") == 0 && std::strcmp(lines[1], "鷺沼二丁目") == 0,
          "default six-slot layout produces final two display rows");
    formatPlaceLines({"東京都", "", "", "千代田区", ""}, lines);
    check(std::strcmp(lines[0], "東京都 千代田区") == 0 && !lines[1][0], "default N03 administrative layout");
    formatPlaceLines({"東京都", "あいうえおかきくけこさしすせそたちつてと"}, lines);
    check(std::strcmp(lines[0], "東京都 あいうえおかきくけこ…") == 0 && validUtf8(lines[0]),
          "ellipsis uses final pixel width with an eight-dot space");
    formatPlaceLines({"", "習志野市"}, lines);
    check(std::strcmp(lines[0], "習志野市") == 0, "missing prefecture has no invented separator");
    formatPlaceLines({}, lines);
    check(!lines[0][0] && !lines[1][0], "empty names clear both display lines");
    PlaceDisplayFormat format{{"%0 %1%2%3", "%4%5"}, true};
    formatPlaceLines({"千葉県", "", "習志野市", "", "鷺沼", "2丁目"}, lines, format);
    check(std::strcmp(lines[0], "千葉県 習志野市") == 0 && std::strcmp(lines[1], "鷺沼2丁目") == 0,
          "percent placeholders preserve empty slots and create two final rows");
    format = PlaceDisplayFormat{{"%2/%%/%0", "%9:%1"}, true};
    formatPlaceLines({"%1", "", "市"}, lines, format);
    check(!std::strcmp(lines[0], "市/%/%1") && !std::strcmp(lines[1], ":"),
          "reordering, percent escape and missing slots work without recursively interpreting dictionary text");
    format = PlaceDisplayFormat{{"%1", "%0"}, true};
    formatPlaceLines({"A", "B"}, lines, format);
    check(!std::strcmp(lines[0], "B") && !std::strcmp(lines[1], "A"), "explicit rows are not joined again by App");
    format = PlaceDisplayFormat{{"%10", "%%%0"}, true};
    formatPlaceLines({"A", "B"}, lines, format);
    check(!std::strcmp(lines[0], "B0") && !std::strcmp(lines[1], "%A"), "indices are exactly one digit");
    format = PlaceDisplayFormat{{"", ""}, true};
    formatPlaceLines({"unused"}, lines, format);
    check(!lines[0][0] && !lines[1][0], "explicit empty formats can hide both rows");
    for (const auto* bad : {"%", "%A", "%/", "abc%%def%"}) {
        check(!validPlaceFormat(bad), "malformed placeholder is rejected");
        format = PlaceDisplayFormat{}; format.specified=true;check(format.setLine(0,bad),"invalid syntax still fits fixed storage"); bool rejected = false;
        try { formatPlaceLines({"A"}, lines, format); } catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "runtime formatting also rejects malformed syntax");
    }
    format = PlaceDisplayFormat{{"%0", "%0%0"}, true};
    const std::string spaces(29, ' ');
    formatPlaceLines({spaces}, lines, format);
    check(std::string(lines[0]) == spaces && std::string(lines[1]) == std::string(27, ' ') + EllipsisUtf8,
          "space-only rows fit exactly 232 pixels, then truncate with a fitting ellipsis");
    const std::string fourteen = "日日日日日日日日日日日日日日";
    formatPlaceLines({fourteen + " "}, lines, format);
    check(std::string(lines[0]) == fourteen + " ", "fourteen 16-dot glyphs plus an eight-dot space fit exactly");
    formatPlaceLines({std::string(200000, 'x')}, lines, format);
    check(!std::strcmp(lines[0], "xxxxxxxxxxxxx…") && validUtf8(lines[1]), "large expansions retain only a bounded prefix");
    format = PlaceDisplayFormat{{"%0", ""}, true}; bool rejected = false;
    try { formatPlaceLines({"valid", "\xc0\x80"}, lines, format); } catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "unreferenced invalid UTF-8 cannot enter the name cache");

    PlaceSearchFlow flow;
    PlaceRequest request{7, 1, {35.68, 140.02, 0, true}};
    check(flow.waitMs(1) == 0, "first valid fix can search immediately");
    flow.sent(100, request);
    check(flow.accepts(100, 7, 7) && !flow.accepts(99, 7, 7) && !flow.accepts(100, 8, 7),
          "only current request progress can light an access lamp");
    check(flow.waitMs(9) == UINT32_MAX, "new GPS fixes cannot flood a pending search");
    check(!flow.completed(99, 7, 7) && flow.pending(), "unrelated result cannot release request");
    check(flow.completed(100, 7, 7) && !flow.pending(), "matching completion releases request");
    check(flow.waitMs(2) == 0, "changed fix is not delayed by a five-second timer");
    check(flow.waitMs(1) == UINT32_MAX, "same GPS sample is not searched repeatedly");
    flow.sent(101, request);
    flow.refresh();
    check(flow.pending(), "view change cannot abandon accepted operation");
    check(!flow.completed(101, 7, 8), "old view completion cannot update current view");
    check(flow.waitMs(1) == 0, "new view requests a fresh snapshot after old completion");
    flow.sent(102, request);
    flow.refresh();  // GPS became invalid and recovered in the same view.
    check(!flow.accepts(102, 7, 7), "late progress cannot light a lamp after fix loss");
    check(!flow.completed(102, 7, 7), "fix invalidation rejects old result even in same view");

    PlaceSearchFlow lattice;
    PlaceRequest cell_request{7,1,{35.68,140.02,0,true}};
    lattice.sent(1,cell_request);check(lattice.completed(1,7,7),"lattice first result");
    auto moved=cell_request.fix;moved.longitude+=0.00001;
    check(lattice.waitMs(2,moved,10000,10000)==UINT32_MAX,"same dataset cell avoids a new request");
    moved.longitude+=0.00006;
    check(lattice.waitMs(2,moved,10000,10000)==0,"changed cell searches immediately without timer delay");
    moved=cell_request.fix;moved.latitude+=0.00006;
    check(lattice.waitMs(2,moved,10000,10000)==0,"north-south lattice crossing also searches");
    lattice.refresh();
    check(lattice.waitMs(2,cell_request.fix,10000,10000)==0,"refresh invalidates same-cell suppression");
    check(!lattice.sameCell(cell_request.fix,0,0),"unopened dataset cannot suppress a lookup");

    UiData data{};
    data.settings = defaultSettings();
    std::strcpy(data.place[0], "千葉県 習志野市");
    data.place_fix = {35.681641, 140.027194, 1000, true};
    const auto old_fix = data.place_fix;
    beginPlacePresentation(data, request);
    check(std::strcmp(data.place[0], "千葉県 習志野市") == 0 &&
          data.place_fix.latitude == old_fix.latitude && data.place_fix.longitude == old_fix.longitude,
          "background search retains the old name and its own coordinates without blanking");
    PlaceResult result{};
    result.request = request;
    result.state = PlaceState::Ready;
    std::strcpy(result.lines[0], "東京都");
    std::strcpy(result.lines[1], "千代田区");
    data.storage.sd = {2, 100, false};
    data.storage.rg = {1, 1, RgCacheState::Ready};
    completePlacePresentation(data, result);
    check(std::strcmp(data.place[0], "東京都") == 0 && !std::strcmp(data.place[1], "千代田区") &&
          data.place_fix.latitude == request.fix.latitude,
          "completion replaces all name rows and coordinates together independently of SD activity");

    GpsUiModel model;
    model.tick(1000);
    model.setScreen({ScreenId::LivePlace, DisplayMode::View, 7, data, 0, ActionResult::None});
    check(model.sdAccessLit(), "coalesced start/completion still produces a visible access pulse");
    model.takeRedraw();
    model.tick(1149);
    check(model.sdAccessLit() && !model.takeRedraw(), "short read keeps its lamp visible for 150ms");
    model.tick(1150);
    check(!model.sdAccessLit() && model.takeRedraw(), "pulse expiry schedules a redraw without GPS activity");
    model.updateContent({DisplayMode::View, 7, data, 0, ActionResult::None});
    check(!model.sdAccessLit(), "repeated GPS/content updates do not restart a completed pulse");

    data.storage.sd = {3, 101, true};
    model.updateContent({DisplayMode::View, 7, data, 0, ActionResult::None});
    model.tick(2000);
    check(model.sdAccessLit(), "long access stays lit beyond minimum pulse time");
    data.storage.sd = {4, 101, false};
    model.updateContent({DisplayMode::View, 7, data, 0, ActionResult::None});
    check(!model.sdAccessLit(), "completed long access goes out immediately");

    model.tick(0xFFFFFFF0u);
    data.storage.sd = {5, 102, false};
    model.updateContent({DisplayMode::View, 7, data, 0, ActionResult::None});
    model.tick(100);
    check(model.sdAccessLit(), "access pulse survives timer wrap");
    model.tick(134);
    check(!model.sdAccessLit(), "wrapped pulse expires exactly once");

    data.storage.sd = {6, 103, true};
    model.updateContent({DisplayMode::View, 7, data, 0, ActionResult::None});
    check(model.sdAccessLit(), "new access lights its lamp before GPS loss");
    clearPlacePresentation(data, PlaceState::NoFix);
    model.updateContent({DisplayMode::View, 7, data, 0, ActionResult::None});
    check(!hasPlace(data) && !data.place_fix.valid && model.sdAccessLit() && model.data().storage.rg.state == RgCacheState::Ready &&
          std::strcmp(placeStateName(data.place_state), "WAIT GPS FIX") == 0,
          "GPS loss clears places but preserves actual SD activity and valid RG cache");
    auto old_data = data;
    old_data.storage.sd = {2, 99, false}; old_data.storage.rg = {};
    model.updateContent({DisplayMode::View, 7, old_data, 0, ActionResult::None});
    check(model.data().storage.sd.access_id == 103 && model.data().storage.rg.state == RgCacheState::Ready,
          "stale job snapshots cannot roll back global RG/SD state");
    beginPlacePresentation(data, request);
    check(!hasPlace(data) && data.place_fix.latitude == request.fix.latitude,
          "initial search can show the pending coordinate when no old name exists");
    result.state = PlaceState::DataError;
    std::memset(result.lines, 0, sizeof(result.lines));
    completePlacePresentation(data, result);
    check(!hasPlace(data) && data.place_state == PlaceState::DataError,
          "read errors replace pending content with their actual error state");


    for (auto failure : {PlaceState::DatasetMissing,PlaceState::MetadataError,PlaceState::VersionError,
             PlaceState::DataError,PlaceState::EncodingError,
             PlaceState::TooManyChunks,PlaceState::OutOfMemory,PlaceState::NoCard}) {
        check(datasetUnavailable(failure),"all dataset faults disable reverse geocoding");
        for(bool ja : {false,true}) {
            const auto text=datasetIssueText(failure,ja);
            check(validUtf8(text.first) && validUtf8(text.second) && *text.first && *text.second,"failure has two localized fixed lines");
            for(const auto* line : {text.first,text.second}) {
                unsigned width=0;
                for(const auto* p=reinterpret_cast<const unsigned char*>(line);*p;++p) if((*p&0xc0)!=0x80)width+=*p==' '?8:16;
                check(width<=232,"localized dataset error fits native name region");
            }
        }
        UiData rejected{};rejected.settings=defaultSettings();rejected.storage.card_state=CardState::Mounted;
        rejected.dataset={failure,2000,1500,7};rejected.gps.reception=GpsReception::Fix;rejected.gps.fix.valid=true;
        check(placeIssue(rejected)==failure && mainError(rejected)==nullptr,"dataset-only fault does not create duplicate red band");
        rejected.gps.fix.valid=false;
        check(placeIssue(rejected)==failure,"NO FIX cannot replace dataset error");
        GpsUiModel warning;warning.setScreen({ScreenId::LivePlace,DisplayMode::View,1,rejected,0,ActionResult::None});
        warning.keyPressed(UiKey::OpenSettings,10);
        check(warning.snapshot().action.target==ScreenId::Settings,"dataset fault never blocks direct settings access");
        GpsUiModel recovery;recovery.setScreen({ScreenId::LivePlace,DisplayMode::View,1,rejected,0,ActionResult::None});
        recovery.keyPressed(UiKey::Escape,10);
        check(recovery.snapshot().action.kind==ActionKind::ChangeScreen && recovery.snapshot().action.target==ScreenId::Menu,"dataset error retains MENU access for settings recovery");
        DatasetInfo state{failure,2000,1500,7};
        check(!applyDatasetInfo(state,{PlaceState::Ready,1500,1500,7}) && state.state==failure,
              "same-generation late success cannot clear a dataset error");
        check(!applyDatasetInfo(state,{PlaceState::Ready,1500,1500,6}),"old dataset success cannot clear current error");
        check(applyDatasetInfo(state,{PlaceState::Ready,1500,1500,8}) && state.state==PlaceState::Ready,
              "explicit successful revalidation with a new generation clears error");
        check(!applyDatasetInfo(state,{failure,2000,1500,7}) && state.state==PlaceState::Ready,"old failure cannot poison repaired selection");
    }
    UiData unselected{};unselected.settings=defaultSettings();unselected.dataset={PlaceState::Unselected,0,1500,8};
    unselected.storage.card_state=CardState::Mounted;unselected.gps.reception=GpsReception::Fix;unselected.gps.fix.valid=true;
    check(datasetInactive(PlaceState::Unselected) && !datasetUnavailable(PlaceState::Unselected),"unselected disables lookup without being a fault");
    check(placeIssue(unselected)==PlaceState::Unselected && mainError(unselected)==nullptr,"unselected does not create a red error band or block GPS");
    for(bool ja:{false,true}) {const auto text=datasetIssueText(PlaceState::Unselected,ja);check(*text.first && *text.second && validUtf8(text.first) && validUtf8(text.second),"unselected has localized display text");}
    std::strcpy(unselected.place[0],"old name");result.state=PlaceState::Unselected;completePlacePresentation(unselected,result);
    check(!hasPlace(unselected) && !unselected.place_fix.valid,"unselection clears old place text and coordinates");
    DatasetInfo state{PlaceState::Unselected,0,1500,8};
    check(!applyDatasetInfo(state,{PlaceState::Ready,1,1500,7}) && !applyDatasetInfo(state,{PlaceState::Ready,1,1500,8}),"late place success cannot undo unselection");
    check(applyDatasetInfo(state,{PlaceState::Ready,1,1500,9}),"explicit selection of a new generation can restore place lookup");
    check(!datasetUnavailable(PlaceState::NoFix) && !datasetUnavailable(PlaceState::NotFound),"NO FIX and zero region do not reject the dataset");
}

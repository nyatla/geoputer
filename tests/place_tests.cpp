#include <cstring>
#include <stdexcept>
#include <string>
#include "task/storage/galuchat/TextEncoding.h"
#include "task/storage/PlaceSearchFlow.h"
#include "task/ui/PlacePresentation.h"
#include "task/ui/UiModel.h"

using namespace geoputer;
namespace {
void check(bool value, const char* detail) { if (!value) throw std::runtime_error(detail); }
}

void testPlace() {
    check(validUtf8("千葉県習志野市鷺沼二丁目") && validUtf8("髙﨑ｱA") && validUtf8("\xF0\x9F\x9A\x80"),
          "UTF-8 Japanese, compatibility characters, half-width kana, ASCII and supplementary characters");
    char lines[2][64];
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
    check(std::strcmp(lines[0], "東京都 あいうえおかきく...") == 0 && validUtf8(lines[0]),
          "ellipsis uses final pixel width with an eight-dot space");
    formatPlaceLines({"", "習志野市"}, lines);
    check(std::strcmp(lines[0], "習志野市") == 0, "missing prefecture has no invented separator");
    formatPlaceLines({}, lines);
    check(!lines[0][0] && !lines[1][0], "empty names clear both display lines");
    PlaceDisplayFormat format{{"%0 %1%2%3", "%4%5"}, true};
    formatPlaceLines({"千葉県", "", "習志野市", "", "鷺沼", "2丁目"}, lines, format);
    check(std::strcmp(lines[0], "千葉県 習志野市") == 0 && std::strcmp(lines[1], "鷺沼2丁目") == 0,
          "percent placeholders preserve empty slots and create two final rows");
    format = {{"%2/%%/%0", "%9:%1"}, true};
    formatPlaceLines({"%1", "", "市"}, lines, format);
    check(!std::strcmp(lines[0], "市/%/%1") && !std::strcmp(lines[1], ":"),
          "reordering, percent escape and missing slots work without recursively interpreting dictionary text");
    format = {{"%1", "%0"}, true};
    formatPlaceLines({"A", "B"}, lines, format);
    check(!std::strcmp(lines[0], "B") && !std::strcmp(lines[1], "A"), "explicit rows are not joined again by App");
    format = {{"%10", "%%%0"}, true};
    formatPlaceLines({"A", "B"}, lines, format);
    check(!std::strcmp(lines[0], "B0") && !std::strcmp(lines[1], "%A"), "indices are exactly one digit");
    format = {{"", ""}, true};
    formatPlaceLines({"unused"}, lines, format);
    check(!lines[0][0] && !lines[1][0], "explicit empty formats can hide both rows");
    for (const auto* bad : {"%", "%A", "%/", "abc%%def%"}) {
        check(!validPlaceFormat(bad), "malformed placeholder is rejected");
        format = {{bad, ""}, true}; bool rejected = false;
        try { formatPlaceLines({"A"}, lines, format); } catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "runtime formatting also rejects malformed syntax");
    }
    format = {{"%0", "%0%0"}, true};
    const std::string spaces(29, ' ');
    formatPlaceLines({spaces}, lines, format);
    check(std::string(lines[0]) == spaces && std::string(lines[1]) == std::string(23, ' ') + "...",
          "space-only rows fit exactly 232 pixels, then truncate with a fitting ellipsis");
    const std::string fourteen = "日日日日日日日日日日日日日日";
    formatPlaceLines({fourteen + " "}, lines, format);
    check(std::string(lines[0]) == fourteen + " ", "fourteen 16-dot glyphs plus an eight-dot space fit exactly");
    formatPlaceLines({std::string(200000, 'x')}, lines, format);
    check(!std::strcmp(lines[0], "xxxxxxxxxxx...") && validUtf8(lines[1]), "large expansions retain only a bounded prefix");
    format = {{"%0", ""}, true}; bool rejected = false;
    try { formatPlaceLines({"valid", "\xc0\x80"}, lines, format); } catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "unreferenced invalid UTF-8 cannot enter the name cache");

    PlaceSearchFlow flow;
    PlaceRequest request{7, 1, {35.68, 140.02, 0, true}};
    check(flow.waitMs(0xFFFFFF00, 1) == 0, "first valid fix can search immediately");
    flow.sent(100, request, 0xFFFFFF00);
    check(flow.accepts(100, 7, 7) && !flow.accepts(99, 7, 7) && !flow.accepts(100, 8, 7),
          "only current request progress can light an access lamp");
    check(flow.waitMs(2000, 9) == UINT32_MAX, "new GPS fixes cannot flood a pending search");
    check(!flow.completed(99, 7, 7) && flow.pending(), "unrelated result cannot release request");
    check(flow.completed(100, 7, 7) && !flow.pending(), "matching completion releases request");
    check(flow.waitMs(0xFFFFFF00 + 2000u, 2) == 0, "changed fix is not delayed by a five-second timer");
    check(flow.waitMs(0xFFFFFF00 + 5000u, 2) == 0, "changed fix remains eligible across timer wrap");
    check(flow.waitMs(9000, 1) == UINT32_MAX, "same GPS sample is not searched repeatedly");
    flow.sent(101, request, 9000);
    flow.refresh();
    check(flow.pending(), "view change cannot abandon accepted operation");
    check(!flow.completed(101, 7, 8), "old view completion cannot update current view");
    check(flow.waitMs(9001, 1) == 0, "new view requests a fresh snapshot after old completion");
    flow.sent(102, request, 10000);
    flow.refresh();  // GPS became invalid and recovered in the same view.
    check(!flow.accepts(102, 7, 7), "late progress cannot light a lamp after fix loss");
    check(!flow.completed(102, 7, 7), "fix invalidation rejects old result even in same view");

    PlaceSearchFlow lattice;
    PlaceRequest cell_request{7,1,{35.68,140.02,0,true}};
    lattice.sent(1,cell_request,0);check(lattice.completed(1,7,7),"lattice first result");
    auto moved=cell_request.fix;moved.longitude+=0.00001;
    check(lattice.waitMs(1,2,moved,10000,10000)==UINT32_MAX,"same dataset cell avoids a new request");
    moved.longitude+=0.00006;
    check(lattice.waitMs(1,2,moved,10000,10000)==0,"changed cell searches immediately without timer delay");
    moved=cell_request.fix;moved.latitude+=0.00006;
    check(lattice.waitMs(1,2,moved,10000,10000)==0,"north-south lattice crossing also searches");
    lattice.refresh();
    check(lattice.waitMs(1,2,cell_request.fix,10000,10000)==0,"refresh invalidates same-cell suppression");
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

    UiModel model;
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
             PlaceState::AmbiguousFiles,PlaceState::DataError,PlaceState::EncodingError,
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
        UiModel warning;warning.setScreen({ScreenId::LivePlace,DisplayMode::View,1,rejected,0,ActionResult::None});
        warning.keyPressed(UiKey::OpenSettings,10);
        check(warning.snapshot().action.target==ScreenId::Settings,"dataset fault never blocks direct settings access");
        UiModel recovery;recovery.setScreen({ScreenId::LivePlace,DisplayMode::View,1,rejected,0,ActionResult::None});
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

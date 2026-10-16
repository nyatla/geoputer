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
    check(cp932ToUtf8("\x90\xE7\x97\x74\x8C\xA7\x8F\x4B\x8E\x75\x96\xEC\x8E\x73\x8D\xEB\x8F\xC0\x93\xF1\x92\x9A\x96\xDA") ==
          "千葉県習志野市鷺沼二丁目", "CP932 address converts to UTF-8");
    check(cp932ToUtf8("\xEE\xE0\xED\x95\xB1\x41") == "髙﨑ｱA", "CP932 extensions, half-width kana and ASCII");
    for (const auto& bad : {std::string("\x81"), std::string("\x81\x30"), std::string("\x81\xAD"), std::string(1, '\0')}) {
        bool rejected = false;
        try { cp932ToUtf8(bad); } catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "invalid CP932 is reported rather than displayed as UTF-8");
    }
    check(!validUtf8("\xC0\xAF") && !validUtf8("\xED\xA0\x80") && !validUtf8("\xE3\x81"), "reject malformed UTF-8");
    char lines[3][64];
    formatPlaceLines({"千葉県", "", "習志野市", "", "鷺沼", "二丁目"}, lines);
    check(std::strcmp(lines[0], "千葉県") == 0 && std::strcmp(lines[1], "習志野市") == 0 &&
          std::strcmp(lines[2], "鷺沼二丁目") == 0, "Integrated six slots preserve empty hierarchy slots");
    formatPlaceLines({"東京都", "", "", "千代田区", ""}, lines);
    check(std::strcmp(lines[1], "千代田区") == 0 && !lines[2][0], "N03 five-slot hierarchy");
    formatPlaceLines({"東京都", "あいうえおかきくけこさしすせそたちつてと"}, lines);
    check(std::strcmp(lines[1], "あいうえおかきくけこさ...") == 0 && validUtf8(lines[1]), "long names truncate at 14 cells without splitting UTF-8");

    char address[2][64];
    formatLivePlaceLines(lines,address);
    check(std::strcmp(address[0],"東京都 あいうえおかき...")==0 && validUtf8(address[0]),
          "combined administrative line is truncated at a UTF-8 cell boundary");
    formatPlaceLines({"千葉県","","習志野市","","鷺沼","2丁目1番1号"},lines);
    formatLivePlaceLines(lines,address);
    check(std::strcmp(address[0],"千葉県 習志野市")==0 && std::strcmp(address[1],"鷺沼2丁目1番1号")==0,
          "three hierarchy rows become two display lines");
    check(std::strcmp(lines[1],"習志野市")==0 && std::strcmp(lines[2],"鷺沼2丁目1番1号")==0,
          "display formatting does not mutate Storage's hierarchy");
    formatPlaceLines({"千葉県","","","習志野市",""},lines);
    formatLivePlaceLines(lines,address);
    check(std::strcmp(address[0],"千葉県 習志野市")==0 && !address[1][0],
          "N03 has one administrative display line without an invented street address");
    std::memset(lines,0,sizeof(lines)); std::strcpy(lines[1],"習志野市");
    formatLivePlaceLines(lines,address);
    check(std::strcmp(address[0],"習志野市")==0,"missing prefecture has no leading separator");
    std::memset(lines,0,sizeof(lines)); formatLivePlaceLines(lines,address);
    check(!address[0][0] && !address[1][0],"empty hierarchy clears both address lines");

    PlaceSearchFlow flow;
    PlaceRequest request{7, 1, {35.68, 140.02, 0, true}};
    check(flow.waitMs(0xFFFFFF00, 1) == 0, "first valid fix can search immediately");
    flow.sent(100, request, 0xFFFFFF00);
    check(flow.accepts(100, 7, 7) && !flow.accepts(99, 7, 7) && !flow.accepts(100, 8, 7),
          "only current request progress can light an access lamp");
    check(flow.waitMs(2000, 9) == UINT32_MAX, "new GPS fixes cannot flood a pending search");
    check(!flow.completed(99, 7, 7) && flow.pending(), "unrelated result cannot release request");
    check(flow.completed(100, 7, 7) && !flow.pending(), "matching completion releases request");
    check(flow.waitMs(0xFFFFFF00 + 2000u, 2) == 3000, "search deadline survives timer wrap");
    check(flow.waitMs(0xFFFFFF00 + 5000u, 2) == 0, "new fix is searched at five-second deadline");
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
    data.storage.sd = {2, 100, false};
    data.storage.rg = {1, 1, RgCacheState::Ready};
    completePlacePresentation(data, result);
    check(std::strcmp(data.place[0], "東京都") == 0 && !data.place[1][0] &&
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

    for (const auto close_key : {UiKey::Escape, UiKey::Enter}) {
        UiModel warning;
        UiData rejected{};
        rejected.settings = defaultSettings(); rejected.revision = 1;
        rejected.dataset = {PlaceState::TooManyChunks, 2000, config::MaxDatasetChunks};
        warning.setScreen({ScreenId::LivePlace, DisplayMode::View, 1, rejected, 0, ActionResult::None});
        check(warning.datasetWarning(), "unusable dataset opens an acknowledgement warning");
        warning.tick(100); warning.tick(100000);
        warning.keyPressed(UiKey::Menu, 100001);
        check(warning.snapshot().action.kind == ActionKind::None && warning.datasetWarning(),
              "dataset warning does not sleep or allow menu keys to bypass acknowledgement");
        warning.keyPressed(close_key, 100002);
        check(!warning.datasetWarning() && warning.snapshot().action.kind == ActionKind::None,
              "Esc/Del or Enter closes the warning without activating the underlying screen");
        ++rejected.revision;
        warning.updateContent({DisplayMode::View, 1, rejected, 0, ActionResult::None});
        check(!warning.datasetWarning(), "ordinary GPS/UI updates cannot reopen an acknowledged warning");
        rejected.dataset.chunks = 2001; ++rejected.revision;
        warning.updateContent({DisplayMode::View, 1, rejected, 0, ActionResult::None});
        check(warning.datasetWarning(), "a different rejected dataset opens a fresh warning");
        rejected.dataset = {PlaceState::Ready, 172, config::MaxDatasetChunks}; ++rejected.revision;
        warning.updateContent({DisplayMode::View, 1, rejected, 0, ActionResult::None});
        check(!warning.datasetWarning(), "a corrected dataset clears its warning");
    }
    UiModel boot_warning;
    UiData boot_data{};
    boot_data.settings = defaultSettings();
    boot_data.dataset = {PlaceState::TooManyChunks, 2000, config::MaxDatasetChunks};
    boot_warning.setScreen({ScreenId::Boot, DisplayMode::View, 1, boot_data, 0, ActionResult::None});
    check(!boot_warning.datasetWarning(), "dataset warning does not cover startup checks");
    boot_warning.setScreen({ScreenId::LivePlace, DisplayMode::View, 2, boot_data, 0, ActionResult::None});
    check(boot_warning.datasetWarning(), "startup failure remains available for acknowledgement in Live Place");
}

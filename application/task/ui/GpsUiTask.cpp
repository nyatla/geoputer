#include "../../utils/Console.h"
#include "GpsUiTask.h"
#include "../../utils/HeapDiagnostics.h"
#include <M5Cardputer.h>
#include "GpsUiRenderer.h"
#include "KeyBindings.h"
#include "../../utils/SyncClient.h"
#include "../../hardware/AdvKeyboardReader.h"

namespace geoputer {

void GpsUiTask::handle(const UiRequest& request, bool ready) {
    if (request.source != TaskId::Application || request.request_id == 0) {
        Console.println("[UI] invalid request envelope");
        return;
    }
    prepareSyncReturn(pending_return_, TaskId::UI, request, ++sequence_,
                      ready ? OperationStatus::InvalidRequest : OperationStatus::Unavailable);
    auto& result = pending_return_.payload.result;
    if (ready) {
        switch (request.kind) {
            case MessageKind::SetUiScreen: {
                const auto old_map_token=model_.mapFrame().info().request.token;
                result.status = model_.setScreen(request.payload.screen);
                if(result.status==OperationStatus::Ok && old_map_token)reportMapHeap("ui-hidden",old_map_token);
                break;
            }
            case MessageKind::UpdateUiContent:
                result.status = model_.updateContent(request.payload.update);
                break;
            case MessageKind::BeginDatasetRead:
                result.status=model_.beginDatasetRead(request.payload.dataset.request,result.map_buffer);break;
            case MessageKind::CommitDatasetRead:result.status=model_.commitDatasetRead(request.payload.dataset);break;
            case MessageKind::DiscardDatasetRead:result.status=model_.discardDatasetRead(request.payload.dataset);break;
            case MessageKind::DatasetReadError:result.status=model_.datasetReadError(request.payload.dataset);break;
            case MessageKind::UpdateDatasetProgress:result.status=model_.updateDatasetProgress(request.payload.dataset_progress);break;
            case MessageKind::BeginPlaceSearch:
                result.status=model_.beginPlaceSearch(request.payload.place,result.map_buffer);break;
            case MessageKind::CommitPlaceSearch:
                result.status=model_.commitPlaceSearch(request.payload.place);break;
            case MessageKind::DiscardPlaceSearch:
                result.status=model_.discardPlaceSearch(request.payload.place);break;
            case MessageKind::BeginMapFrame:
                result.status=model_.beginMapFrame(request.payload.map.request,result.map_buffer);
                if(result.status==OperationStatus::Ok)reportMapHeap("ui-begin",request.payload.map.request.token);
                break;
            case MessageKind::DiscardMapFrame: result.status=model_.discardMapFrame(request.payload.map.request); break;
            case MessageKind::CommitMapFrame:
                result.status=model_.commitMapFrame(request.payload.map);
                if(result.status==OperationStatus::Ok)reportMapHeap("ui-commit",request.payload.map.request.token);
                break;
            case MessageKind::ControlAudio:
                result.status=audio_.command(request.payload.audio,taskTimeMs())?OperationStatus::Ok:OperationStatus::Unavailable;
                if(result.status==OperationStatus::Ok)model_.setAudioPreview(request.payload.audio.play && request.payload.audio.preview);
                model_.setAudioActive(audio_.active());break;
            case MessageKind::GetUiContent:
                result.status = OperationStatus::Ok;
                result.content = model_.snapshot();  // Immutable result copy, not an action acknowledgement.
                break;
            default:
                break;
        }
    }
    has_return_ = true;
}

void GpsUiTask::sendReturn() {
    if (has_return_ && sendSpeculative(results_, pending_return_) == SpeculativeResult::Queued) {
        has_return_ = false;
    }
    // On a full queue keep this same result; never repeat the request's operation.
}

void GpsUiTask::notifyContentChanged() {
    if (!model_.notificationPending()) return;
    const auto content = model_.snapshot();
    AppNotification event{};
    event.kind = MessageKind::UiContentChanged;
    event.source = TaskId::UI;
    event.sequence = ++sequence_;
    event.sent_at_ms = taskTimeMs();
    event.payload.changed = {content.content_revision, content.screen_generation};
    if (sendSpeculative(notifications_, event) == SpeculativeResult::Queued) model_.notificationSent();
}

void GpsUiTask::run() {
    // LCD, keyboard and their M5Stack driver state are only accessed by this task.
    auto config = M5.config();
    config.internal_spk = power_manager_.current().audio_available;
    config.internal_mic = false;
    // Keep the board's automatic keyboard update off: this task owns polling.
    M5Cardputer.begin(config, false);
    audio_.begin(power_manager_.current(),taskTimeMs());
    if (M5.getBoard() == m5::board_t::board_M5CardputerADV) {
        auto reader = std::make_unique<AdvKeyboardReader>(M5Cardputer.Keyboard.maxKeys());
        auto* state = reader.get();
        M5Cardputer.Keyboard.begin(std::move(reader));
        Console.println(state->ready() ? "[UI] ADV physical key snapshot ready" : "[UI] ADV keyboard initialization failed");
    } else M5Cardputer.Keyboard.begin();
    auto update_board = [] {
        M5Cardputer.update();
        M5Cardputer.Keyboard.updateKeyList();
        M5Cardputer.Keyboard.updateKeysState();
    };
    // Sample before accepting any App request. A held physical ` must be
    // latched even if it is released while the initial screen is prepared.
    update_board();
    bool startup_escape = false;
    for (const auto& position : M5Cardputer.Keyboard.keyList())
        startup_escape |= M5Cardputer.Keyboard.getKeyValue(position).value_first == '`';
    if (startup_escape) {
        model_.keyPressed(UiKey::Escape, taskTimeMs());
        Console.println("[UI] startup ESC held; maintenance latched");
    }
    GpsUiRenderer renderer(M5Cardputer.Display);
    const bool ready = renderer.begin(config::UiColorDepth, frame_buffer_.data, sizeof(frame_buffer_.data));
    Console.println(ready ? "[UI] ready (240x135)" : "[UI] initialization failed");
    uint8_t was_viewer=0;
    constexpr auto& viewer_keys=LocalShortcutKeys;
    constexpr unsigned viewer_key_count=LocalShortcutCount;
    bool was_one = false, was_gps = false, was_logs = false, was_escape = startup_escape || M5Cardputer.Keyboard.keysState().del, was_up = false, was_down = false;
    bool was_left = false, was_right = false, was_enter = false, was_any = false, was_tab = false;
    bool was_delete = false, was_map = false, was_settings = false, was_off = false, was_control = false;
    uint32_t repeat_at = 0;
    bool clock_reported = false;
    uint32_t battery_at = 0;
    bool battery_sampled = false;
    for (;;) {
        sendReturn();
        UiRequest request{};
        if (!has_return_ && inbox().receive(request)) {
            handle(request, ready);
            sendReturn();  // Return after copying/accepting, before rendering.
        }
        update_board();
        power_manager_.configure(model_.data().settings.power_level,power_manager_.mode());
        audio_.setPowerPolicy(power_manager_.current());
        audio_.tick(taskTimeMs());model_.setAudioActive(audio_.active());
        const auto& keys = M5Cardputer.Keyboard.keysState();
        // Normalize physical keys before detecting edges; adding/removing Fn
        // while holding a navigation key must not create another press.
        // Del/Backspace is Back/Cancel; only the physical ` selects maintenance.
        const bool back = keys.del;
        bool live = false, gps = false, logs = false, escape = false, up = false, down = false;
        bool left = false, right = false;
        uint8_t viewer=0;
        bool delete_log = false, open_map = false, settings = false, off = false, control = false;
        for (const auto& position : M5Cardputer.Keyboard.keyList()) {
            const auto value = M5Cardputer.Keyboard.getKeyValue(position).value_first;
            if(value==KEY_TAB)continue; // HID Tab (0x2b) is not ASCII '+'.
            const auto key = model_.viewingLicenses() ? mapLicenseKey(value, keys.fn, keys.shift) : mapPhysicalKey(value, keys.fn);
            for(unsigned i=0;i<viewer_key_count;++i)if(key==viewer_keys[i])viewer|=1u<<i;
            settings |= key == UiKey::OpenSettings;
            off |= key == UiKey::ScreenOff;
            control |= key == UiKey::GpsControl;
            gps |= key == UiKey::OpenGpsLogger;
            live |= key == UiKey::OpenLivePlace;
            logs |= key == UiKey::OpenLogs;
            open_map |= key == UiKey::OpenMap;
            delete_log |= key == UiKey::DeleteLog;
            escape |= key == UiKey::Escape;
            up |= key == UiKey::Up;
            down |= key == UiKey::Down;
            left |= key == UiKey::Left;
            right |= key == UiKey::Right;
        }
        const uint32_t now = taskTimeMs();
        const bool enter = keys.enter, tab = keys.tab;
        const bool one = live; // Fn changes do not create another physical press edge.
        const bool any = M5Cardputer.Keyboard.isPressed() || keys.fn || keys.shift || keys.ctrl || keys.alt;
        if (ready) {
            if (!battery_sampled || now - battery_at >= 5000) {
                const auto charging = M5Cardputer.Power.isCharging();
                model_.setBattery(M5Cardputer.Power.getBatteryLevel(), charging == m5::Power_Class::is_charging,
                                  charging != m5::Power_Class::charge_unknown);
                battery_sampled = true; battery_at = now;
            }
            for(unsigned i=0;i<viewer_key_count;++i)if((viewer&(1u<<i)) && !(was_viewer&(1u<<i)))model_.keyPressed(viewer_keys[i],now);
            if(tab && !was_tab)model_.keyPressed(UiKey::Tab,now);
            if (one && !was_one) model_.keyPressed(UiKey::OpenLivePlace, now);
            if (settings && !was_settings) model_.keyPressed(UiKey::OpenSettings, now);
            if (off && !was_off) model_.keyPressed(UiKey::ScreenOff, now);
            if (control && !was_control) model_.keyPressed(UiKey::GpsControl, now);
            if (gps && !was_gps) model_.keyPressed(UiKey::OpenGpsLogger, now);
            if(open_map && !was_map)model_.keyPressed(UiKey::OpenMap,now);
            if (logs && !was_logs) model_.keyPressed(UiKey::OpenLogs, now);
            if (delete_log && !was_delete) model_.keyPressed(UiKey::DeleteLog, now);
            if ((escape || back) && !was_escape) model_.keyPressed(escape ? UiKey::Escape : UiKey::Back, now);
            if (up && !was_up) model_.keyPressed(UiKey::Up, now);
            if (down && !was_down) model_.keyPressed(UiKey::Down, now);
            if (left && !was_left) model_.keyPressed(UiKey::Left, now);
            if (right && !was_right) model_.keyPressed(UiKey::Right, now);
            if (enter && !was_enter) model_.keyPressed(UiKey::Enter, now);
            if (any && !was_any && !viewer && !one && !settings && !off && !control && !gps && !logs && !open_map && !delete_log && !escape && !back && !up && !down && !left && !right && !enter && !tab) model_.keyPressed(UiKey::Other, now);
            if ((up && !was_up) || (down && !was_down)) repeat_at = now + 400;
            else if ((up || down) && repeat_at && static_cast<int32_t>(now - repeat_at) >= 0) {
                model_.keyPressed(up ? UiKey::Up : UiKey::Down, now);
                repeat_at = now + 100;
            }
            model_.tick(now);
            if (!clock_reported && model_.clock().known()) {
                char clock_text[9];
                model_.clock().format(clock_text, sizeof(clock_text));
                Console.printf("[UI] clock=%s JST (initial GPS time)\n", clock_text);
                clock_reported = true;
            }
        }
        was_viewer=viewer;was_one = one;
        was_settings = settings; was_off = off; was_control = control;
        was_gps = gps;
        was_logs = logs;was_map=open_map;
        was_delete = delete_log;
        was_escape = escape || back;
        was_up = up;
        was_down = down;
        was_left = left; was_right = right; was_enter = enter; was_any = any; was_tab=tab;
        if (!up && !down) repeat_at = 0;
        notifyContentChanged();
        if (ready && model_.takeRedraw()) {
            renderer.draw(model_);
        }
        // Rendering/SD-driven updates can exceed 10 ms. An absolute deadline
        // would then run catch-up iterations without blocking and starve IDLE.
        // Always yield after a UI iteration, including slow full-screen draws.
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

}  // namespace geoputer

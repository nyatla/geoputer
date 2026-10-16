#include "UiTask.h"
#include <M5Cardputer.h>
#include "UiRenderer.h"
#include "KeyBindings.h"
#include "../../utils/SyncClient.h"

namespace geoputer {

void UiTask::handle(const TaskMessage& request, bool ready) {
    if (request.source != TaskId::Application || request.request_id == 0) {
        Serial.println("[UI] invalid request envelope");
        return;
    }
    pending_return_ = {};
    pending_return_.kind = MessageKind::SyncReturn;
    pending_return_.source = TaskId::UI;
    pending_return_.request_id = request.request_id;
    pending_return_.sequence = ++sequence_;
    pending_return_.sent_at_ms = taskTimeMs();
    pending_return_.payload.result = {
        request.kind, ready ? OperationStatus::InvalidRequest : OperationStatus::Unavailable, {}, {}, {}
    };
    auto& result = pending_return_.payload.result;
    if (ready) {
        switch (request.kind) {
            case MessageKind::SetUiScreen:
                result.status = model_.setScreen(request.payload.screen);
                break;
            case MessageKind::UpdateUiContent:
                result.status = model_.updateContent(request.payload.update);
                break;
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

void UiTask::sendReturn() {
    if (has_return_ && sendSpeculative(results_, pending_return_) == SpeculativeResult::Queued) {
        has_return_ = false;
    }
    // On a full queue keep this same result; never repeat the request's operation.
}

void UiTask::notifyContentChanged() {
    if (!model_.notificationPending()) return;
    const auto content = model_.snapshot();
    TaskMessage event{};
    event.kind = MessageKind::UiContentChanged;
    event.source = TaskId::UI;
    event.sequence = ++sequence_;
    event.sent_at_ms = taskTimeMs();
    event.payload.changed = {content.content_revision, content.screen_generation};
    if (sendSpeculative(notifications_, event) == SpeculativeResult::Queued) model_.notificationSent();
}

void UiTask::run() {
    // LCD, keyboard and their M5Stack driver state are only accessed by this task.
    auto config = M5.config();
    config.internal_spk = false;
    config.internal_mic = false;
    M5Cardputer.begin(config, true);
    UiRenderer renderer(M5Cardputer.Display);
    const bool ready = renderer.begin();
    Serial.println(ready ? "[UI] ready (240x135)" : "[UI] initialization failed");
    bool was_f1 = false, was_menu = false, was_escape = false, was_up = false, was_down = false;
    bool was_left = false, was_right = false, was_enter = false, was_any = false;
    uint32_t repeat_at = 0;
    bool clock_reported = false;
    uint32_t battery_at = 0;
    bool battery_sampled = false;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        sendReturn();
        TaskMessage request{};
        if (!has_return_ && inbox().receive(request)) {
            handle(request, ready);
            sendReturn();  // Return after copying/accepting, before rendering.
        }
        M5Cardputer.update();
        const auto& keys = M5Cardputer.Keyboard.keysState();
        // Normalize physical keys before detecting edges; adding/removing Fn
        // while holding a navigation key must not create another press.
        // Physical Del/Backspace and Fn+` share one Back/Cancel press edge.
        bool f1 = false, menu = false, escape = keys.del, up = false, down = false;
        bool left = false, right = false;
        for (const auto& position : M5Cardputer.Keyboard.keyList()) {
            const auto key = mapPhysicalKey(M5Cardputer.Keyboard.getKeyValue(position).value_first, keys.fn);
            f1 |= key == UiKey::F1;
            menu |= key == UiKey::Menu;
            escape |= key == UiKey::Escape;
            up |= key == UiKey::Up;
            down |= key == UiKey::Down;
            left |= key == UiKey::Left;
            right |= key == UiKey::Right;
        }
        const uint32_t now = taskTimeMs();
        const bool enter = keys.enter;
        const bool any = M5Cardputer.Keyboard.isPressed() || keys.fn || keys.shift || keys.ctrl || keys.alt;
        if (ready) {
            if (!battery_sampled || now - battery_at >= 5000) {
                const auto charging = M5Cardputer.Power.isCharging();
                model_.setBattery(M5Cardputer.Power.getBatteryLevel(), charging == m5::Power_Class::is_charging,
                                  charging != m5::Power_Class::charge_unknown);
                battery_sampled = true; battery_at = now;
            }
            if (f1 && !was_f1) model_.keyPressed(UiKey::F1, now);
            if (menu && !was_menu) model_.keyPressed(UiKey::Menu, now);
            if (escape && !was_escape) model_.keyPressed(UiKey::Escape, now);
            if (up && !was_up) model_.keyPressed(UiKey::Up, now);
            if (down && !was_down) model_.keyPressed(UiKey::Down, now);
            if (left && !was_left) model_.keyPressed(UiKey::Left, now);
            if (right && !was_right) model_.keyPressed(UiKey::Right, now);
            if (enter && !was_enter) model_.keyPressed(UiKey::Enter, now);
            if (any && !was_any && !f1 && !menu && !escape && !up && !down && !left && !right && !enter) model_.keyPressed(UiKey::Other, now);
            if ((up && !was_up) || (down && !was_down)) repeat_at = now + 400;
            else if ((up || down) && repeat_at && static_cast<int32_t>(now - repeat_at) >= 0) {
                model_.keyPressed(up ? UiKey::Up : UiKey::Down, now);
                repeat_at = now + 100;
            }
            model_.tick(now);
            if (!clock_reported && model_.clock().known()) {
                char clock_text[9];
                model_.clock().format(clock_text, sizeof(clock_text));
                Serial.printf("[UI] clock=%s JST (initial GPS time)\n", clock_text);
                clock_reported = true;
            }
        }
        was_f1 = f1;
        was_menu = menu;
        was_escape = escape;
        was_up = up;
        was_down = down;
        was_left = left; was_right = right; was_enter = enter; was_any = any;
        if (!up && !down) repeat_at = 0;
        notifyContentChanged();
        if (ready && model_.takeRedraw()) renderer.draw(model_);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(10));
    }
}

}  // namespace geoputer

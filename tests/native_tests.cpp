#include <cstring>
#include <iostream>
#include <stdexcept>
#include "utils/SyncClient.h"
#include "task/ui/UiModel.h"

using namespace geoputer;
void testMaps();
void testGps();
void testSettings();
void testPlace();
void testBoot();
void testDatasets();
void testDatasetSelection();
void testClock();
void testFonts();
void testLicenses();
void testLogging();
void testLogRing();
void testLogReader();
void testLiveLayout();
void testWordBookCache();
void testKeyBindings();
void testSettingsLanguage();
void testDialogs();
void testUsbMode();
void testExport();

static void check(bool value, const char* detail) {
    if (!value) throw std::runtime_error(detail);
}

static ReturnMessage terminal(const UiRequest& request, OperationStatus status = OperationStatus::Ok) {
    ReturnMessage result{};
    prepareSyncReturn(result, TaskId::UI, request, 1, status);
    return result;
}

static void testLateReturnAndSeparateNotifications() {
    Queue<ReturnMessage, 1> returns;
    Queue<AppNotification, 1> notifications;
    Queue<UiRequest, 2> destination;
    returns.initialize();
    notifications.initialize();
    destination.initialize();
    SyncClient client(returns);
    AppNotification notification{};
    notification.kind = MessageKind::UiContentChanged;
    check(notifications.send(notification), "normal queue accepts a notification");
    UiRequest request{};
    request.kind = MessageKind::GetUiContent;
    check(client.sendSync(TaskId::UI, destination, request, 0) == SyncSendResult::AwaitingResult,
          "initial sync request is accepted");
    UiRequest accepted{};
    check(destination.receive(accepted), "subtask receives one request");
    check(accepted.request_id != 0 && accepted.source == TaskId::Application, "request identity");
    SyncReturn result{};
    for (unsigned i = 0; i < 3; ++i) {
        check(client.receiveSyncResult(result, 0) == SyncReceiveResult::ResultTimeout, "receive times out");
        check(client.waiting(), "timeout retains outstanding request");
        check(client.sendSync(TaskId::UI, destination, request, 0) == SyncSendResult::SyncPendingError,
              "new request is forbidden after a timeout");
    }
    check(!destination.receive(request), "timeout does not resend accepted work");
    const auto completed = terminal(accepted, OperationStatus::Unavailable);
    check(sendSpeculative(returns, completed) == SpeculativeResult::Queued,
          "late failure can return despite full ordinary queue");
    check(sendSpeculative(returns, completed) == SpeculativeResult::QueueFull, "return capacity is one");
    check(client.receiveSyncResult(result, 0) == SyncReceiveResult::Returned, "original late result retrieved");
    check(result.status == OperationStatus::Unavailable && !client.waiting(), "terminal failure ends waiting");
    check(notifications.receive(notification) && notification.kind == MessageKind::UiContentChanged,
          "ordinary notification survives sync waits");
    check(returns.send(completed), "place protocol test value in return queue");
    check(client.receiveSyncResult(result, 0) == SyncReceiveResult::NoPendingSyncError,
          "idle retrieval is an error");
    ReturnMessage unconsumed{};
    check(returns.receive(unconsumed), "idle retrieval does not consume return queue");
}

static void testSendFailuresAndReturnIdentity() {
    Queue<ReturnMessage, 1> returns;
    Queue<UiRequest, 1> destination, unavailable;
    destination.initialize();
    SyncClient client(returns);
    UiRequest request{};
    request.kind = MessageKind::GetUiContent;
    check(client.sendSync(TaskId::UI, destination, request, 0) == SyncSendResult::DestinationUnavailable,
          "uninitialized return queue rejects enqueue");
    check(!client.waiting(), "missing return queue creates no waiting state");
    returns.initialize();
    check(client.sendSync(TaskId::UI, unavailable, request, 0) == SyncSendResult::DestinationUnavailable,
          "uninitialized destination is reported");
    check(!client.waiting(), "unavailable send creates no waiting state");
    destination.send(request);
    check(client.sendSync(TaskId::UI, destination, request, 0) == SyncSendResult::QueueTimeout,
          "full destination rejects sync enqueue");
    check(!client.waiting(), "enqueue failure creates no waiting state");
    destination.receive(request);
    client.sendSync(TaskId::UI, destination, request, 0);
    destination.receive(request);
    const auto expected = terminal(request);
    SyncReturn result{};
    for (int corruption = 0; corruption < 4; ++corruption) {
        auto unexpected = expected;
        if (corruption == 0) ++unexpected.request_id;
        if (corruption == 1) unexpected.source = TaskId::Application;
        if (corruption == 2) unexpected.payload.result.request_kind = MessageKind::UpdateUiContent;
        if (corruption == 3) unexpected.kind = MessageKind::UiContentChanged;
        returns.send(unexpected);
        check(client.receiveSyncResult(result, 0) == SyncReceiveResult::ProtocolError, "reject wrong return identity");
        check(client.waiting(), "protocol error does not finish pending request");
    }
    returns.send(expected);
    check(client.receiveSyncResult(result, 0) == SyncReceiveResult::Returned, "correct identity completes request");
}

static void testDirectionalPayloadCopies() {
    Queue<StorageRequest, 1> storage;
    Queue<AppNotification, 8> notifications;
    Queue<ReturnMessage, 1> returns;
    storage.initialize(); notifications.initialize(); returns.initialize();
    StorageRequest request{};
    request.kind = MessageKind::ReadLogRecords;
    request.request_id = 91;
    request.payload.log_read.session_id = UINT64_C(0x1234567812345678);
    request.payload.log_read.name[LogNameBytes - 2] = 'N';
    request.payload.log_read.anchor[LogNameBytes - 2] = 'A';
    check(storage.send(request), "compact Storage request is queued");
    StorageRequest received{};
    storage.receive(received);
    check(received.request_id == 91 && received.payload.log_read.session_id == request.payload.log_read.session_id &&
          received.payload.log_read.name[LogNameBytes - 2] == 'N' &&
          received.payload.log_read.anchor[LogNameBytes - 2] == 'A',
          "largest Storage request survives its smaller queue intact");
    AppNotification event{};
    event.kind = MessageKind::ReadCompleted; event.source = TaskId::Storage;
    auto& result = event.payload.storage_completion;
    result.read_kind = StorageReadKind::LogBrowse;
    result.logs.request = request.payload.log_read;
    result.logs.count = LogReadCount;
    for (unsigned n = 0; n < LogReadCount; ++n) for (unsigned b = 0; b < 32; ++b)
        result.logs.data.records[n][b] = static_cast<uint8_t>(n * 32 + b);
    for (unsigned n = 0; n < 8; ++n) check(notifications.send(event), "all eight notification slots remain available");
    check(!notifications.send(event), "notification capacity is still eight");
    ReturnMessage reply{};
    reply.kind = MessageKind::SyncReturn; reply.source = TaskId::Storage;
    reply.payload.result.storage.log.saved = 12345;
    check(returns.send(reply), "full notification queue does not block a Storage return");
    reply = {}; returns.receive(reply);
    check(reply.payload.result.storage.log.saved == 12345, "Storage return snapshot survives the compact return queue");
    // Task-owned replies are reused across commands; no previous operation's
    // UI/GPS/storage payload may survive initialization of a new result.
    reply.payload.result.content.action.kind = ActionKind::Logging;
    reply.payload.result.gps.fix.valid = true;
    for (auto source : {TaskId::UI, TaskId::GPS, TaskId::Storage}) {
        prepareSyncReturn(reply, source, request, 17, OperationStatus::Unavailable);
        check(reply.kind == MessageKind::SyncReturn && reply.source == source &&
              reply.request_id == request.request_id && reply.sequence == 17 && reply.sent_at_ms == taskTimeMs() &&
              reply.payload.result.request_kind == request.kind && reply.payload.result.status == OperationStatus::Unavailable,
              "each subtask returns the current request identity and terminal status");
        check(reply.payload.result.content.action.kind == ActionKind::None && !reply.payload.result.gps.fix.valid &&
              reply.payload.result.storage.log.saved == 0,
              "a reused reply cannot leak a previous operation's snapshots");
        reply.payload.result.content.action.kind = ActionKind::Logging;
        reply.payload.result.gps.fix.valid = true;
        reply.payload.result.storage.log.saved = 12345;
    }
    while (notifications.receive(event)) {
        const auto& copied = event.payload.storage_completion.logs;
        check(copied.request.session_id == request.payload.log_read.session_id && copied.count == LogReadCount,
              "log batch identity is retained");
        for (unsigned n = 0; n < LogReadCount; ++n) for (unsigned b = 0; b < 32; ++b)
            check(copied.data.records[n][b] == static_cast<uint8_t>(n * 32 + b),
                  "all ten binary records survive the compact notification queue");
    }
}

static void testSyncCallerEnvelopeAndQueuedSnapshot() {
    Queue<ReturnMessage, 1> returns;
    Queue<UiRequest, 1> destination;
    returns.initialize(); destination.initialize();
    SyncClient client(returns);
    UiRequest request{};
    request.kind = MessageKind::SetUiScreen;
    request.payload.screen.generation = 42;
    request.payload.screen.data.datasets.detail = true;
    request.payload.screen.data.datasets.info = {};
    std::strcpy(request.payload.screen.data.datasets.info.description, "UTF-8 dataset description");
    check(client.sendSync(TaskId::UI, destination, request, 0) == SyncSendResult::AwaitingResult,
          "full UI request is accepted from a caller-owned envelope");
    check(request.source == TaskId::Application && request.request_id && request.sequence,
          "sync stamps the existing envelope rather than making a full request copy");
    const auto first_id = request.request_id;
    const auto first_sequence = request.sequence;
    check(client.sendSync(TaskId::UI, destination, request, 0) == SyncSendResult::SyncPendingError &&
          request.request_id == first_id && request.sequence == first_sequence,
          "rejected overlapping sync does not mutate the outstanding envelope");
    request.payload.screen.generation = 99;
    request.payload.screen.data.datasets.info.description[0] = 'X';
    UiRequest queued{};
    check(destination.receive(queued) && queued.request_id == first_id &&
          queued.payload.screen.generation == 42 &&
          !std::strcmp(queued.payload.screen.data.datasets.info.description, "UTF-8 dataset description"),
          "queue owns an independent value snapshot after the send returns");
    auto reply = terminal(queued);
    returns.send(reply);
    SyncReturn result{};
    check(client.receiveSyncResult(result, 0) == SyncReceiveResult::Returned && !client.waiting(),
          "caller reuse cannot alter return matching for the accepted request");
}

static SetUiScreen initialScreen() {
    SetUiScreen command{};
    command.screen = ScreenId::LivePlace;
    command.mode = DisplayMode::View;
    command.generation = 1;
    command.data.revision = 1;
    std::strcpy(command.data.place[0], "千葉県");
    return command;
}

static void testUiActionOwnershipAndStaleUpdates() {
    UiModel model;
    auto command = initialScreen();
    check(model.setScreen(command) == OperationStatus::Ok, "initial screen accepted");
    check(!model.notificationPending(), "App screen update does not notify App again");
    check(model.takeRedraw() && !model.takeRedraw(), "screen requests one redraw");
    model.keyPressed(UiKey::OpenSettings, 10);
    const auto confirmed = model.snapshot();
    check(model.notificationPending(), "confirmed UI action creates notification");
    check(confirmed.screen == ScreenId::LivePlace, "UI cannot switch top-level screen on its own");
    check(confirmed.action.target == ScreenId::Settings, "Settings shortcut requests Settings");
    model.keyPressed(UiKey::Escape, 20);
    model.keyPressed(UiKey::OpenSettings, 30);
    check(model.snapshot().action.action_id == confirmed.action.action_id, "pending action cannot be overwritten");
    check(model.snapshot().content_revision == confirmed.content_revision, "duplicate key does not repeat action");
    check(confirmed.last_input_ms == 10, "snapshot is not mutated by later keys");
    check(model.snapshot().action.kind == ActionKind::ChangeScreen, "retrieval does not acknowledge action");
    model.notificationSent();
    command.screen = ScreenId::Settings;
    command.generation = 2;
    check(model.setScreen(command) == OperationStatus::InvalidRequest, "screen cannot drop unprocessed action");
    command.processed_action_id = confirmed.action.action_id;
    command.action_result = ActionResult::Applied;
    check(model.setScreen(command) == OperationStatus::Ok, "matching acknowledgement permits transition");
    check(model.snapshot().action.kind == ActionKind::None, "matching acknowledgement clears action");
    check(!model.notificationPending(), "screen acknowledgement creates no feedback loop");
    model.keyPressed(UiKey::Down, 40);
    check(model.snapshot().selected_setting == 1, "selection is local to UI");
    UpdateUiContent update{};
    update.mode = DisplayMode::View;
    update.generation = 1;
    update.data.revision = 10;
    check(model.updateContent(update) == OperationStatus::StaleContent, "old generation cannot overwrite UI");
    update.generation = 2;
    update.data.revision = 0;
    check(model.updateContent(update) == OperationStatus::StaleContent, "old data revision cannot overwrite UI");
    update.data.revision = 2;
    check(model.updateContent(update) == OperationStatus::Ok, "current data update accepted");
    check(model.snapshot().selected_setting == 1, "App content update preserves local selection");
    model.notificationSent();
    model.keyPressed(UiKey::OpenLivePlace, 50);
    const auto back = model.snapshot().action;
    update.processed_action_id = back.action_id + 1;
    update.action_result = ActionResult::Rejected;
    model.updateContent(update);
    check(model.snapshot().action.action_id == back.action_id, "wrong acknowledgement preserves action");
    update.processed_action_id = back.action_id;
    model.updateContent(update);
    check(model.snapshot().action.kind == ActionKind::None, "explicit rejection clears matching action");
}

static void testRepeatedUiRoundTripsAndValueCopies() {
    UiModel model;
    auto command = initialScreen();
    model.setScreen(command);
    command.data.place[0][0] = 'X';
    check(std::strcmp(model.data().place[0], "千葉県") == 0, "UI keeps a value copy of App data");
    for (uint32_t i = 0; i < 1000; ++i) {
        const auto previous = model.snapshot().screen;
        model.keyPressed(previous == ScreenId::LivePlace ? UiKey::OpenSettings : UiKey::OpenLivePlace, i + 1);
        auto snapshot = model.snapshot();
        check(snapshot.screen == previous, "top-level screen stays until App sends it");
        check(snapshot.action.action_id == i + 1, "each confirmed action has a distinct ID");
        command.screen = snapshot.action.target;
        command.generation = snapshot.screen_generation + 1;
        command.processed_action_id = snapshot.action.action_id;
        command.action_result = ActionResult::Applied;
        model.notificationSent();
        check(model.setScreen(command) == OperationStatus::Ok, "App applies requested screen");
        check(model.snapshot().action.kind == ActionKind::None, "completed transition releases fixed action slot");
    }
    check(model.snapshot().screen == ScreenId::LivePlace, "round trips return to main screen");
}

int main() {
    try {
        testLateReturnAndSeparateNotifications();
        testSendFailuresAndReturnIdentity();
        testDirectionalPayloadCopies();
        testSyncCallerEnvelopeAndQueuedSnapshot();
        testUiActionOwnershipAndStaleUpdates();
        testRepeatedUiRoundTripsAndValueCopies();
        testMaps();
        testGps();
        testSettings();
        testSettingsLanguage();
        testDialogs();
        testUsbMode();
        testExport();
        testPlace();
        testWordBookCache();
        testKeyBindings();
        testBoot();
        testDatasets();
        testDatasetSelection();
        testClock();
        testFonts();
        testLicenses();
        testLogging();
        testLogRing();
        testLogReader();
        testLiveLayout();
        std::cout << "PASS: queue/sync, UI/settings/GPS, UTF-8/place search, Boot, JST clock, Shinonome and full license navigation, RG/SD lamps and binary log recovery\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

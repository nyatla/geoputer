#include <cstring>
#include <iostream>
#include <stdexcept>
#include "utils/SyncClient.h"
#include "task/ui/UiModel.h"
#include "AppScreenResources.h"

using namespace geoputer;
void testMaps();
void testStorageReplies();
void testGps();
void testSettings();
void testPlace();
void testBoot();
void testWifiMode();
void testDatasets();
void testDatasetBorrowing();
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
    prepareSyncReturn(reply, TaskId::Storage, request, 1, OperationStatus::Ok);
    reply.payload.result.storage.log.saved = 12345;
    check(returns.send(reply), "full notification queue does not block a Storage return");
    reply = {}; returns.receive(reply);
    check(reply.payload.result.storage.log.saved == 12345, "Storage return snapshot survives the compact return queue");
    // Reuse one task-owned response through every alternative, including the
    // map transfer's final byte. Only the member selected by the request is read.
    const TaskId sources[]{TaskId::UI,TaskId::GPS,TaskId::UI,TaskId::Storage,TaskId::UI};
    const MessageKind kinds[]{MessageKind::GetUiContent,MessageKind::GetGpsStatus,
        MessageKind::BeginMapFrame,MessageKind::GetStorageStatus,MessageKind::GetUiContent};
    for (unsigned i=0;i<5;++i) {
        request.kind=kinds[i];
        prepareSyncReturn(reply, sources[i], request, 17, OperationStatus::Ok);
        auto& value=reply.payload.result;
        check(reply.kind == MessageKind::SyncReturn && reply.source == sources[i] &&
              reply.request_id == request.request_id && reply.sequence == 17 && reply.sent_at_ms == taskTimeMs() &&
              value.request_kind == request.kind && value.status == OperationStatus::Ok,
              "each subtask returns the current request identity and terminal status");
        if (request.kind==MessageKind::BeginMapFrame) {
            check(!value.map_buffer.data && !value.map_buffer.id,"new map loan starts empty");
            static uint8_t pixels[map::BoundaryBytes];
            value.map_buffer={pixels,sizeof(pixels),91};
        } else if (sources[i]==TaskId::UI) {
            check(value.content.action.kind==ActionKind::None,"new UI return starts with no stale action");
            value.content.action.kind=ActionKind::Logging;value.content.content_revision=123;
        } else if (sources[i]==TaskId::GPS) {
            check(!value.gps.fix.valid,"new GPS return starts without a stale fix");
            value.gps.fix.valid=true;value.gps.fix.latitude=35.681641;value.gps.fix.longitude=140.027194;
        } else {
            check(!value.storage.log.saved,"new Storage return starts without stale counters");
            value.storage.log.saved=23456;
        }
        check(returns.send(reply),"each response alternative fits the one-slot queue");
        // FreeRTOS must own its copy even after the producer reuses its response.
        prepareSyncReturn(reply,TaskId::UI,request,18);
        ReturnMessage copied{};check(returns.receive(copied),"receive response alternative");
        const auto& received_value=copied.payload.result;
        if(request.kind==MessageKind::BeginMapFrame)check(received_value.map_buffer.data && received_value.map_buffer.capacity==map::BoundaryBytes && received_value.map_buffer.id==91,"loan identity and pointer survive queue copy");
        else if (sources[i]==TaskId::UI)check(received_value.content.action.kind==ActionKind::Logging && received_value.content.content_revision==123,"UI snapshot preserved");
        else if(sources[i]==TaskId::GPS)check(received_value.gps.fix.valid && received_value.gps.fix.latitude==35.681641 && received_value.gps.fix.longitude==140.027194,"GPS snapshot preserved");
        else check(received_value.storage.log.saved==23456,"Storage snapshot preserved");
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

static void testExclusiveUiPages() {
    UiData data{};
    check(!data.hasBrowse() && !data.hasDatasets(), "UI starts without a page payload");
    check(!data.browse().count && !data.datasets().count && !data.hasBrowse() && !data.hasDatasets(),
          "reading an unused page returns empty data without activating it");
    data.gps.fix={35.68,140.02,1000,true};
    data.recording.state=RecordingState::Recording;
    for (unsigned i=0;i<100;++i) {
        auto& dataset=data.editDatasets();
        check(!dataset.count && !dataset.loading && !dataset.applying, "new dataset payload is zero initialized");
        dataset.detail=true;dataset.info={};dataset.revision=i+1;
        std::strcpy(dataset.info.description,"dataset description");
        UiData copied=data;
        auto& browse=data.editBrowse();
        check(!browse.count && !browse.loading && !browse.deleting && !browse.names[0][0],
              "switching from datasets cannot leave stale browser flags or filenames");
        browse.count=1;browse.revision=i+2;std::strcpy(browse.names[0],"20261025_120000_0.gps.dat");
        check(!data.hasDatasets() && !data.datasets().loading && !data.datasets().applying &&
              copied.hasDatasets() && !std::strcmp(copied.datasets().info.description,"dataset description"),
              "inactive reads are empty and value copies retain the original active payload");
        copied=data;
        const auto& empty=data.datasets();
        check(!empty.count && !empty.detail && data.hasBrowse(), "const page reads preserve the active browser");
        check(copied.hasBrowse() && copied.browse().count==1 && copied.browse().revision==i+2,
              "copy assignment carries the browser discriminator and content");
        check(data.gps.fix.valid && data.gps.fix.latitude==35.68 && data.recording.state==RecordingState::Recording,
              "page changes preserve GPS and logger state");
    }
    UiModel ui;
    check(ui.setScreen({ScreenId::Logs,DisplayMode::View,1,data,0,ActionResult::None})==OperationStatus::Ok,
          "browser payload accepted by UI");
    check(ui.data().hasBrowse(), "UI sanitization preserves browser payload");
    data.editDatasets().detail=true;data.editDatasets().info={};
    std::strcpy(data.editDatasets().info.description,"not overwritten by browser sanitization");
    check(ui.setScreen({ScreenId::Settings,DisplayMode::View,2,data,0,ActionResult::None})==OperationStatus::Ok &&
          ui.data().hasDatasets() && !std::strcmp(ui.data().datasets().info.description,"not overwritten by browser sanitization"),
          "UI sanitizes only the active page and does not activate browser storage");
}

static void testScreenResourceLifetime() {
    AppScreenResources app;
    app.enter(ScreenGroup::Map);app.map().info.region=0xaa;app.map().anchor.request.token=21;
    app.enter(ScreenGroup::Logs);check(app.hasLogs() && !app.logs().cache_count,"log work initializes after map leave");
    app.logs().records[0][0]=0x55;app.logs().cache_count=1;
    app.enter(ScreenGroup::Logs);check(app.logs().cache_count==1 && app.logs().records[0][0]==0x55,"log hierarchy retains its own work");
    app.enter(ScreenGroup::Settings);check(!app.hasMap() && !app.hasLogs(),"settings owns no map/log working payload");
    app.enter(ScreenGroup::Map);check(!app.map().info.region && !app.map().anchor.request.token,"map reentry starts clean");
    app.enter(ScreenGroup::Usb);check(app.hasLogs(),"USB file selection uses bounded log working resources");
    UiScreenResources ui;ui.enter(ScreenGroup::Map);
    map::FrameInfo info{};info.request.token=1;info.width=map::MaxWidth;info.height=map::MaxHeight;info.meters_per_pixel=1;
    check(ui.map.frame.begin(info.request,info.request.buffer),"UI lends fixed staging memory");
    std::memset(info.request.buffer.data,0,map::BoundaryBytes); // Simulate a completed producer.
    check(ui.map.frame.commit(info) && ui.map.frame.image().bytes(),"UI owns a committed fixed image");
    ui.enter(ScreenGroup::Settings);
    check(!ui.map.frame.image().bytes() && !ui.map.cursor.valid,"map image is hidden at screen leave");
    ui.settings.editing=true;ui.settings.dataset.edit_original[0]='x';ui.settings.license_offset=81;
    ui.enter(ScreenGroup::Usb);
    check(!ui.settings.editing && !ui.settings.dataset.edit_original[0] && !ui.settings.license_offset && ui.usb.page==UsbPage::Menu,
          "departed settings and license state are cleared before USB entry");
}

static void testSyncCallerEnvelopeAndQueuedSnapshot() {
    Queue<ReturnMessage, 1> returns;
    Queue<UiRequest, 1> destination;
    returns.initialize(); destination.initialize();
    SyncClient client(returns);
    UiRequest request{};
    request.kind = MessageKind::SetUiScreen;
    request.payload.screen.generation = 42;
    request.payload.screen.data.editDatasets().detail = true;
    request.payload.screen.data.editDatasets().info = {};
    std::strcpy(request.payload.screen.data.editDatasets().info.description, "UTF-8 dataset description");
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
    request.payload.screen.data.editDatasets().info.description[0] = 'X';
    UiRequest queued{};
    check(destination.receive(queued) && queued.request_id == first_id &&
          queued.payload.screen.generation == 42 &&
          !std::strcmp(queued.payload.screen.data.datasets().info.description, "UTF-8 dataset description"),
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
        testExclusiveUiPages();
        testScreenResourceLifetime();
        testUiActionOwnershipAndStaleUpdates();
        testRepeatedUiRoundTripsAndValueCopies();
        testStorageReplies();
        testMaps();
        testGps();
        testSettings();
        testSettingsLanguage();
        testDialogs();
        testUsbMode();
        testWifiMode();
        testExport();
        testPlace();
        testWordBookCache();
        testKeyBindings();
        testBoot();
        testDatasets();
        testDatasetBorrowing();
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

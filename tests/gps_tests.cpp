#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include "task/gps/GpsReceiver.h"
#include "task/gps/GpsTime.h"
#include "task/ui/UiModel.h"
#include "utils/SyncClient.h"

using namespace geoputer;

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::string sentence(const std::string& body) {
    unsigned checksum = 0;
    for (unsigned char c : body) checksum ^= c;
    char tail[8];
    std::snprintf(tail, sizeof(tail), "*%02X\r\n", checksum);
    return "$" + body + tail;
}

NmeaResult decode(NmeaParser& parser, const std::string& bytes, GpsFix& fix) {
    NmeaResult completed = NmeaResult::Incomplete;
    for (char byte : bytes) {
        const auto result = parser.feed(byte, fix);
        if (result != NmeaResult::Incomplete) completed = result;
    }
    return completed;
}

void feed(GpsReceiver& receiver, const std::string& body, uint32_t now) {
    for (char byte : sentence(body)) receiver.feed(byte, now);
}

void testNmeaCoordinatesAndDamage() {
    NmeaParser parser;
    GpsFix fix{};
    check(decode(parser, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n", fix) == NmeaResult::Position,
          "independently known RMC checksum is accepted");
    check(fix.valid && std::abs(fix.latitude - 48.1173) < 1e-9 &&
          std::abs(fix.longitude - 11.5166666667) < 1e-9 && fix.utc_ms == 45319000,
          "NMEA degrees/minutes become signed decimal degrees and UTC");
    check(decode(parser, sentence("GNGGA,123520.125,3450.000,S,05830.000,W,1,08,0.9,0,M,0,M,,"), fix) == NmeaResult::Position,
          "GN multi-constellation talker is accepted");
    check(fix.valid && fix.latitude < 0 && fix.longitude == -58.5 && fix.utc_ms == 45320125,
          "southern and western coordinates and fractional seconds");
    check(decode(parser, sentence("BDGGA,123521,0000.000,N,00000.000,E,1,08,1,0,M,0,M,,"), fix) == NmeaResult::Position && fix.valid,
          "a genuine fix at zero coordinates is valid");
    check(decode(parser, sentence("GNGGA,123522,,,,,0,00,99.99,,,,,,"), fix) == NmeaResult::Position && !fix.valid,
          "GGA quality zero clears fix without inventing coordinates");
    check(decode(parser, sentence("GNRMC,123522,V,,,,,,,141026,,,N"), fix) == NmeaResult::Position && !fix.valid,
          "RMC void status clears fix");
    check(decode(parser, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6B\r\n", fix) == NmeaResult::Rejected,
          "checksum damage is rejected");
    for (const auto& body : {
             "GNGGA,123520,9060.000,N,01131.000,E,1,08,1,0,M,0,M,,",
             "GNGGA,123520,4860.000,N,01131.000,E,1,08,1,0,M,0,M,,",
             "GNGGA,123520,4807.038,X,01131.000,E,1,08,1,0,M,0,M,,",
             "GNGGA,256199,4807.038,N,01131.000,E,1,08,1,0,M,0,M,,",
             "GNGGA,123520,nan,N,01131.000,E,1,08,1,0,M,0,M,,",
             "GNGGA,123520,4807.038,N,18100.000,E,1,08,1,0,M,0,M,,",
             "GNRMC,123520,A,4807.038,N"}) {
        check(decode(parser, sentence(body), fix) == NmeaResult::Rejected, "malformed position rejected");
    }
    check(decode(parser, "$GNGGA," + std::string(300, 'X') + "\n", fix) == NmeaResult::Rejected,
          "overlong frame cannot overflow fixed buffer");
    check(decode(parser, "$broken" + sentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,1,0,M,0,M,,"), fix) == NmeaResult::Position,
          "new dollar resynchronizes after partial/damaged frame");
}

void testEpochMergeAndStaleFix() {
    GpsReceiver receiver;
    receiver.begin(100);
    feed(receiver, "GNGGA,123519,4807.038,N,01131.000,E,1,08,1,0,M,0,M,,", 110);
    feed(receiver, "GNRMC,123519,A,4807.038,N,01131.000,E,0,0,141026,,,A", 130);
    receiver.poll(190);
    check(receiver.snapshot().fix.valid && receiver.snapshot().sample_seq == 1 && receiver.sampleChanged(),
          "GGA/RMC for one UTC epoch produce one complete sample");
    uint64_t epoch;
    check(nmeaEpochMs("141026",45319000,epoch) && receiver.snapshot().fix.utc_epoch_ms == epoch &&
          receiver.snapshot().fix.fields == (GpsTime|GpsAltitude|GpsSpeed|GpsCourse|GpsHdop|GpsSatellites) &&
          receiver.snapshot().fix.satellites == 8 && receiver.snapshot().fix.hdop == 1,
          "same-epoch GGA/RMC merge date, altitude, speed, course, HDOP and satellites");
    receiver.notificationSent();
    check(!receiver.changed(), "queued notification releases pending snapshot");
    feed(receiver, "GNRMC,123519,V,,,,,,,141026,,,N", 210);
    check(!receiver.snapshot().fix.valid, "late void sentence revokes same-epoch fix");
    feed(receiver, "GNGGA,123520,4807.038,N,01131.000,E,1,08,1,0,M,0,M,,", 1110);
    feed(receiver, "GNRMC,123520,V,,,,,,,141026,,,N", 1130);
    receiver.poll(1190);
    check(!receiver.snapshot().fix.valid && receiver.snapshot().sample_seq == 2,
          "contradictory same-epoch validity resolves to invalid");
    feed(receiver, "GNRMC,123521,A,4807.038,N,01131.000,E,0,0,141026,,,A", 2110);
    receiver.poll(2190);
    check(receiver.snapshot().fix.valid, "RMC-only receiver also yields coordinates");
    check(receiver.snapshot().fix.fields == (GpsTime|GpsSpeed|GpsCourse),
          "RMC-only epoch cannot inherit old GGA satellite, altitude or HDOP values");
    const auto old = receiver.snapshot();
    feed(receiver, "GPGSV,1,1,00", 5000);
    receiver.poll(5190);
    check(receiver.snapshot().reception == GpsReception::NoFix && !receiver.snapshot().fix.valid,
          "unrelated sentences do not keep an old position fresh");
    GpsSnapshot app{};
    check(applyGpsSnapshot(app, receiver.snapshot()), "accept latest GPS state");
    check(!applyGpsSnapshot(app, old) && !app.fix.valid,
          "queued old sample cannot restore stale position after status retrieval");
    receiver.poll(8000);
    check(receiver.snapshot().reception == GpsReception::NoData, "no valid NMEA for three seconds means NO GPS");
    receiver.begin(0);
    receiver.poll(3000);
    check(receiver.snapshot().reception == GpsReception::NoData, "unconnected GPS never blocks initialization");
}

void testClockRolloverAndUiIsolation() {
    GpsReceiver receiver;
    receiver.begin(UINT32_MAX - 1000);
    feed(receiver, "GNRMC,235959.900,A,4807.038,N,01131.000,E,0,0,141026,,,A", UINT32_MAX - 500);
    receiver.poll(UINT32_MAX - 400);
    feed(receiver, "GNRMC,000000.100,A,4807.038,N,01131.000,E,0,0,151026,,,A", 100);
    receiver.poll(180);
    check(receiver.snapshot().sample_seq == 2 && receiver.snapshot().fix.valid,
          "UTC midnight and monotonic uint32 rollover both work");
    feed(receiver, "GNRMC,235959.800,V,,,,,,,141026,,,N", 200);
    check(receiver.snapshot().fix.valid, "out-of-order previous-day sentence cannot revoke newer epoch");
    receiver.poll(3200);
    check(!receiver.snapshot().fix.valid, "stale timeout survives uint32 rollover");

    Queue<ReturnMessage, 1> results;
    Queue<GpsRequest, 1> gps_commands;
    Queue<AppNotification, 1> notifications;
    results.initialize(); gps_commands.initialize(); notifications.initialize();
    SyncClient client(results);
    GpsRequest request{};
    request.kind = MessageKind::GetGpsStatus;
    check(client.sendSync(TaskId::GPS, gps_commands, request, 0) == SyncSendResult::AwaitingResult,
          "GPS uses the same single outstanding sync contract");
    gps_commands.receive(request);
    AppNotification event{};
    event.kind = MessageKind::GpsSampleReady;
    event.source = TaskId::GPS;
    event.payload.gps = receiver.snapshot();
    notifications.send(event);
    ReturnMessage reply{};
    reply.kind = MessageKind::SyncReturn;
    reply.source = TaskId::GPS;
    reply.request_id = request.request_id;
    reply.payload.result = {request.kind, OperationStatus::Ok, {}, receiver.snapshot(), {}, {}};
    results.send(reply);
    SyncReturn returned{};
    check(client.receiveSyncResult(returned, 0) == SyncReceiveResult::Returned,
          "GPS return succeeds while notification queue is full");
    check(notifications.receive(event), "sample notification remains separate from sync return");
    UiModel ui;
    SetUiScreen screen{};
    screen.screen = ScreenId::LivePlace; screen.mode = DisplayMode::View; screen.generation = 1;
    ui.setScreen(screen);
    ui.keyPressed(UiKey::OpenSettings, 10);
    const auto action = ui.snapshot().action;
    UpdateUiContent update{};
    update.mode = DisplayMode::View; update.generation = 1; update.data.revision = 2;
    update.data.gps = returned.gps;
    check(ui.updateContent(update) == OperationStatus::Ok && ui.snapshot().action.action_id == action.action_id,
          "GPS display update preserves an unprocessed 2 action");
    update.data.gps.reception = GpsReception::Fix;
    check(ui.data().gps.reception == GpsReception::NoData, "UI GPS display uses an immutable value copy");
}
}  // namespace

void testGps() {
    testNmeaCoordinatesAndDamage();
    testEpochMergeAndStaleFix();
    testClockRolloverAndUiIsolation();
}

#pragma once
#include "UiTypes.h"
namespace geoputer {
inline unsigned gpsControlChoices(const UiData& data) {
    return data.recording.state == RecordingState::Stopped ? 1 : 2;
}
// Start is the sole action when stopped; otherwise Stop precedes Pause/Resume.
inline unsigned gpsControlDefault(RecordingState state) {
    return state == RecordingState::Stopped ? 0 : 1;
}
inline LogAction gpsControlAction(RecordingState state, unsigned choice) {
    if (state == RecordingState::Stopped) return LogAction::Start;
    if (choice == 0) return LogAction::Stop;
    return state == RecordingState::Paused ? LogAction::Resume : LogAction::Pause;
}
inline bool gpsControlAllowed(const UiData& data, LogAction action) {
    if(data.boot.maintenance || data.storage.log.closing || data.datasets.applying) return false;
    if(action == LogAction::Start) return data.storage.card_state == CardState::Mounted && !data.storage.log.open;
    if (action == LogAction::Stop) return data.recording.state != RecordingState::Stopped;
    return true;
}
inline const char* gpsControlLabel(LogAction action, bool ja) {
    switch(action) {
        case LogAction::Start:return ja ? "開始" : "Start";
        case LogAction::Stop:return ja ? "停止" : "Stop";
        case LogAction::Pause:return ja ? "一時停止" : "Pause";
        case LogAction::Resume:return ja ? "再開" : "Resume";
    }
    return "";
}
}

#pragma once
#include "Config.h"
#include "task/storage/log/LogReadTypes.h"
namespace geoputer::replay {
inline constexpr unsigned SpeedCount=sizeof(config::LogReplayIntervalsMs)/sizeof(*config::LogReplayIntervalsMs);
inline constexpr const char* SpeedLabels[]={"1/4x","1/2x","1x","2x","4x"};
static_assert(SpeedCount==sizeof(SpeedLabels)/sizeof(*SpeedLabels) && config::LogReplayDefaultSpeed<SpeedCount,"Replay speed labels/default match intervals");
inline unsigned speed(const LogBrowserView& view) {return view.replay_speed<SpeedCount?view.replay_speed:config::LogReplayDefaultSpeed;}
inline uint32_t intervalMs(const LogBrowserView& view) {return config::LogReplayIntervalsMs[speed(view)];}
inline const char* speedLabel(const LogBrowserView& view) {return SpeedLabels[speed(view)];}
inline bool canAdvance(const LogBrowserView& view) {
    return view.index<view.summary.records && (view.replay_reverse?view.index>0:uint64_t(view.index)+1<view.summary.records);
}
} // namespace geoputer::replay

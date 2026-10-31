#pragma once
#include <cstdint>
namespace geoputer {
enum class ScreenId : uint8_t { LivePlace, Settings, Boot, GpsLogger, Logs, LogPoint, UsbStorage, Map, Menu, LogProperties, LogStatistics, WifiManagement };
enum class DisplayMode : uint8_t { Background, View };
enum class LogAction : uint8_t { Start, Pause, Resume, Stop };
enum class ActionResult : uint8_t { None, Applied, Rejected };
enum class UiKey : uint8_t { OpenGpsLogger, OpenSettings, OpenLogs, DeleteLog, Escape, Up, Down, Left, Right, Enter, Other, Back, OpenLivePlace, OpenMap, ScreenOff, GpsControl, Tab, ZoomIn, FirstPoint, LastPoint, Playback, ZoomOut, Recalculate };
enum class UsbPage : uint8_t { Menu, WaitEject, Files, Format, Progress, Complete, Error, Busy };
struct BatteryStatus { int8_t percent = -1; bool charging = false; bool charge_known = false; };
}

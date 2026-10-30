#pragma once
#include <cstdint>
namespace geoputer {
enum class WifiPage : uint8_t {ConnectionQr, WebQr, Text};
enum class WifiError:uint8_t {None, RadioInit, Credentials, AccessPoint, SdRecovery, Restart};
inline const char* wifiErrorName(WifiError error) {
    switch(error) {
        case WifiError::None:return "";case WifiError::RadioInit:return "WIFI INIT FAILED";
        case WifiError::Credentials:return "PASSWORD NVS ERROR";case WifiError::AccessPoint:return "AP START FAILED";
        case WifiError::SdRecovery:return "SD RECOVERY ERROR";case WifiError::Restart:return "SETTINGS SAVE ERROR";
    }
    return "WIFI ERROR";
}
enum class WifiPhase : uint8_t {Starting, Ready, Receiving, Saving, Reading, Removing, Stopping, Failed};
struct WifiView {
    char ssid[32]{},password[17]{},url[24]{};
    uint32_t transferred=0,total=0;
    WifiPhase phase=WifiPhase::Starting;
    uint8_t clients=0;
    bool restart_ready=false,portal=false;
    WifiError error=WifiError::None;
};
inline const char* wifiPhaseName(WifiPhase phase) {
    switch(phase) {
        case WifiPhase::Starting:return "STARTING";case WifiPhase::Ready:return "READY";
        case WifiPhase::Receiving:return "RECEIVING";case WifiPhase::Saving:return "SAVING";
        case WifiPhase::Reading:return "READING";case WifiPhase::Removing:return "REMOVING";
        case WifiPhase::Stopping:return "STOPPING";case WifiPhase::Failed:return "ERROR";
    }
    return "ERROR";
}
}

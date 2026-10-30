#include "BootMode.h"
#include <Preferences.h>

namespace geoputer {
bool loadBootMode(BootMode& mode) {
    mode = BootMode::GpsLogger;
    Preferences nvs;
    if (!nvs.begin("geoputer", false)) return false;
    if (!nvs.isKey("boot_mode")) return true;
    uint8_t record[4]{};
    if (nvs.getBytesLength("boot_mode") != sizeof(record) ||
        nvs.getBytes("boot_mode", record, sizeof(record)) != sizeof(record) ||
        record[0] != 'M' || record[1] != 1 || record[2] > 2 || record[3] != (record[2] ^ 0xa5)) return false;
    mode = static_cast<BootMode>(record[2]);
    return true;
}
bool saveBootMode(BootMode mode) {
    if (mode != BootMode::GpsLogger && mode != BootMode::UsbStorage && mode != BootMode::WifiManagement) return false;
    Preferences nvs;
    if (!nvs.begin("geoputer", false)) return false;
    const auto value = static_cast<uint8_t>(mode);
    const uint8_t record[] = {'M', 1, value, static_cast<uint8_t>(value ^ 0xa5)};
    return nvs.putBytes("boot_mode", record, sizeof(record)) == sizeof(record);
}
}

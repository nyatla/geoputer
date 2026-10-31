#include "UsbStorageTask.h"
#include "../../hardware/CardSpi.h"
namespace geoputer {
void UsbStorageTask::initializeStorage(){initializeCardSpi(spi_);SettingsStore store;Settings settings=defaultSettings();if(store.begin())store.load(settings);state_.settings={settings.language,static_cast<uint8_t>(Brightnesses[settings.brightness])};usb_.begin(spi_);state_.usb=usb_.snapshot();state_.revision=1;initialized_=dirty_=true;}
}

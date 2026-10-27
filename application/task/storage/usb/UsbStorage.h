#pragma once
#include <SPI.h>
#include <USBMSC.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <atomic>
#include "UsbStorageTypes.h"

namespace geoputer {
// Exclusive ownership: PC uses raw sectors, or Storage mounts FatFs locally.
class UsbStorage {
public:
    // Register the mode-specific descriptor before starting USB or app tasks.
    static USBMSC& registerInterface();
    bool begin(SPIClass& spi);
    UsbStorageView snapshot() const;
    struct Activity { uint32_t accesses; bool active; };
    Activity takeActivity() { return {accesses_.exchange(0),io_active_.load()}; }
    bool takeLocal();
    bool publish();
    bool local() const { return local_; }
    uint8_t drive() const { return drive_; }
    bool exitToLogger();
private:
    static int32_t read(uint32_t lba, uint32_t offset, void* data, uint32_t size);
    static int32_t write(uint32_t lba, uint32_t offset, uint8_t* data, uint32_t size);
    static bool startStop(uint8_t power, bool start, bool eject);
    int32_t transfer(uint32_t lba, uint32_t offset, void* data, uint32_t size, bool writing);
    template<class Operation> bool sectorIo(Operation operation) {
        ++accesses_;io_active_=true;
        struct End { std::atomic<bool>& active; ~End() {active=false;} } end{io_active_};
        return operation();
    }
    std::atomic<uint32_t> accesses_{0};
    std::atomic<bool> io_active_{false};
    inline static UsbStorage* active_ = nullptr;
    USBMSC* msc_ = nullptr;
    SPIClass* spi_ = nullptr;
    StaticSemaphore_t mutex_control_{};
    SemaphoreHandle_t mutex_ = nullptr;
    uint8_t drive_ = 0xff;
    uint32_t sectors_ = 0;
    uint8_t scratch_[512]{};
    bool stopping_ = false;
    bool mounted_ = false;
    std::atomic<bool> ready_{false}, ejected_{false}, error_{false}, local_{false};
};
}

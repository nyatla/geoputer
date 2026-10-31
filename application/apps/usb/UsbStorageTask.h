#pragma once
#include "UsbMessages.h"
#include "../../utils/Task.h"
#include "../../task/storage/SettingsStore.h"
#include "../../task/storage/usb/UsbStorage.h"
#include "../../task/storage/log/LogReader.h"
#include "../../task/storage/export/LogExporter.h"
#include <SPI.h>
#include <memory>
#include <variant>
namespace geoputer {
struct UsbExportWork {std::unique_ptr<LogExporter> exporter;bool started=false;uint32_t progress_at=0;};
class UsbStorageTask final:public Task<UsbChannels::StorageRequest,1>,private StorageObserver {
public:
 UsbStorageTask(UsbChannels::Events& events,UsbChannels::Results& results):events_(events),results_(results),work_(std::in_place_type<LogReader>,config::LogDirectory,static_cast<StorageObserver*>(this),&file_){}
 const UsbState& state()const{return state_;}
protected:void run()override;
private:
 enum class Work:uint8_t {None,Prepare,Convert,PublishMenu,PublishResult};
 void initializeStorage();
 void handle(const UsbChannels::StorageRequest&);bool exportCommand(const UsbCommand&);void stepExport();void step();void notify();
 void sdBegin()override;void sdEnd()override;void cacheState(RgCacheState,bool=false)override{}
 LogReader& logs(){return std::get<LogReader>(work_);}UsbExportWork& converting(){return std::get<UsbExportWork>(work_);}
 UsbChannels::Events& events_;UsbChannels::Results& results_;UsbStorage usb_;SPIClass spi_{FSPI};
 UsbState state_{};UsbChannels::Returned reply_{};
 // Observer state outlives the readers/exporter which call sdBegin/sdEnd on close.
 Work work_kind_=Work::None;bool reading_=false,initialized_=false,reply_pending_=false,dirty_=false;unsigned sd_depth_=0;
 ReadFileSlot file_;std::variant<LogReader,UsbExportWork> work_;
};
}

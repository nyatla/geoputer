#include "WifiStorageTask.h"
#include "../../hardware/CardSpi.h"
#include "../../task/storage/StorageCard.h"
#include "../../task/storage/StorageFs.h"
#include <SD.h>
namespace geoputer {
void WifiStorageTask::sdBegin(){if(!sd_depth_++){state_.sd.active=true;++state_.sd.access_id;++state_.sd.revision;dirty_=true;}}
void WifiStorageTask::sdEnd(){if(sd_depth_ && !--sd_depth_){state_.sd.active=false;++state_.sd.revision;dirty_=true;}}
void WifiStorageTask::changed(void* p){auto& self=*static_cast<WifiStorageTask*>(p);self.controls();self.state_.wifi=self.service_.view();++self.state_.revision;self.dirty_=true;self.notify();}
void WifiStorageTask::notify(){if(!dirty_)return;WifiChannels::Event event{};event.source=TaskId::Storage;event.kind=MessageKind::WifiStorageChanged;event.storage=state_;if(sendSpeculative(events_,event)==SpeculativeResult::Queued)dirty_=false;}
void WifiStorageTask::controls(){WifiChannels::StorageRequest request{};if(inbox().receive(request)){if(request.source!=TaskId::Application)return;if(request.command.action==WifiActionKind::Exit)service_.requestExit();else if(request.command.action==WifiActionKind::Regenerate)service_.requestRegenerate();}}
void WifiStorageTask::handle(const WifiChannels::StorageRequest& request){
 if(request.source!=TaskId::Application || !request.request_id)return;
 WifiChannels::reply(reply_,TaskId::Storage,request,OperationStatus::InvalidRequest);
 if(request.kind==MessageKind::InitializeStorage && !initialized_){
  initializeCardSpi(spi_);WifiDeviceInfo info{};info.card_state=CardState::NoCard;info.settings=defaultSettings();if(settings_.begin())settings_.load(info.settings);
  state_.settings={info.settings.language,static_cast<uint8_t>(Brightnesses[info.settings.brightness])};
  {SdAccessScope access(this);drive_=initStorageCard(12,&spi_,config::SdSpiHz);if(drive_!=0xff){
   const bool mounted=mountStorageCard(drive_,"/sd",config::SdMaxOpenFiles,false);info.card_state=mounted?CardState::Unsupported:CardState::NoCard;
   char volume[]={static_cast<char>('0'+drive_),':',0};DWORD free=0;FATFS* fs=nullptr;
   if(mounted && f_getfree(volume,&free,&fs)==FR_OK && fs){info.total_bytes=uint64_t(fs->n_fatent-2)*fs->csize*512;info.free_bytes=uint64_t(free)*fs->csize*512;info.card_state=fs->fs_type==FS_FAT32?CardState::Mounted:CardState::Unsupported;}
  }}
  state_.card_state=info.card_state;state_.revision=1;service_.setStatus(info);initialized_=true;
  const bool ok=service_.begin(drive_,info.card_state==CardState::Mounted,this,changed,this);state_.wifi=service_.view();reply_.payload.result.status=ok?OperationStatus::Ok:OperationStatus::Unavailable;dirty_=true;
 }
 reply_.payload.result.storage=state_;reply_pending_=true;
}
void WifiStorageTask::step(){if(reply_pending_&&results_.send(reply_))reply_pending_=false;if(initialized_){controls();service_.step();state_.wifi=service_.view();notify();}else if(!reply_pending_){WifiChannels::StorageRequest request{};if(inbox().receive(request))handle(request);}}
void WifiStorageTask::run(){for(;;){step();vTaskDelay(pdMS_TO_TICKS(5));}}
}

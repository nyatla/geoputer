#include "UsbStorageTask.h"
#include <cstring>
#include <algorithm>
namespace geoputer {
void UsbStorageTask::sdBegin(){if(!sd_depth_++){state_.sd.active=true;++state_.sd.access_id;++state_.sd.revision;dirty_=true;}}
void UsbStorageTask::sdEnd(){if(sd_depth_&&! --sd_depth_){state_.sd.active=false;++state_.sd.revision;dirty_=true;}}
void UsbStorageTask::notify(){if(!dirty_)return;UsbChannels::Event event{};event.source=TaskId::Storage;event.kind=MessageKind::UsbStorageChanged;event.storage=state_;if(sendSpeculative(events_,event)==SpeculativeResult::Queued)dirty_=false;}
bool UsbStorageTask::exportCommand(const UsbCommand& command){
 auto& job=state_.export_job;if(!initialized_)return false;
 if(command.action!=UsbActionKind::Publish&&(reading_||work_kind_!=Work::None))return false;
 if(command.action==UsbActionKind::Begin){if(job.phase!=ExportPhase::Idle||!usb_.snapshot().ready)return false;job={};job.phase=usbHostNeedsEject(usb_.snapshot())?ExportPhase::WaitEject:ExportPhase::Preparing;work_kind_=Work::Prepare;}
 else if(command.action==UsbActionKind::Start){if(job.phase!=ExportPhase::Selecting||!usb_.local()||!validLogName(command.name)||!validExportFormat(command.format))return false;if(!logs().releaseForReuse())return false;work_.emplace<UsbExportWork>();job={};job.phase=ExportPhase::Running;job.format=command.format;std::memcpy(job.name,command.name,sizeof(job.name));work_kind_=Work::Convert;}
 else if(command.action==UsbActionKind::Publish){if(reading_){logs().cancel();reading_=false;state_.files.loading=false;}if(job.phase==ExportPhase::Running||job.phase==ExportPhase::Publishing||job.phase==ExportPhase::Preparing)return false;if(job.phase==ExportPhase::WaitEject&&!usb_.local()){job={};work_kind_=Work::None;}else{work_kind_=Work::PublishMenu;job.phase=ExportPhase::Publishing;}}
 else return false;
 state_.busy=work_kind_!=Work::None&&job.phase!=ExportPhase::WaitEject;++state_.revision;dirty_=true;return true;
}
void UsbStorageTask::handle(const UsbChannels::StorageRequest& request){
 if(request.source!=TaskId::Application||!request.request_id)return;
 UsbChannels::reply(reply_,TaskId::Storage,request,OperationStatus::InvalidRequest);
 auto& result=reply_.payload.result;const auto& c=request.command;
 if(request.kind==MessageKind::InitializeStorage&&!initialized_){initializeStorage();result.status=OperationStatus::Ok;}
 else if(c.action==UsbActionKind::Exit&&!state_.busy&&state_.export_job.phase==ExportPhase::Idle){if(usb_.exitToLogger()){state_.restart_ready=true;result.status=OperationStatus::Ok;}else result.status=OperationStatus::Unavailable;++state_.revision;dirty_=true;}
 else if(c.action==UsbActionKind::List&&initialized_&&usb_.local()&&!state_.busy&&state_.export_job.phase==ExportPhase::Selecting&&std::holds_alternative<LogReader>(work_)){
  LogReadRequest read{};read.kind=LogReadKind::List;read.count=4;read.token=request.request_id;read.direction=c.direction;std::memcpy(read.anchor,c.name,sizeof(read.anchor));
  if(logs().begin(read)){state_.files.loading=state_.busy=reading_=true;state_.files.error=LogReadError::None;result.status=OperationStatus::Ok;++state_.revision;dirty_=true;}
 }else if(exportCommand(c))result.status=OperationStatus::Ok;
 result.storage=state_;reply_pending_=true;
}
void UsbStorageTask::stepExport(){
 auto& job=state_.export_job;if(work_kind_==Work::None)return;
 if(work_kind_==Work::Prepare){if(usbHostNeedsEject(usb_.snapshot()))return;job.phase=ExportPhase::Preparing;if(usb_.takeLocal()){logs().setDrive(usb_.drive());state_.card_state=CardState::Mounted;job.phase=ExportPhase::Selecting;work_kind_=Work::None;}else{job.error=ExportError::Media;job.phase=ExportPhase::Publishing;work_kind_=Work::PublishResult;}state_.busy=work_kind_!=Work::None;++state_.revision;dirty_=true;return;}
 if(work_kind_==Work::Convert){auto& work=converting();
  if(!work.exporter){try{work.exporter=std::make_unique<LogExporter>();work.exporter->setDrive(usb_.drive());}catch(const std::bad_alloc&){job.error=ExportError::Memory;job.phase=ExportPhase::Publishing;work_kind_=Work::PublishResult;++state_.revision;dirty_=true;return;}}
  if(!work.started){work.started=work.exporter->begin(job.name,job.format);if(!work.started){job.error=ExportError::Close;job.phase=ExportPhase::Failed;work_kind_=Work::None;}}else work.exporter->step();
  const auto& progress=work.exporter->snapshot();const bool complete=work.started&&!work.exporter->running();const auto now=taskTimeMs();
  if(!work.started||complete||job.total!=progress.total||now-work.progress_at>=config::ExportProgressIntervalMs){if(work.started)job=progress;work.progress_at=now;if(complete){job.phase=ExportPhase::Publishing;work_kind_=Work::PublishResult;}++state_.revision;dirty_=true;}return;
 }
 const bool menu=work_kind_==Work::PublishMenu;bool closed=true;
 if(auto* logs=std::get_if<LogReader>(&work_))closed=logs->releaseForReuse();
 if(auto* work=std::get_if<UsbExportWork>(&work_))if(work->exporter)closed=work->exporter->closeFiles();
 if(!closed){job.phase=ExportPhase::Failed;job.error=ExportError::Close;}
 else {
  if(!std::holds_alternative<LogReader>(work_))work_.emplace<LogReader>(config::LogDirectory,static_cast<StorageObserver*>(this),&file_);
  if((!menu||usb_.local())&&!usb_.publish()){job.phase=ExportPhase::Failed;job.error=ExportError::Publish;}else if(menu)job={};else job.phase=job.error==ExportError::None?ExportPhase::Complete:ExportPhase::Failed;
 }
 state_.card_state=usb_.local()?CardState::Mounted:CardState::Unmounted;work_kind_=Work::None;state_.busy=false;++state_.revision;dirty_=true;
}
void UsbStorageTask::step(){
 if(reply_pending_&&results_.send(reply_))reply_pending_=false;
 if(!reply_pending_){UsbChannels::StorageRequest request{};if(inbox().receive(request))handle(request);}
 if(!reply_pending_&&initialized_){
  if(reading_){logs().step();if(logs().done()){const auto& reply=logs().result();auto& f=state_.files;const auto revision=f.revision+1;f={};f.count=reply.count;f.error=reply.error;f.has_newer=reply.has_newer;f.has_older=reply.has_older;std::memcpy(f.names,reply.data.list.names,sizeof(f.names));f.revision=revision;f.loading=state_.busy=reading_=false;++state_.revision;dirty_=true;}}
  else stepExport();
  const auto io=usb_.takeActivity();const bool active=io.active||sd_depth_;if(io.accesses||active!=state_.sd.active){state_.sd.access_id+=io.accesses;state_.sd.active=active;++state_.sd.revision;dirty_=true;}
  const auto view=usb_.snapshot();if(!(view==state_.usb)){state_.usb=view;++state_.revision;dirty_=true;}
 }
 notify();
}
void UsbStorageTask::run(){for(;;){step();vTaskDelay(pdMS_TO_TICKS(5));}}
}

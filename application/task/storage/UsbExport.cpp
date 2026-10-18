#include "StorageTask.h"
#include "../../utils/SyncClient.h"
#include <cstring>

namespace geoputer {
void StorageTask::handleUsbExport(const StorageRequest& request, SyncReturn& result) {
    if(!usb_mode_ || !initialized_ || has_job_)return;
    const auto& command=request.payload.export_request;
    auto& job=state_.export_job;
    if(command.operation==ExportOperation::Begin) {
        if(usb_work_!=UsbWork::None || job.phase!=ExportPhase::Idle || !usb_.snapshot().ready || has_read_)return;
        job={};job.phase=usbHostNeedsEject(usb_.snapshot())?ExportPhase::WaitEject:ExportPhase::Preparing;
        usb_work_=UsbWork::Prepare;
    } else if(command.operation==ExportOperation::Start) {
        if(usb_work_!=UsbWork::None || job.phase!=ExportPhase::Selecting || !usb_.local() || has_read_ ||
           has_completion_ || !validLogName(command.name) || !validExportFormat(command.format))return;
        job={};job.phase=ExportPhase::Running;job.format=command.format;
        std::memcpy(job.name,command.name,sizeof(job.name));
        usb_work_=UsbWork::Convert;export_started_=false;export_progress_at_=0;
    } else if(command.operation==ExportOperation::Publish) {
        if(job.phase==ExportPhase::Running || job.phase==ExportPhase::Publishing || job.phase==ExportPhase::Preparing)return;
        if(job.phase==ExportPhase::WaitEject && !usb_.local()) {
            usb_work_=UsbWork::None;job={};
        } else {
            usb_work_=UsbWork::PublishMenu;job.phase=ExportPhase::Publishing;
        }
    } else return;
    state_.operation=StorageOperation::None;
    state_.busy=usb_work_!=UsbWork::None && job.phase!=ExportPhase::WaitEject;
    result.status=OperationStatus::Ok;++state_.revision;usb_dirty_=true;
}
void StorageTask::stepUsbExport() {
    auto& job=state_.export_job;
    if(usb_work_==UsbWork::None)return;
    if(usb_work_==UsbWork::Prepare) {
        if(usbHostNeedsEject(usb_.snapshot()))return;
        job.phase=ExportPhase::Preparing;
        if(usb_.takeLocal()) {
            log_reader_.setDrive(usb_.drive());exporter_.setDrive(usb_.drive());
            state_.card_state=CardState::Mounted;state_.fat_type=32;
            job.phase=ExportPhase::Selecting;usb_work_=UsbWork::None;
        } else if(usbHostNeedsEject(usb_.snapshot()))job.phase=ExportPhase::WaitEject;
        else {job.error=ExportError::Media;job.phase=ExportPhase::Publishing;usb_work_=UsbWork::PublishResult;}
        state_.busy=usb_work_!=UsbWork::None && job.phase!=ExportPhase::WaitEject;
        ++state_.revision;usb_dirty_=true;return;
    }
    if(usb_work_==UsbWork::Convert) {
        if(!export_started_) {
            export_started_=exporter_.begin(job.name,job.format);
            if(!export_started_) {job.error=ExportError::Close;job.phase=ExportPhase::Failed;usb_work_=UsbWork::None;}
        } else exporter_.step();
        const auto& progress=exporter_.snapshot();
        const bool complete=export_started_ && !exporter_.running();
        const auto now=taskTimeMs();
        if(!export_started_ || complete || job.total!=progress.total || now-export_progress_at_>=config::ExportProgressIntervalMs) {
            if(export_started_)job=progress;
            export_progress_at_=now;
            if(complete) {job.phase=ExportPhase::Publishing;usb_work_=UsbWork::PublishResult;}
            ++state_.revision;usb_dirty_=true;
        }
        return;
    }
    const bool menu=usb_work_==UsbWork::PublishMenu;
    log_reader_.cancel();
    if(!exporter_.closeFiles()) {job.phase=ExportPhase::Failed;job.error=ExportError::Close;}
    else if((!menu || usb_.local()) && !usb_.publish()) {job.phase=ExportPhase::Failed;job.error=ExportError::Publish;}
    else if(menu)job={};
    else job.phase=job.error==ExportError::None?ExportPhase::Complete:ExportPhase::Failed;
    state_.card_state=usb_.local()?CardState::Mounted:CardState::Unmounted;
    if(!usb_.local())state_.fat_type=0;
    usb_work_=UsbWork::None;state_.busy=false;++state_.revision;usb_dirty_=true;
}
}

#include "StorageTask.h"
#include "../../utils/SyncClient.h"
#include <cstring>
#include <new>

namespace geoputer {
void StorageTask::handleUsbExport(const StorageRequest& request, SyncReturn& result) {
    if(!usb_mode_ || !initialized_ || hasJob())return;
    const auto& command=request.payload.export_request;
    auto& job=state_.export_job;
    if(command.operation==ExportOperation::Begin) {
        if(usbMode().usb_work_!=UsbWork::None || job.phase!=ExportPhase::Idle || !usbMode().usb_.snapshot().ready || has_read_)return;
        job={};job.phase=usbHostNeedsEject(usbMode().usb_.snapshot())?ExportPhase::WaitEject:ExportPhase::Preparing;
        usbMode().usb_work_=UsbWork::Prepare;
    } else if(command.operation==ExportOperation::Start) {
        if(usbMode().usb_work_!=UsbWork::None || job.phase!=ExportPhase::Selecting || !usbMode().usb_.local() || has_read_ ||
           replies_.completionPending() || !validLogName(command.name) || !validExportFormat(command.format))return;
        job={};job.phase=ExportPhase::Running;job.format=command.format;
        std::memcpy(job.name,command.name,sizeof(job.name));
        usbMode().usb_work_=UsbWork::Convert;usbMode().export_started_=false;usbMode().export_progress_at_=0;
    } else if(command.operation==ExportOperation::Publish) {
        if(job.phase==ExportPhase::Running || job.phase==ExportPhase::Publishing || job.phase==ExportPhase::Preparing)return;
        if(job.phase==ExportPhase::WaitEject && !usbMode().usb_.local()) {
            usbMode().usb_work_=UsbWork::None;job={};
        } else {
            usbMode().usb_work_=UsbWork::PublishMenu;job.phase=ExportPhase::Publishing;
        }
    } else return;
    state_.operation=StorageOperation::None;
    state_.busy=usbMode().usb_work_!=UsbWork::None && job.phase!=ExportPhase::WaitEject;
    result.status=OperationStatus::Ok;++state_.revision;usb_dirty_=true;
}
void StorageTask::stepUsbExport() {
    auto& job=state_.export_job;
    if(usbMode().usb_work_==UsbWork::None)return;
    if(usbMode().usb_work_==UsbWork::Prepare) {
        if(usbHostNeedsEject(usbMode().usb_.snapshot()))return;
        job.phase=ExportPhase::Preparing;
        if(usbMode().usb_.takeLocal()) {
            work().setDrive(usbMode().usb_.drive());
            state_.card_state=CardState::Mounted;state_.fat_type=32;
            job.phase=ExportPhase::Selecting;usbMode().usb_work_=UsbWork::None;
        } else if(usbHostNeedsEject(usbMode().usb_.snapshot()))job.phase=ExportPhase::WaitEject;
        else {job.error=ExportError::Media;job.phase=ExportPhase::Publishing;usbMode().usb_work_=UsbWork::PublishResult;}
        state_.busy=usbMode().usb_work_!=UsbWork::None && job.phase!=ExportPhase::WaitEject;
        ++state_.revision;usb_dirty_=true;return;
    }
    if(usbMode().usb_work_==UsbWork::Convert) {
        if(auto* logs=work().activeLogs())logs->releaseForReuse();
        if(!usbMode().exporter_) {
            try {
                usbMode().exporter_=std::make_unique<LogExporter>();
                usbMode().exporter_->setDrive(usbMode().usb_.drive());
            } catch(const std::bad_alloc&) {
                job.error=ExportError::Memory;job.phase=ExportPhase::Publishing;
                usbMode().usb_work_=UsbWork::PublishResult;++state_.revision;usb_dirty_=true;return;
            }
        }
        if(!usbMode().export_started_) {
            usbMode().export_started_=usbMode().exporter_->begin(job.name,job.format);
            if(!usbMode().export_started_) {job.error=ExportError::Close;job.phase=ExportPhase::Failed;usbMode().usb_work_=UsbWork::None;}
        } else usbMode().exporter_->step();
        const auto& progress=usbMode().exporter_->snapshot();
        const bool complete=usbMode().export_started_ && !usbMode().exporter_->running();
        const auto now=taskTimeMs();
        if(!usbMode().export_started_ || complete || job.total!=progress.total || now-usbMode().export_progress_at_>=config::ExportProgressIntervalMs) {
            if(usbMode().export_started_)job=progress;
            usbMode().export_progress_at_=now;
            if(complete) {job.phase=ExportPhase::Publishing;usbMode().usb_work_=UsbWork::PublishResult;}
            ++state_.revision;usb_dirty_=true;
        }
        return;
    }
    const bool menu=usbMode().usb_work_==UsbWork::PublishMenu;
    work().logs().cancel();
    if(usbMode().exporter_ && !usbMode().exporter_->closeFiles()) {
        // Retain the FILs after close failure so Publish can retry safely.
        job.phase=ExportPhase::Failed;job.error=ExportError::Close;
    } else {
        usbMode().exporter_.reset();
        if((!menu || usbMode().usb_.local()) && !usbMode().usb_.publish()) {job.phase=ExportPhase::Failed;job.error=ExportError::Publish;}
        else if(menu)job={};
        else job.phase=job.error==ExportError::None?ExportPhase::Complete:ExportPhase::Failed;
    }
    state_.card_state=usbMode().usb_.local()?CardState::Mounted:CardState::Unmounted;
    if(!usbMode().usb_.local())state_.fat_type=0;
    usbMode().usb_work_=UsbWork::None;state_.busy=false;++state_.revision;usb_dirty_=true;
}
}

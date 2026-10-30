// Compile the real USB export state machine with host hardware adapters.
// -fno-access-control permits observing ownership without a firmware test API.
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <stdexcept>
#include "task/storage/StorageTask.h"
using namespace geoputer;
namespace {
bool fail_allocation=false, fail_close=false, fail_publish=false;
unsigned publications=0;
StorageTask* current=nullptr;
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
void command(StorageTask& task,ExportOperation op) {
    StorageRequest request{};request.payload.export_request.operation=op;
    request.payload.export_request.format=ExportFormat::Gpx;
    std::strcpy(request.payload.export_request.name,"lifecycle.gps.dat");
    SyncReturn result{};result.status=OperationStatus::InvalidRequest;
    task.handleUsbExport(request,result);
    check(result.status==OperationStatus::Ok,"USB export command accepted");
}
void select(StorageTask& task) {
    command(task,ExportOperation::Begin);task.stepUsbExport();
    check(task.state_.export_job.phase==ExportPhase::Selecting && !task.usbMode().exporter_,"selection does not allocate a converter");
}
}
void* operator new(std::size_t bytes) {
    if(fail_allocation && bytes==sizeof(LogExporter)) {fail_allocation=false;throw std::bad_alloc();}
    if(auto* data=std::malloc(bytes))return data;
    throw std::bad_alloc();
}
void operator delete(void* data) noexcept {std::free(data);}
void operator delete(void* data,std::size_t) noexcept {std::free(data);}
extern "C" int __real_fclose(FILE*);
extern "C" int __wrap_fclose(FILE* file) {
    const auto result=__real_fclose(file);
    if(fail_close) {fail_close=false;return EOF;}
    return result;
}
namespace geoputer {
// No RTOS loop or other jobs run in this focused state-machine test.
void StorageTask::run() {}
void GpsTask::run() {}
void StorageTask::sdBegin() {}
void StorageTask::sdEnd() {}
void StorageTask::cacheState(RgCacheState,bool) {}
UsbStorageView UsbStorage::snapshot() const {UsbStorageView view{};view.ready=true;view.local=local_;return view;}
bool UsbStorage::takeLocal() {local_=true;return true;}
bool UsbStorage::publish() {
    check(current && !current->usbMode().exporter_,"converter is released before USB publication");
    ++publications;
    if(fail_publish) {fail_publish=false;return false;}
    local_=false;return true;
}
}
int main() {
    Queue<AppNotification,8> notifications;Queue<ReturnMessage,1> results;
    ApplicationModeMemory memory;StorageTask task(notifications,results,memory);current=&task;
    check(!task.mode_resources_.selected(),"Storage starts without constructing any mode");
    check(task.setUsbMode(true),"select USB resources before startup");check(!task.usbMode().exporter_,"USB selection has no converter yet");task.initialized_=true;
    select(task);command(task,ExportOperation::Start);
    fail_allocation=true;task.stepUsbExport();
    check(!fail_allocation && !task.usbMode().exporter_ && task.state_.export_job.error==ExportError::Memory,"allocation failure is reported without retaining memory");
    task.stepUsbExport();
    check(publications==1 && task.state_.export_job.phase==ExportPhase::Failed,"allocation failure republishes USB and preserves its error");
    command(task,ExportOperation::Publish);task.stepUsbExport();
    select(task);command(task,ExportOperation::Start);task.stepUsbExport();
    check(task.usbMode().exporter_ && task.usbMode().exporter_->running(),"accepted conversion allocates a workspace lazily");
    // Use a real tiny binary log in a temporary directory for complete conversion.
    char pattern[]="/tmp/reversegeoputer-export-lifecycle-XXXXXX";
    const auto* directory=mkdtemp(pattern);check(directory,"create fixture directory");
    const std::filesystem::path root=directory;
    std::filesystem::create_directory(root/"logs");
    uint8_t header[512],record[32];encodeLogHeader(header,1,0);
    GpsFix fix{35.681641,140.027194,0,true};check(encodeLogRecord(fix,record),"encode fixture fix");
    {std::ofstream out(root/"logs/lifecycle.gps.dat",std::ios::binary);
     out.write(reinterpret_cast<char*>(header),sizeof(header));out.write(reinterpret_cast<char*>(record),sizeof(record));}
    // Restart before the first I/O so begin() constructs paths under the fixture root.
    task.usbMode().exporter_->root_=root.string();task.usbMode().exporter_->state_={};task.usbMode().export_started_=false;
    for(unsigned i=0;i<20 && task.usbMode().usb_work_!=StorageTask::UsbWork::None;++i)task.stepUsbExport();
    check(!task.usbMode().exporter_ && task.state_.export_job.phase==ExportPhase::Complete && task.state_.export_job.output_created &&
          task.state_.export_job.processed==1 && publications==2,"conversion result survives converter destruction and USB republication");
    check(std::filesystem::exists(root/"export/lifecycle.gps.dat.gpx"),"completed GPX exists");
    command(task,ExportOperation::Publish);task.stepUsbExport();
    select(task);command(task,ExportOperation::Start);task.stepUsbExport();
    // Model the pending handles of a failed conversion. Host fclose reports an
    // injected failure; the owner must retain the converter and withhold USB.
    task.usbMode().exporter_->input_.file_=std::tmpfile();check(task.usbMode().exporter_->input_.file_,"open pending handle");
    task.usbMode().exporter_->input_.opened_=true;
    task.state_.export_job.phase=ExportPhase::Failed;task.usbMode().usb_work_=StorageTask::UsbWork::None;
    command(task,ExportOperation::Publish);fail_close=true;task.stepUsbExport();
    check(!fail_close && task.usbMode().exporter_ && task.state_.export_job.error==ExportError::Close && publications==2 && task.usbMode().usb_.local(),
          "close failure retains the converter and local SD ownership");
    command(task,ExportOperation::Publish);fail_publish=true;task.stepUsbExport();
    check(!task.usbMode().exporter_ && task.state_.export_job.error==ExportError::Publish && publications==3,"closed workspace is freed even if republication fails");
    command(task,ExportOperation::Publish);task.stepUsbExport();
    check(!task.usbMode().exporter_ && task.state_.export_job.phase==ExportPhase::Idle && publications==4,"republication retry returns to idle without reallocating");
    std::filesystem::remove_all(root);
    std::puts("PASS: USB export lazy allocation, allocation failure, completed result, close retention and republication retry");
}

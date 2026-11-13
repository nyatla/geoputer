// Compile the real USB export state machine with host hardware adapters.
// -fno-access-control permits observing ownership without a firmware test API.
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <stdexcept>
#include "apps/usb/UsbStorageTask.h"
using namespace geoputer;
namespace {
bool fail_allocation=false, fail_close=false, fail_publish=false;
unsigned publications=0;
UsbStorageTask* current=nullptr;
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
void command(UsbStorageTask& task,UsbActionKind op) {
    UsbCommand request{};request.action=op;
    request.format=ExportFormat::Gpx;std::strcpy(request.name,"lifecycle.gps.dat");
    const bool accepted=task.exportCommand(request);
    check(accepted,"USB export command accepted");
}
void select(UsbStorageTask& task) {
    command(task,UsbActionKind::Begin);task.stepExport();
    check(task.state_.export_job.phase==ExportPhase::Selecting && (!std::get_if<UsbExportWork>(&task.work_) || !task.converting().exporter),"selection does not allocate a converter");
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
// Hardware setup is outside this focused state-machine test.
void UsbStorageTask::initializeStorage(){}
bool UsbStorage::exitToLogger(){return false;}
UsbStorageView UsbStorage::snapshot() const {UsbStorageView view{};view.ready=true;view.local=local_;return view;}
bool UsbStorage::takeLocal() {local_=true;return true;}
bool UsbStorage::publish() {
    check(current && !std::get_if<UsbExportWork>(&current->work_),"converter is released before USB publication");
    ++publications;
    if(fail_publish) {fail_publish=false;return false;}
    local_=false;return true;
}
}
int main() {
    UsbChannels::Events notifications;UsbChannels::Results results;
    UsbStorageTask task(notifications,results);current=&task;task.initialized_=true;
    select(task);command(task,UsbActionKind::Start);
    fail_allocation=true;task.stepExport();
    check(!fail_allocation && (!std::get_if<UsbExportWork>(&task.work_) || !task.converting().exporter) && task.state_.export_job.error==ExportError::Memory,"allocation failure is reported without retaining memory");
    task.stepExport();
    check(publications==1 && task.state_.export_job.phase==ExportPhase::Failed,"allocation failure republishes USB and preserves its error");
    command(task,UsbActionKind::Publish);task.stepExport();
    select(task);command(task,UsbActionKind::Start);task.stepExport();
    check(task.converting().exporter && task.converting().exporter->running(),"accepted conversion allocates a workspace lazily");
    // Use a real tiny binary log in a temporary directory for complete conversion.
    char pattern[]="/tmp/geoputer-export-lifecycle-XXXXXX";
    const auto* directory=mkdtemp(pattern);check(directory,"create fixture directory");
    const std::filesystem::path root=directory;
    std::filesystem::create_directory(root/"logs");
    uint8_t header[512],record[32];encodeLogHeader(header,1,0);
    GpsFix fix{35.681641,140.027194,0,true};check(encodeLogRecord(fix,record),"encode fixture fix");
    {std::ofstream out(root/"logs/lifecycle.gps.dat",std::ios::binary);
     out.write(reinterpret_cast<char*>(header),sizeof(header));out.write(reinterpret_cast<char*>(record),sizeof(record));}
    // Restart before the first I/O so begin() constructs paths under the fixture root.
    task.converting().exporter->root_=root.string();task.converting().exporter->state_={};task.converting().started=false;
    for(unsigned i=0;i<20 && task.work_kind_!=UsbStorageTask::Work::None;++i)task.stepExport();
    check((!std::get_if<UsbExportWork>(&task.work_) || !task.converting().exporter) && task.state_.export_job.phase==ExportPhase::Complete && task.state_.export_job.output_created &&
          task.state_.export_job.processed==1 && publications==2,"conversion result survives converter destruction and USB republication");
    check(std::filesystem::exists(root/"export/lifecycle.gps.dat.gpx"),"completed GPX exists");
    command(task,UsbActionKind::Publish);task.stepExport();
    select(task);command(task,UsbActionKind::Start);task.stepExport();
    // Model the pending handles of a failed conversion. Host fclose reports an
    // injected failure; the owner must retain the converter and withhold USB.
    task.converting().exporter->input_.file_=std::tmpfile();check(task.converting().exporter->input_.file_,"open pending handle");
    task.converting().exporter->input_.opened_=true;
    task.state_.export_job.phase=ExportPhase::Failed;task.work_kind_=UsbStorageTask::Work::None;
    command(task,UsbActionKind::Publish);fail_close=true;task.stepExport();
    check(!fail_close && task.converting().exporter && task.state_.export_job.error==ExportError::Close && publications==2 && task.usb_.local(),
          "close failure retains the converter and local SD ownership");
    command(task,UsbActionKind::Publish);fail_publish=true;task.stepExport();
    check((!std::get_if<UsbExportWork>(&task.work_) || !task.converting().exporter) && task.state_.export_job.error==ExportError::Publish && publications==3,"closed workspace is freed even if republication fails");
    command(task,UsbActionKind::Publish);task.stepExport();
    check((!std::get_if<UsbExportWork>(&task.work_) || !task.converting().exporter) && task.state_.export_job.phase==ExportPhase::Idle && publications==4,"republication retry returns to idle without reallocating");
    std::filesystem::remove_all(root);
    std::puts("PASS: USB export lazy allocation, allocation failure, completed result, close retention and republication retry");
}

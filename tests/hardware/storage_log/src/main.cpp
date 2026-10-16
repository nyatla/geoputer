#include <M5Cardputer.h>
#include <sd_diskio.h>
#include <SD.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <memory>
#include <stdexcept>
#include <freertos/semphr.h>
#include "../../../../application/task/storage/log/LogWriter.cpp"
#include "../../../../application/task/storage/log/LogReader.cpp"

using namespace geoputer;
namespace {
SPIClass spi(FSPI);
void check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
struct Activity : StorageObserver {
    unsigned depth=0, count=0;
    StaticSemaphore_t entered_control{}, release_control{}, done_control{};
    SemaphoreHandle_t entered=xSemaphoreCreateBinaryStatic(&entered_control);
    SemaphoreHandle_t release=xSemaphoreCreateBinaryStatic(&release_control);
    SemaphoreHandle_t done=xSemaphoreCreateBinaryStatic(&done_control);
    bool block=false;
    void sdBegin() override {
        if (!depth++) ++count;
        if (block) {
            block=false;
            xSemaphoreGive(entered);
            xSemaphoreTake(release,portMAX_DELAY);
        }
    }
    void sdEnd() override { check(depth>0,"balanced SD scopes"); --depth; }
    void cacheState(RgCacheState,bool) override {}
};
void saveDuringAppend(LogWriter& writer,Activity& activity,LogCommand point,unsigned first,unsigned last) {
    struct Run { LogWriter* writer; Activity* activity; bool ok=false; } run{&writer,&activity};
    activity.block=true;
    auto consume=[](void* arg) {
        auto& run=*static_cast<Run*>(arg);
        run.ok=run.writer->flush(millis());
        xSemaphoreGive(run.activity->done);
        vTaskDelete(nullptr);
    };
    check(xTaskCreatePinnedToCore(consume,"RingSaveTest",8192,&run,1,nullptr,0)==pdPASS,"create concurrent Storage consumer");
    const bool entered=xSemaphoreTake(activity.entered,pdMS_TO_TICKS(5000))==pdTRUE;
    bool accepted=entered;
    uint32_t max_us=0;
    if (entered) {
        for(unsigned n=first;n<=last;++n) {
            point.sample_seq=n; point.fix.utc_epoch_ms=n*1000ull;
            const auto start=micros();
            accepted &= writer.append(point,millis())==LogError::None;
            const uint32_t elapsed=micros()-start;
            if (elapsed>max_us) max_us=elapsed;
        }
        point.sample_seq=last+1;
        accepted &= writer.append(point,millis())==LogError::BufferFull;
        accepted &= writer.snapshot().buffered==64;
    }
    xSemaphoreGive(activity.release);
    xSemaphoreTake(activity.done,portMAX_DELAY);
    check(entered && accepted && run.ok,"App appends during pinned I/O without waiting for Storage consumer");
    Serial.printf("[RING-TEST] producer_core=%u consumer_core=0 append=%u..%u max_us=%lu saved=%lu buffered=%u\n",
                  static_cast<unsigned>(xPortGetCoreID()),first,last,static_cast<unsigned long>(max_us),
                  static_cast<unsigned long>(writer.snapshot().saved),writer.snapshot().buffered);
}
void verifyRing(const std::string& path,uint64_t session) {
    auto* file=std::fopen(path.c_str(),"rb"); check(file!=nullptr,"open ring readback");
    uint8_t header[512],record[32];
    bool valid=std::fread(header,1,512,file)==512 && validLogHeader(header) && logGet(header+16,8)==session;
    for(unsigned n=1;n<=104 && valid;++n)
        valid=std::fread(record,1,32,file)==32 && logGet(record,8)==n*1000ull &&
              logGet(record+28,4)==logCrc32(record,28) && logGet(record+8,4)==356816410 && logGet(record+12,4)==1400271940;
    valid &= std::fgetc(file)==EOF;
    std::fclose(file);
    check(valid,"ring wrap preserves all 104 records in order with exact CRC");
}
void readExisting(uint8_t drive) {
    auto reader=std::make_unique<LogReader>("/sd/logs");reader->setDrive(drive);
    LogReadRequest request{};request.kind=LogReadKind::List;
    check(reader->begin(request),"accept existing log list");while(!reader->step())delay(1);
    check(reader->result().error==LogReadError::None,"read existing log directory without changes");
    if(!reader->result().count) {Serial.println("[BROWSE-EXISTING] EMPTY");return;}
    std::snprintf(request.name,sizeof(request.name),"%s",reader->result().data.names[0]);
    request.kind=LogReadKind::Summary;check(reader->begin(request),"existing summary request");reader->step();
    if(reader->result().error!=LogReadError::None) {
        Serial.printf("[BROWSE-EXISTING] name=%s error=%u\n",request.name,static_cast<unsigned>(reader->result().error));return;
    }
    const auto summary=reader->result().summary;
    request.kind=LogReadKind::Records;request.session_id=summary.session_id;request.limit=summary.records;request.count=10;
    for(request.start=0;request.start<request.limit;request.start+=10) {
        check(reader->begin(request),"existing records request");reader->step();
        check(reader->result().error==LogReadError::None,"existing log full readback and CRC");
        delay(1);
    }
    Serial.printf("[BROWSE-EXISTING] PASS name=%s records=%lu bytes=%llu tail=%u full_CRC=OK read_only=YES\n",
                  request.name,static_cast<unsigned long>(summary.records),static_cast<unsigned long long>(summary.bytes),summary.tail);
}
void verify(const std::string& path, unsigned records, uint64_t session) {
    auto* file=std::fopen(path.c_str(),"rb"); check(file!=nullptr,"read back file");
    uint8_t header[512], record[32];
    check(std::fread(header,1,512,file)==512 && validLogHeader(header) && logGet(header+16,8)==session,"header and session CRC");
    for(unsigned i=0;i<records;++i) {
        check(std::fread(record,1,32,file)==32 && logGet(record+28,4)==logCrc32(record,28),"record CRC");
        check(logGet(record+8,4)==356816410 && logGet(record+12,4)==1400271940,"coordinate readback");
    }
    check(std::fgetc(file)==EOF,"no duplicate or extra records"); std::fclose(file);
}
}
void setup() {
    Serial.begin(115200); Serial.setTxTimeoutMs(0);
    auto config=M5.config(); config.internal_spk=false; config.internal_mic=false;
    M5Cardputer.begin(config,true);
    pinMode(5,OUTPUT); digitalWrite(5,HIGH);
    pinMode(12,OUTPUT); digitalWrite(12,HIGH);
    delay(5000);
    std::string directory; std::vector<std::string> created;
    try {
        spi.begin(40,39,14,12);
        const auto drive=sdcard_init(12,&spi,4000000);
        check(drive!=0xff && sdcard_mount(drive,"/sd",5,false),"mount existing SD without formatting");
        const uint64_t session=(static_cast<uint64_t>(esp_random())<<32)|esp_random();
        char name[80]; std::snprintf(name,sizeof(name),"/sd/.geoputer-log-test-%016llx",static_cast<unsigned long long>(session));
        directory=name; check(mkdir(directory.c_str(),0775)==0,"create unique test directory");
        Activity activity;
        {
            auto owned_writer=std::make_unique<LogWriter>(directory,&activity);
            auto& writer=*owned_writer; writer.setDrive(drive);
            uint64_t utc=0; check(nmeaEpochMs("151026",0,utc),"GPS date conversion");
            check(writer.open(session,utc)==LogError::None,"open session");
            const auto accesses=activity.count;
            LogCommand point{}; point.session_id=session; point.fix={35.681641,140.027194,0,true};
            point.fix.fields=GpsTime; point.fix.utc_epoch_ms=utc;
            for(unsigned n=1;n<=64;++n) { point.sample_seq=n; check(writer.append(point)==LogError::None,"append 64 points"); }
            check(activity.count==accesses,"RAM appends cause no SD access");
            point.sample_seq=65; check(writer.append(point)==LogError::BufferFull,"reject buffer overflow");
            check(writer.flush(millis()),"save 64 points and FatFs f_sync"); created.push_back(writer.path());
            check(writer.snapshot().saved==64 && writer.snapshot().buffered==0,"committed counters");
            verify(writer.path(),64,session);
            check(writer.requestFlush(session,64,false)==LogError::None && writer.flush(millis()),"pause boundary");
            check(writer.append(point)==LogError::None,"resume same session");
            check(writer.requestFlush(session,65,true)==LogError::None && writer.flush(millis()),"stop saves pending record and closes");
            verify(writer.path(),65,session);
            check(writer.open(session,utc)==LogError::SessionExists,"reject duplicate persisted session");
            check(writer.open(session+1,utc)==LogError::None,"open collision-time session");
            point.session_id=session+1; point.sample_seq=1;
            check(writer.append(point)==LogError::None && writer.flush(millis()),"save same-second session"); created.push_back(writer.path());
            check(writer.path().find("_1.gps.dat")!=std::string::npos,"exclusive create suffix");
            check(writer.requestFlush(session+1,1,true)==LogError::None && writer.flush(millis()),"close second session");
            check(writer.open(session+2,UINT64_MAX)==LogError::None,"open empty session");
            check(writer.requestFlush(session+2,0,true)==LogError::None && writer.flush(millis()) && writer.path().empty(),"empty session has no file");
            check(writer.open(session+3,UINT64_MAX)==LogError::None,"open concurrent ring session");
            point.session_id=session+3;
            for(unsigned n=1;n<=40;++n) {
                point.sample_seq=n; point.fix.utc_epoch_ms=n*1000ull;
                check(writer.append(point,millis())==LogError::None,"ring initial 40 points");
            }
            saveDuringAppend(writer,activity,point,41,64); created.push_back(writer.path());
            check(writer.snapshot().saved==40 && writer.snapshot().buffered==24,"only the first pinned 40 records are released");
            for(unsigned n=65;n<=90;++n) {
                point.sample_seq=n; point.fix.utc_epoch_ms=n*1000ull;
                check(writer.append(point,millis())==LogError::None,"ring tail wraps");
            }
            saveDuringAppend(writer,activity,point,91,104);
            check(writer.snapshot().saved==90 && writer.snapshot().buffered==14,"wrapped save retains its concurrently appended suffix");
            auto reader=std::make_unique<LogReader>(directory,&activity);reader->setDrive(drive);
            LogReadRequest request{};request.kind=LogReadKind::Summary;
            std::snprintf(request.name,sizeof(request.name),"%s",std::strrchr(writer.path().c_str(),'/')+1);
            check(reader->begin(request) && reader->step(&writer) && reader->result().error==LogReadError::None && reader->result().summary.records==90,
                  "active log reader excludes the 14 buffered points and uses shared FIL");
            request.kind=LogReadKind::Records;request.session_id=session+3;request.limit=90;request.start=84;request.count=10;
            check(reader->begin(request) && reader->step(&writer) && reader->result().error==LogReadError::None && reader->result().count==6 && reader->result().eof,
                  "active reader observes fixed upper bound and short final batch");
            check(writer.requestFlush(session+3,104,true)==LogError::None && writer.flush(millis()),"close ring session");
            verifyRing(writer.path(),session+3);
            Serial.println("[RING-TEST] PASS records=104 wrap=OK concurrent_append=OK overflow=OK CRC=OK");
            request.kind=LogReadKind::Summary;
            check(reader->begin(request) && reader->step(&writer) && reader->result().summary.records==104,"reader sees final saved file after close");
            request={};request.kind=LogReadKind::List;check(reader->begin(request),"accept synthetic log list");
            while(!reader->step(&writer))delay(1);
            check(reader->result().error==LogReadError::None && reader->result().count==3,"list synthetic files independently by name");
            Serial.println("[BROWSE-TEST] PASS list=3 active_prefix=90 final=104 EOF=OK CRC=OK");
        }
        for(const auto& path:created) check(unlink(path.c_str())==0,"remove own test file");
        check(rmdir(directory.c_str())==0,"remove own test directory"); directory.clear();
        readExisting(drive);
        Serial.printf("[STORAGE-TEST] PASS records=65+1+104 CRC=OK overflow=OK pause/resume=OK collision=OK empty=OK ring=OK cleanup=OK heap=%u stack_free=%u\n",
                      ESP.getFreeHeap(),static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    } catch(const std::exception& error) {
        Serial.printf("[STORAGE-TEST] FAIL %s directory=%s\n",error.what(),directory.c_str());
    }
}
void loop() { delay(1000); }

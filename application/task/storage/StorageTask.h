#pragma once

#include <SPI.h>
#include "SettingsStore.h"
#include "galuchat/PlaceLookup.h"
#include "log/LogWriter.h"
#include "log/LogReader.h"
#include "../../TaskMessages.h"
#include "../../utils/Task.h"

namespace geoputer {

class StorageTask final : public Task<TaskMessage, 1>, private StorageObserver {
public:
    StorageTask(Queue<TaskMessage, 8>& notifications, Queue<TaskMessage, 1>& results)
        : notifications_(notifications), results_(results), log_writer_("/sd/logs", this), log_reader_("/sd/logs",this) { place_lookup_.setObserver(this); }
    // Runs in the App caller; touches only the shared RAM ring, never SD or task queues.
    LogError tryAppendLog(const LogCommand& command, LogSnapshot& snapshot);
protected:
    void run() override;
private:
    void handle(const TaskMessage& request);
    void sendReturn();
    void completeJob();
    void notifyActivity();
    void sdBegin() override;
    void sdEnd() override;
    void cacheState(RgCacheState state, bool new_dataset = false) override;
    void flushLog(bool force=false);
    void stepLogRead();
    void notify();
    void mountCard();
    void closeCard();
    bool formatCard();
    bool loadSettings();
    bool saveSettings(const Settings& value);

    Queue<TaskMessage, 8>& notifications_;
    Queue<TaskMessage, 1>& results_;
    SettingsStore settings_store_;
    PlaceLookup place_lookup_;
    LogWriter log_writer_;
    LogReader log_reader_;
    uint64_t read_barrier_session_=0;
    uint32_t read_barrier_=0, read_request_id_=0;
    bool has_read_=false, read_prepared_=false;
    unsigned sd_depth_ = 0;
    bool sd_dirty_ = false, rg_dirty_ = false;
    uint32_t next_log_retry_ = 0;
    PlaceRequest place_request_{};
    SPIClass spi_{FSPI};  // LCD uses SPI3/HSPI. SD owns the other SPI peripheral.
    StorageSnapshot state_{};
    TaskMessage pending_return_{};
    TaskMessage completion_{};
    StorageCommand job_{};
    uint32_t job_request_id_ = 0;
    uint32_t sequence_ = 0;
    uint8_t drive_ = 0xFF;
    bool nvs_ready_ = false;
    bool mounted_ = false;
    bool initialized_ = false;
    bool has_return_ = false;
    bool has_job_ = false;
    bool load_job_ = false;
    bool search_job_ = false;
    bool dataset_job_ = false;
    bool has_completion_ = false;
};

}  // namespace geoputer

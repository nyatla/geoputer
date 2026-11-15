#pragma once
#include "AppScreenResources.h"
#include "alarm/AlarmEngine.h"
#include "task/storage/PlaceSearchFlow.h"
namespace geoputer {
enum class AppBootStage {NeedSettings,WaitSettings,NeedSdDecision,AwaitSdDecision,WaitSdOperation,SelectMode,NeedDataset,WaitDataset,Complete};
struct GpsAppState {
    LogCommand log_pending_record_{};
    UiAction log_control_{};
    bool log_pending_ = false, log_accepting_ = false;
    uint32_t log_seen_seq_ = 0, log_adopt_at_ = 0, log_retry_at_ = 0, log_elapsed_at_ = 0;
    bool log_adopted_ = false;
    AppScreenResources screen_resources_;
    map::Request map_request_{},map_session_{};
    bool map_session_close_=false,map_session_ready_=false;
    uint32_t map_token_=0xa0000000u;
    bool map_cancel_=false,map_storage_owned_=false;
    alarm::Engine alarm_engine_;
    alarm::SoundSession alarm_sound_;
    uint32_t alarm_profile_revision_=0,alarm_identity_=0,alarm_sample_seq_=0;
    uint8_t alarm_dataset_=0;
    bool alarm_audio_owned_=false,alarm_preview_=false;
    PlaceSearchFlow place_flow_;
    // Loan ownership survives screen replacement and accepted async work.
    PlaceRequest place_borrow_{};
    bool place_completion_ready_=false,place_queued_=false,place_discard_=false;
    uint32_t dataset_token_ = 0, dataset_waiting_ = 0;
    DatasetRequest dataset_borrow_{},dataset_next_{};
    IndexProgress dataset_progress_{};
    DatasetError dataset_error_=DatasetError::None;
    bool dataset_pending_=false,dataset_ready_=false,dataset_queued_=false,dataset_discard_=false,dataset_progress_dirty_=false;
    bool dataset_applying_ = false;
    bool dataset_check_needed_ = false;
    uint32_t dataset_check_waiting_ = 0;
    uint32_t place_request_id_ = 0x90000000u;
    using BootStage=AppBootStage;
    BootStage boot_stage_ = BootStage::NeedSettings;
    uint32_t boot_completed_at_ = 0;
    uint32_t boot_selection_at_ = 0;
};
}

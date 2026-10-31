#include "GpsApplication.h"
namespace geoputer {
bool GpsApplication::alarmAvailable()const {
    const auto& p=data_.storage.alarm;
    return !data_.boot.maintenance && screen_!=ScreenId::Boot &&
        data_.settings.dataset && p.dataset==data_.settings.dataset && p.usable() && p.settings.enabled &&
        !data_.storage.nvs_error && data_.storage.card_state==CardState::Mounted && data_.dataset.state==PlaceState::Ready &&
        data_.gps.fix.valid && data_.gps.reception==GpsReception::Fix;
}
bool GpsApplication::audioCommand(const alarm::AudioCommand& audio) {
    UiRequest request{};request.kind=MessageKind::ControlAudio;request.payload.audio=audio;
    SyncReturn result{};return callUi(request,result) && result.status==OperationStatus::Ok;
}
void GpsApplication::stopAlarm() {

    gpsState().alarm_sound_.stop();gpsState().alarm_preview_=false;
    if(gpsState().alarm_audio_owned_ && audioCommand({}))gpsState().alarm_audio_owned_=false;
}
bool GpsApplication::playAlarm(const alarm::Settings& settings,bool preview) {
    const auto now=taskTimeMs();
    if(!preview && gpsState().alarm_sound_.active(now) && gpsState().alarm_sound_.oneShot())return true;
    if(preview){stopAlarm();gpsState().alarm_sound_.stop();}
    const auto policy=power::policy(data_.settings.power_level,BootMode::GpsLogger);
    gpsState().alarm_sound_.start(settings.tone,preview?0:settings.duration,data_.settings.volume,now,
        policy.manage_audio?power::AudioPreparationBudgetMs:0);
    auto command=gpsState().alarm_sound_.command();command.preview=preview;
    if(!audioCommand(command)) {gpsState().alarm_sound_.stop();message("AUDIO UNAVAILABLE");return false;}
    gpsState().alarm_audio_owned_=true;gpsState().alarm_preview_=preview;return true;
}
void GpsApplication::processAlarm() {
    const auto& p=data_.storage.alarm;
    if(gpsState().alarm_profile_revision_!=p.revision || gpsState().alarm_identity_!=p.identity || gpsState().alarm_dataset_!=data_.settings.dataset) {
        stopAlarm();gpsState().alarm_engine_.reset();gpsState().alarm_sample_seq_=0;
        gpsState().alarm_profile_revision_=p.revision;gpsState().alarm_identity_=p.identity;gpsState().alarm_dataset_=data_.settings.dataset;
        if(p.state==alarm::State::Repaired)message("ALERT CONFIG RESET / OFF");
        else if(p.state==alarm::State::IoError || p.state==alarm::State::DataError)message(alarm::stateName(p.state));
    }
    if(gpsState().alarm_audio_owned_ && (!gpsState().alarm_sound_.active(taskTimeMs()) || (!gpsState().alarm_preview_ && !alarmAvailable()) || data_.storage.card_state!=CardState::Mounted || data_.storage.nvs_error))stopAlarm();
    requestAlarmSample();
}
void GpsApplication::requestAlarmSample() {
    if(!alarmAvailable() || !data_.gps.sample_seq || gpsState().alarm_sample_seq_==data_.gps.sample_seq || gpsState().place_borrow_.token || gpsState().place_flow_.pending() || gpsState().map_request_.token || browse_waiting_ || gpsState().dataset_pending_ || gpsState().dataset_waiting_ || data_.storage.busy || gpsState().dataset_check_needed_ || gpsState().dataset_check_waiting_)return;
    StorageRequest request{};request.kind=MessageKind::SearchPlace;request.source=TaskId::Application;
    if(++gpsState().place_request_id_==0)++gpsState().place_request_id_;
    request.request_id=gpsState().place_request_id_;
    auto& search=request.payload.place_request;search={generation_,data_.gps.sample_seq,data_.gps.fix};
    search.token=request.request_id;search.dataset_generation=data_.dataset.generation;
    search.alarm_sample=true;search.alarm_revision=data_.storage.alarm.revision;search.work_bytes=config::IndexReadBufferBytes;
    if(!placeUiCommand(MessageKind::BeginPlaceSearch,search,&search.buffer))return;
    gpsState().place_borrow_=search;gpsState().place_queued_=gpsState().place_discard_=false;
    if(sendSpeculative(storage_.inbox(),request)!=SpeculativeResult::Queued) {gpsState().place_completion_ready_=true;gpsState().place_discard_=true;return;}
    gpsState().alarm_sample_seq_=data_.gps.sample_seq;gpsState().place_completion_ready_=false;gpsState().place_queued_=true;
}
void GpsApplication::completeAlarmSample() {
    alarm::Observation observation{};
    bool publish=false;
    if(gpsState().place_queued_ && !gpsState().place_discard_) {
        const auto& result=*placeResultMemory(gpsState().place_borrow_);
        publish=alarmAvailable() && samePlaceRequest(result.request,gpsState().place_borrow_) &&
            result.state==PlaceState::Ready && gpsState().place_borrow_.dataset_generation==data_.dataset.generation &&
            gpsState().place_borrow_.alarm_revision==data_.storage.alarm.revision;
        if(publish)observation=result.alarm;
    }
    if(!placeUiCommand(MessageKind::DiscardPlaceSearch,gpsState().place_borrow_))return;
    gpsState().place_borrow_={};gpsState().place_completion_ready_=gpsState().place_queued_=gpsState().place_discard_=false;
    if(publish && gpsState().alarm_engine_.observe(observation,data_.storage.alarm.settings))playAlarm(data_.storage.alarm.settings);
}
} // namespace geoputer

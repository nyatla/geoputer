#include "Application.h"
namespace geoputer {
bool Application::alarmAvailable()const {
    const auto& p=data_.storage.alarm;
    return !usb_mode_ && !data_.boot.maintenance && screen_!=ScreenId::Boot &&
        data_.settings.dataset && p.dataset==data_.settings.dataset && p.usable() && p.settings.enabled &&
        !data_.storage.nvs_error && data_.storage.card_state==CardState::Mounted && data_.dataset.state==PlaceState::Ready &&
        data_.gps.fix.valid && data_.gps.reception==GpsReception::Fix;
}
bool Application::audioCommand(const alarm::AudioCommand& audio) {
    UiRequest request{};request.kind=MessageKind::ControlAudio;request.payload.audio=audio;
    SyncReturn result{};return callUi(request,result) && result.status==OperationStatus::Ok;
}
void Application::stopAlarm() {
    if(usb_mode_)return;
    gpsMode().alarm_sound_.stop();gpsMode().alarm_preview_=false;
    if(gpsMode().alarm_audio_owned_ && audioCommand({}))gpsMode().alarm_audio_owned_=false;
}
bool Application::playAlarm(const alarm::Settings& settings,bool preview) {
    const auto now=taskTimeMs();
    if(!preview && gpsMode().alarm_sound_.active(now) && gpsMode().alarm_sound_.oneShot())return true;
    if(preview){stopAlarm();gpsMode().alarm_sound_.stop();}
    const auto policy=power::policy(data_.settings.power_level,usb_mode_?BootMode::UsbStorage:BootMode::GpsLogger);
    gpsMode().alarm_sound_.start(settings.tone,preview?0:settings.duration,data_.settings.volume,now,
        policy.manage_audio?power::AudioPreparationBudgetMs:0);
    auto command=gpsMode().alarm_sound_.command();command.preview=preview;
    if(!audioCommand(command)) {gpsMode().alarm_sound_.stop();message("AUDIO UNAVAILABLE");return false;}
    gpsMode().alarm_audio_owned_=true;gpsMode().alarm_preview_=preview;return true;
}
void Application::processAlarm() {
    const auto& p=data_.storage.alarm;
    if(gpsMode().alarm_profile_revision_!=p.revision || gpsMode().alarm_identity_!=p.identity || gpsMode().alarm_dataset_!=data_.settings.dataset) {
        stopAlarm();gpsMode().alarm_engine_.reset();gpsMode().alarm_sample_seq_=0;
        gpsMode().alarm_profile_revision_=p.revision;gpsMode().alarm_identity_=p.identity;gpsMode().alarm_dataset_=data_.settings.dataset;
        if(p.state==alarm::State::Repaired)message("ALERT CONFIG RESET / OFF");
        else if(p.state==alarm::State::IoError || p.state==alarm::State::DataError)message(alarm::stateName(p.state));
    }
    if(gpsMode().alarm_audio_owned_ && (!gpsMode().alarm_sound_.active(taskTimeMs()) || (!gpsMode().alarm_preview_ && !alarmAvailable()) ||
       data_.storage.card_state!=CardState::Mounted || data_.storage.nvs_error))stopAlarm();
    requestAlarmSample();
}
void Application::requestAlarmSample() {
    if(!alarmAvailable() || !data_.gps.sample_seq || gpsMode().alarm_sample_seq_==data_.gps.sample_seq ||
        gpsMode().place_borrow_.token || gpsMode().place_flow_.pending() || gpsMode().map_request_.token || browse_waiting_ ||
        gpsMode().dataset_pending_ || gpsMode().dataset_waiting_ || data_.storage.busy || gpsMode().dataset_check_needed_ || gpsMode().dataset_check_waiting_)return;
    StorageRequest request{};request.kind=MessageKind::SearchPlace;request.source=TaskId::Application;
    if(++gpsMode().place_request_id_==0)++gpsMode().place_request_id_;
    request.request_id=gpsMode().place_request_id_;
    auto& search=request.payload.place_request;search={generation_,data_.gps.sample_seq,data_.gps.fix};
    search.token=request.request_id;search.dataset_generation=data_.dataset.generation;
    search.alarm_sample=true;search.alarm_revision=data_.storage.alarm.revision;search.work_bytes=config::IndexReadBufferBytes;
    if(!placeUiCommand(MessageKind::BeginPlaceSearch,search,&search.buffer))return;
    gpsMode().place_borrow_=search;gpsMode().place_queued_=gpsMode().place_discard_=false;
    if(sendSpeculative(storage_.inbox(),request)!=SpeculativeResult::Queued) {gpsMode().place_completion_ready_=true;gpsMode().place_discard_=true;return;}
    gpsMode().alarm_sample_seq_=data_.gps.sample_seq;gpsMode().place_completion_ready_=false;gpsMode().place_queued_=true;
}
void Application::completeAlarmSample() {
    alarm::Observation observation{};
    bool publish=false;
    if(gpsMode().place_queued_ && !gpsMode().place_discard_) {
        const auto& result=*placeResultMemory(gpsMode().place_borrow_);
        publish=alarmAvailable() && samePlaceRequest(result.request,gpsMode().place_borrow_) &&
            result.state==PlaceState::Ready && gpsMode().place_borrow_.dataset_generation==data_.dataset.generation &&
            gpsMode().place_borrow_.alarm_revision==data_.storage.alarm.revision;
        if(publish)observation=result.alarm;
    }
    if(!placeUiCommand(MessageKind::DiscardPlaceSearch,gpsMode().place_borrow_))return;
    gpsMode().place_borrow_={};gpsMode().place_completion_ready_=gpsMode().place_queued_=gpsMode().place_discard_=false;
    if(publish && gpsMode().alarm_engine_.observe(observation,data_.storage.alarm.settings))playAlarm(data_.storage.alarm.settings);
}
} // namespace geoputer

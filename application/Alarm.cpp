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
    alarm_sound_.stop();alarm_preview_=false;
    if(alarm_audio_owned_ && audioCommand({}))alarm_audio_owned_=false;
}
bool Application::playAlarm(const alarm::Settings& settings,bool preview) {
    const auto now=taskTimeMs();
    if(!preview && alarm_sound_.active(now) && alarm_sound_.oneShot())return true;
    if(preview){stopAlarm();alarm_sound_.stop();}
    const auto policy=power::policy(data_.settings.power_level,usb_mode_?BootMode::UsbStorage:BootMode::GpsLogger);
    alarm_sound_.start(settings.tone,preview?0:settings.duration,data_.settings.volume,now,
        policy.manage_audio?power::AudioPreparationBudgetMs:0);
    if(!audioCommand(alarm_sound_.command())) {alarm_sound_.stop();message("AUDIO UNAVAILABLE");return false;}
    alarm_audio_owned_=true;alarm_preview_=preview;return true;
}
void Application::processAlarm() {
    const auto& p=data_.storage.alarm;
    if(alarm_profile_revision_!=p.revision || alarm_identity_!=p.identity || alarm_dataset_!=data_.settings.dataset) {
        stopAlarm();alarm_engine_.reset();alarm_sample_seq_=0;
        alarm_profile_revision_=p.revision;alarm_identity_=p.identity;alarm_dataset_=data_.settings.dataset;
        if(p.state==alarm::State::Repaired)message("ALERT CONFIG RESET / OFF");
        else if(p.state==alarm::State::IoError || p.state==alarm::State::DataError)message(alarm::stateName(p.state));
    }
    if(alarm_audio_owned_ && (!alarm_sound_.active(taskTimeMs()) || (!alarm_preview_ && !alarmAvailable()) ||
       data_.storage.card_state!=CardState::Mounted || data_.storage.nvs_error))stopAlarm();
    if(!alarmAvailable())alarm_engine_.unavailable();
    requestAlarmSample();
}
void Application::requestAlarmSample() {
    if(!alarmAvailable() || !data_.gps.sample_seq || alarm_sample_seq_==data_.gps.sample_seq ||
        place_borrow_.token || place_flow_.pending() || map_request_.token || browse_waiting_ ||
        dataset_pending_ || dataset_waiting_ || data_.storage.busy || dataset_check_needed_ || dataset_check_waiting_)return;
    StorageRequest request{};request.kind=MessageKind::SearchPlace;request.source=TaskId::Application;
    if(++place_request_id_==0)++place_request_id_;
    request.request_id=place_request_id_;
    auto& search=request.payload.place_request;search={generation_,data_.gps.sample_seq,data_.gps.fix};
    search.token=request.request_id;search.dataset_generation=data_.dataset.generation;
    search.alarm_sample=true;search.alarm_revision=data_.storage.alarm.revision;search.work_bytes=config::IndexReadBufferBytes;
    if(!placeUiCommand(MessageKind::BeginPlaceSearch,search,&search.buffer))return;
    place_borrow_=search;place_queued_=place_discard_=false;
    if(sendSpeculative(storage_.inbox(),request)!=SpeculativeResult::Queued) {place_completion_ready_=true;place_discard_=true;return;}
    alarm_sample_seq_=data_.gps.sample_seq;place_completion_ready_=false;place_queued_=true;
}
void Application::completeAlarmSample() {
    alarm::Observation observation{};
    bool publish=false;
    if(place_queued_ && !place_discard_) {
        const auto& result=*placeResultMemory(place_borrow_);
        publish=alarmAvailable() && samePlaceRequest(result.request,place_borrow_) &&
            result.state==PlaceState::Ready && place_borrow_.dataset_generation==data_.dataset.generation &&
            place_borrow_.alarm_revision==data_.storage.alarm.revision;
        if(publish)observation=result.alarm;
    }
    if(!placeUiCommand(MessageKind::DiscardPlaceSearch,place_borrow_))return;
    place_borrow_={};place_completion_ready_=place_queued_=place_discard_=false;
    if(publish && alarm_engine_.observe(observation,data_.storage.alarm.settings,taskTimeMs()))playAlarm(data_.storage.alarm.settings);
}
} // namespace geoputer

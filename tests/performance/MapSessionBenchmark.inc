// Included only inside the isolated benchmark Application::runOnce.
static unsigned bench_stage=0;
static bool bench_recolored=false;
static uint32_t bench_started=0,bench_stable=0;
static GpsFix bench_base{},bench_fix{};
const auto bench_now=taskTimeMs();
if(!bench_stage && screen_==ScreenId::LivePlace && data_.boot.complete && data_.dataset.state==PlaceState::Ready && data_.gps.fix.valid &&
    (data_.place_state==PlaceState::Ready || data_.place_state==PlaceState::NotFound) && !place_flow_.pending() && !data_.storage.busy) {
    bench_base=bench_fix=data_.gps.fix;bench_started=bench_now;bench_stage=1;
    Console.println("[SESSION-BENCH] begin; virtual positions, no log writes or settings changes");
    reportMapHeap("bench-before-map",0);
    setScreen(ScreenId::Map);
}
if(bench_stage && bench_stage<10) {
    data_.gps.fix=bench_fix;data_.gps.reception=GpsReception::Fix;data_.gps.sample_seq=0xf0000000u+bench_stage;
    const bool on_map=bench_stage<=7;
    const bool ready=!map_request_.token && !map_session_close_ && !place_flow_.pending() && !data_.storage.busy &&
        (on_map?(map_session_ready_ && !data_.map.busy && map_session_.screen_generation==generation_):!map_session_.token);
    if(bench_stage==1 && ready && !bench_recolored) {
        bench_recolored=true;data_.place_region^=1u;data_.place_fix=bench_fix;data_.place_state=PlaceState::Ready;
        ++data_.revision;ui_data_dirty_=true;
        Console.println("[SESSION-BENCH] recolor from retained pixels; no SD read expected");
    } else if(on_map && data_.map.error!=map::Error::None && !data_.map.busy) {
        Console.printf("[SESSION-BENCH-ERROR] stage=%u error=%u\n",bench_stage,unsigned(data_.map.error));
        bench_stage=10;setScreen(ScreenId::LivePlace);Console.println("[SESSION-BENCH-DONE] FAILED");
    } else if(bench_now-bench_started>150000) {
        Console.printf("[SESSION-BENCH-ERROR] timed out stage=%u\n",bench_stage);
        bench_stage=10;setScreen(ScreenId::LivePlace);Console.println("[SESSION-BENCH-DONE] FAILED");
    } else if(ready) {
        if(!bench_stable)bench_stable=bench_now;
        if(bench_now-bench_stable>=500) {
            const char* stages[]={"", "bench-live-ready", "bench-live-cache-miss", "bench-live-return", "bench-live-half",
                "bench-live-quarter", "bench-log-ready", "bench-log-cache-miss", "bench-text-released", "bench-exit"};
            reportMapHeap(stages[bench_stage],bench_stage);
            Console.printf("[SESSION-BENCH] completed=%u elapsed_ms=%lu\n",bench_stage,static_cast<unsigned long>(bench_now-bench_started));
            bench_stable=0;++bench_stage;
            if(bench_stage==2) {bench_fix.latitude+=.02;refreshPlace();}
            if(bench_stage==3) {bench_fix=bench_base;refreshPlace();}
            if(bench_stage==4)changeMapScale(2);
            if(bench_stage==5)changeMapScale(4);
            if(bench_stage==6) {
                auto& view=data_.editBrowse();view={};view.point_loaded=true;view.map_mode=true;view.point=bench_base;
                view.summary.records=1;view.summary.session_id=1;setScreen(ScreenId::LogPoint);
            }
            if(bench_stage==7) {data_.editBrowse().point.latitude+=.02;refreshPlace();}
            if(bench_stage==8) {UiAction toggle{};toggle.browse_action=BrowseAction::Text;operateBrowser(toggle);}
            if(bench_stage==9)setScreen(ScreenId::LivePlace);
            if(bench_stage==10)Console.println("[SESSION-BENCH-DONE] OK");
        }
    } else bench_stable=0;
}

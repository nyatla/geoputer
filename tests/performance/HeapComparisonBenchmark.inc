// Read-only paired benchmark: common states are identical in both source versions.
static unsigned hstage=0,hcycle=0,hrow=0;
static uint32_t hbegin=0,hstable=0;
const auto hnow=taskTimeMs();
const bool hquiet=!map_request_.token && !map_session_close_ && !place_flow_.pending() && !place_borrow_.token &&
    !data_.storage.busy && !browse_waiting_ && !browse_release_generation_;
const bool hmap=hquiet && map_session_ready_ && !data_.map.busy && map_session_.screen_generation==generation_;
if(!hstage && screen_==ScreenId::LivePlace && data_.boot.complete && data_.gps.fix.valid &&
    data_.dataset.state==PlaceState::Ready && (data_.place_state==PlaceState::Ready || data_.place_state==PlaceState::NotFound) && hquiet) {
    hstage=1;hbegin=hnow;hstable=hnow;benchmark::mark(benchmark::Live);
    Console.printf("[HEAP-COMPARE] version=%u dataset=%u fixed=35.681641,140.027194 no writes/NVS changes\n",HEAP_COMPARISON_AFTER,data_.settings.dataset);
}
if(hstage && hstage<90) {
    if(hnow-hbegin>180000) {benchmark::dump();Console.printf("[HEAP-COMPARE-DONE] FAILED stage=%u\n",hstage);hstage=99;}
    else switch(hstage) {
        case 1:
            if(hquiet && hnow-hstable>=1000) {
                benchmark::snapshot(hcycle,"live");benchmark::mark(benchmark::Map1Build);
                data_.map.scale=1;if(setScreen(ScreenId::Map))hstage=2;
            }break;
        case 2:
            if(hmap) {benchmark::snapshot(hcycle,"map1-ready");benchmark::mark(benchmark::Map1);hstable=hnow;hstage=3;}break;
        case 3:
            if(hmap && hnow-hstable>=1000) {benchmark::snapshot(hcycle,"map1-hold");benchmark::mark(benchmark::Map4Build);changeMapScale(4);hstage=4;}break;
        case 4:
            if(hmap) {benchmark::snapshot(hcycle,"map4-ready");benchmark::mark(benchmark::Map4);hstable=hnow;hstage=5;}break;
        case 5:
            if(hmap && hnow-hstable>=1000) {
                benchmark::snapshot(hcycle,"map4-hold");benchmark::mark(benchmark::LogMapBuild);
                auto& view=data_.editBrowse();view={};view.point_loaded=true;view.map_mode=true;view.point=data_.gps.fix;
                view.summary.records=1;view.summary.session_id=1;data_.map.scale=1;
                if(setScreen(ScreenId::LogPoint))hstage=6;
            }break;
        case 6:
            if(hmap) {benchmark::snapshot(hcycle,"log-map-ready");benchmark::mark(benchmark::LogMap);hstable=hnow;hstage=7;}break;
        case 7:
            if(hmap && hnow-hstable>=1000) {
                benchmark::snapshot(hcycle,"log-map-hold");benchmark::mark(benchmark::Text);
                UiAction toggle{};toggle.browse_action=BrowseAction::ToggleView;
                if(operateBrowser(toggle)) {hstage=8;hstable=0;}
            }break;
        case 8:
            if(hquiet && !map_session_.token) {
                if(!hstable)hstable=hnow;
                if(hnow-hstable>=1000) {
                    benchmark::snapshot(hcycle,"log-text");benchmark::mark(benchmark::List);
                    if(setScreen(ScreenId::Logs)) {
                        auto& logs=screen_resources_.logs();logs.request={};logs.request.kind=LogReadKind::List;logs.need=true;
                        data_.editBrowse().loading=true;++data_.revision;ui_data_dirty_=true;hstage=9;hstable=0;
                    }
                }
            }break;
        case 9:
            if(hquiet && !data_.browse().loading && !screen_resources_.logs().need) {
                if(!hstable)hstable=hnow;
                if(hnow-hstable>=1000) {
                    benchmark::snapshot(hcycle,"list");hrow=0;hstage=10;
                }
            }break;
        case 10:
            if(hquiet && !data_.browse().loading) {
                const auto& view=data_.browse();
                while(hrow<view.count && !view.list_records[hrow])++hrow;
                if(hrow==view.count) {Console.println("[HEAP-NOTE] no valid nonempty file on first page; file/statistics omitted");hstage=20;break;}
                UiAction open{};open.browse_action=BrowseAction::OpenFile;open.browse_index=hrow;open.browse_revision=view.revision;
                benchmark::mark(benchmark::FileOpen);if(operateBrowser(open))hstage=11;
            }break;
        case 11:
            if(hquiet && !data_.browse().loading && !screen_resources_.logs().need) {
                if(data_.browse().error!=LogReadError::None) {setScreen(ScreenId::Logs);++hrow;hstage=10;break;}
#if HEAP_COMPARISON_AFTER
                benchmark::snapshot(hcycle,"properties");benchmark::mark(benchmark::Properties);hstable=hnow;hstage=12;
#else
                Console.printf("[HEAP-FILE] name=%s records=%lu\n",data_.browse().name,(unsigned long)data_.browse().summary.records);
                benchmark::snapshot(hcycle,"point");benchmark::mark(benchmark::Point);hstable=hnow;hstage=14;
#endif
            }break;
#if HEAP_COMPARISON_AFTER
        case 12:
            if(hquiet && hnow-hstable>=1000) {
                benchmark::snapshot(hcycle,"properties-hold");UiAction open{};open.browse_action=BrowseAction::OpenPoint;
                benchmark::mark(benchmark::FileOpen);if(operateBrowser(open))hstage=13;
            }break;
        case 13:
            if(hquiet && !data_.browse().loading && !screen_resources_.logs().need) {
                Console.printf("[HEAP-FILE] name=%s records=%lu\n",data_.browse().name,(unsigned long)data_.browse().summary.records);
                benchmark::snapshot(hcycle,"point");benchmark::mark(benchmark::Point);hstable=hnow;hstage=14;
            }break;
#endif
        case 14:
            if(hquiet && hnow-hstable>=1000) {
                benchmark::snapshot(hcycle,"point-hold");
#if HEAP_COMPARISON_AFTER
                benchmark::mark(benchmark::Statistics);UiAction stats{};stats.browse_action=BrowseAction::Statistics;
                if(operateBrowser(stats))hstage=15;
#else
                hstage=20;
#endif
            }break;
#if HEAP_COMPARISON_AFTER
        case 15:
            if(hquiet && !data_.browse().loading && !screen_resources_.logs().need) {
                benchmark::snapshot(hcycle,"statistics-done");
                Console.printf("[HEAP-STATS] records=%lu error=%u\n",(unsigned long)data_.browse().statistics.processed,unsigned(data_.browse().error));
                benchmark::mark(benchmark::StatsResult);hstable=hnow;hstage=16;
            }break;
        case 16:
            if(hquiet && hnow-hstable>=1000) {benchmark::snapshot(hcycle,"stats-result");hstage=20;}break;
#endif
        case 20:
            benchmark::mark(benchmark::Return);if(setScreen(ScreenId::LivePlace)){hstage=21;hstable=0;}break;
        case 21:
            if(hquiet && !map_session_.token && (data_.place_state==PlaceState::Ready || data_.place_state==PlaceState::NotFound)) {
                if(!hstable)hstable=hnow;
                if(hnow-hstable>=1000) {
                    benchmark::snapshot(hcycle,"live-end");
                    if(++hcycle==3) {benchmark::dump();Console.println("[HEAP-COMPARE-DONE] OK read-only 3 cycles");hstage=99;}
                    else {benchmark::mark(benchmark::Live);hstage=1;hstable=hnow;}
                }
            }break;
    }
}

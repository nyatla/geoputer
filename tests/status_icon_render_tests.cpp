#include "task/ui/GpsUiRenderer.h"
#include "task/ui/GpsHeaderLayout.h"
#include "task/ui/AlarmBellIcon.h"
#include "task/ui/SdCardIcon.h"
#include "apps/common/PanelRenderer.h"
#include "task/ui/GpsControls.h"
#include "task/ui/PlacePresentation.h"
#include "task/ui/EscapeIcon.h"
#include "task/ui/EnterIcon.h"
#include "LogReplay.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
using namespace geoputer;
namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
constexpr int X=sd_icon::X,Y=sd_icon::Y,W=sd_icon::Width,H=sd_icon::Height;
static_assert(X-(gps_header::AlarmX+alarm_icon::Width)==2,"bell to SD has a two-pixel gap");
static_assert(180-(X+W)==2,"SD to battery has a two-pixel gap");
using Mask=std::array<bool,W*H>;
void checkHeaderName(const M5GFX& display,const GpsUiModel& model) {
    const auto* label=model.screenNameText();const int length=std::strlen(label);
    for(int n=0;n<length;++n)check(label[n]>=32 && label[n]<=126,"screen names stay ASCII in every UI language");
    for(int y=0;y<16;++y)for(int x=0;x<gps_header::IconsLeft;++x) {
        bool ink=false;const int column=x-gps_header::StatusX;
        if(y>=4 && y<12 && column>=0 && column<length*8)
            ink=font8x8_c64[(label[column/8]-32)*8+column%8]&(1u<<(y-4));
        check(display.pixels[y*240+x]==(ink?TFT_CYAN:TFT_BLACK),"header overlay contains only left-aligned ASCII text, without a frame or old status/clock pixels");
    }
}
Mask render(ScreenId screen,int page,bool active,bool japanese,int depth,bool dialog=false,bool output=false,int percent=100,bool charging=false,bool charge_known=true,bool loading=false,unsigned replay_speed=config::LogReplayDefaultSpeed,bool reverse=false,bool playing=false,bool alert=false,bool sounding=false,bool notice=false) {
    UiData data{};data.settings=defaultSettings();data.settings.language=japanese?UiLanguage::Japanese:UiLanguage::English;
    data.storage.sd={1,1,active};data.storage.card_state=CardState::Mounted;
    data.storage.settings_state=SettingsState::Loaded;data.storage.settings=data.settings;
    data.recording.state=RecordingState::Paused;
    data.gps.reception=GpsReception::Fix;data.gps.fix={35.68,140.02,0,true};
    if(screen==ScreenId::Settings)data.editDatasets().loading=loading;
    if(isLogBrowser(screen)||screen==ScreenId::UsbStorage) {
        auto& view=data.editBrowse();view.summary.records=3527;view.index=321;view.point=data.gps.fix;
        view.point_loaded=true;view.map_mode=page==-1;view.loading=loading;
        view.replay_speed=replay_speed;view.replay_reverse=reverse;view.playing=playing;
        std::strcpy(view.name,"20261026_120000_0.gps.dat");
    }
    if(screen==ScreenId::Logs) {
        auto& list=data.editBrowse();list.count=2;list.total_files=61;
        list.list_records[0]=3527;list.list_records[1]=99;
        std::strcpy(list.names[0],"20261026_120000_0.gps.dat");std::strcpy(list.names[1],"20261025_120000_0.gps.dat");
    }
    if(alert){data.settings.dataset=1;data.storage.alarm.dataset=1;data.storage.alarm.depth=4;data.storage.alarm.state=alarm::State::Ready;data.storage.alarm.settings.enabled=true;}
    GpsUiModel model;if(screen!=ScreenId::UsbStorage)check(model.setScreen({screen,DisplayMode::View,1,data,0,ActionResult::None})==OperationStatus::Ok,"test screen");
    if(screen==ScreenId::Settings)model.screen_resources_.settings.page=static_cast<SettingsPage>(page);
    model.setBattery(percent,charging,charge_known);model.setAudioActive(sounding);model.tick(notice?200:1200);
    if(dialog) {model.keyPressed(UiKey::GpsControl,1201);check(model.confirming(),"dialog must open");}
    M5GFX display;GpsUiRenderer renderer(display);
    alignas(4) uint8_t backing[240*135*2+4]{};
    check(renderer.begin(depth,backing,sizeof(backing)),"renderer begin");
    UsbUiModel usb;
    if(screen==ScreenId::UsbStorage){
        UsbUiData content{};content.settings.language=data.settings.language;content.storage.sd=data.storage.sd;
        content.storage.usb.ready=content.storage.usb.connected=true;content.storage.files.loading=loading;
        usb.update(content,0);usb.page_=static_cast<UsbPage>(page);usb.setBattery(percent,charging,charge_known);usb.tick(200);
        PanelRenderer panel(display);check(panel.begin(backing,sizeof(backing),depth),"USB renderer begin");panel.draw(usb);
    }else renderer.draw(model);
    if((hasRuntimeHeader(screen,data) || hasPlaybackHeader(screen,data)) && !notice) {
        std::array<uint16_t,gps_header::IconsLeft*16> expected{};
        auto label=[&](unsigned x,const char* value,uint16_t color) {
            for(unsigned n=0;value[n];++n)for(unsigned y=0;y<8;++y)for(unsigned col=0;col<8;++col) {
                const bool ink=font8x8_c64[(value[n]-32)*8+col]&(1u<<y);
                if(ink)expected[(4+y)*gps_header::IconsLeft+x+n*8+col]=dialog?TFT_DARKGREY:color;
            }
        };
        if(hasPlaybackHeader(screen,data)) {
            label(4,playing?"PLAY":"PAUSE",playing?TFT_GREEN:TFT_YELLOW);
            label(60,reverse?"<":">",TFT_WHITE);label(76,replay::speedLabel(data.browse()),TFT_WHITE);
        } else {label(4,"PAUSE",TFT_YELLOW);label(gps_header::ClockX,"09:00:01",TFT_WHITE);}
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<unsigned(gps_header::IconsLeft);++x)
            check(display.pixels[y*240+x]==expected[y*gps_header::IconsLeft+x],"root header shows status/time; log-map header only shows playback/direction/rate, without timestamps");
    }
    if(screen!=ScreenId::UsbStorage)for(unsigned y=0;y<alarm_icon::Height;++y)for(unsigned x=0;x<alarm_icon::Width;++x){
        const bool ink=alert&&(alarm_icon::Rows[y]&(1u<<(alarm_icon::Width-1-x)));
        check(display.pixels[(gps_header::AlarmY+y)*240+gps_header::AlarmX+x]==(ink?(sounding?TFT_YELLOW:TFT_WHITE):TFT_BLACK),"ROM bell is white when armed, yellow for actual alerts, absent when off, without overlapping adjacent fields");
    }
    if(model.screenNameVisible())checkHeaderName(display,model);
    const auto shortcuts=screen==ScreenId::UsbStorage?usb.shortcutGuide():model.shortcutGuide();
    for(unsigned i=0;i<shortcuts.size();++i)if(shortcuts[i].key==UiKey::Escape)
        check(shortcuts[i].tone!=ShortcutTone::Accent,"ESC is white when available, grey when disabled; never cyan");
    static constexpr uint8_t arrows[4][8]={
        {0x10,0x38,0x54,0x92,0x10,0x10,0x10,0}, // up
        {0x10,0x10,0x10,0x92,0x54,0x38,0x10,0}, // down
        {0x10,0x20,0x40,0xfe,0x40,0x20,0x10,0}, // left
        {0x10,0x08,0x04,0xfe,0x04,0x08,0x10,0}, // right
    };
    for(unsigned y=121;y<135;++y)for(unsigned x=0;x<240;++x) {
        uint16_t expected=TFT_BLACK;
        unsigned left=4;
        for(unsigned i=0;i<shortcuts.size();++i) {
            const auto hint=shortcuts[i];const auto width=shortcutWidth(hint.key);
            if(x<left || x>=left+width) {left+=width;continue;}
            const auto col=x-left;
            const auto c=shortcutCharacter(hint.key);bool ink=false;
            if(hint.key==UiKey::Escape)ink=y>=122 && y<134 && (escape_icon::Rows[y-122]&(0x800000u>>col));
            else if(hint.key==UiKey::Enter)ink=y>=122 && y<134 && (enter_icon::Rows[y-122]&((1u<<(enter_icon::Width-1))>>col));
            else if(c)ink=y>=124 && y<132 && (font8x8_c64[(c-32)*8+col]&(1u<<(y-124)));
            else {
                const int index=hint.key==UiKey::Up?0:hint.key==UiKey::Down?1:hint.key==UiKey::Left?2:3;
                ink=y>=124 && y<132 && (arrows[index][y-124]&(0x80u>>col));
            }
            if(ink)expected=hint.tone==ShortcutTone::Disabled?TFT_DARKGREY:hint.tone==ShortcutTone::Accent?TFT_CYAN:TFT_WHITE;
            break;
        }
        check(display.pixels[y*240+x]==expected,"common footer: ESC bitmap, groups, key colors and blank remainder");
    }
    Mask mask{};
    const uint16_t expected=active?TFT_WHITE:TFT_DARKGREY;
    for(int y=0;y<16;++y)for(int x=X-2;x<X+W+2;++x) {
        const auto pixel=display.pixels[y*240+x];
        const bool inside=x>=X&&x<X+W&&y>=Y&&y<Y+H;
        check(pixel==TFT_BLACK||(inside&&(pixel==expected || (active&&pixel==TFT_BLUE))),"reserved SD area contains foreign header text or a dimmed icon");
        if(inside)mask[(y-Y)*W+x-X]=pixel==expected;
    }
    const auto frame_color=charging&&charge_known?TFT_CYAN:TFT_WHITE;
    check(display.pixels[3*240+180]==frame_color && display.pixels[6*240+198]==frame_color,"common battery frame/charging color");
    const auto fill=percent>=0&&percent<=10?TFT_RED:percent>=0&&percent<=20?TFT_YELLOW:TFT_GREEN;
    const int bars=percent>0?(percent+24)/25:0;
    for(int n=0;n<4;++n)check(display.pixels[5*240+182+n*4]==(n<bars?fill:TFT_BLACK),"common battery bars");
    char value[16];if(percent<0)std::strcpy(value,"--%");else std::snprintf(value,sizeof(value),"%d%%",percent);
    const int left=236-8*std::strlen(value);
    const auto text_color=percent>=0&&percent<=20?fill:TFT_WHITE;
    for(unsigned n=0;value[n];++n)for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
        const bool ink=font8x8_c64[(value[n]-32)*8+x]&(1u<<y);
        check(display.pixels[(4+y)*240+left+n*8+x]==(ink?text_color:TFT_BLACK),"right-aligned common battery percentage");
    }
    if(loading && screen==ScreenId::LogPoint && page!=-1)
        for(int y=75;y<83;++y)for(int x=124;x<232;++x)check(display.pixels[y*240+x]==TFT_BLACK,"READING must be replaced by SD access");
    if(loading && screen==ScreenId::UsbStorage && page==static_cast<int>(UsbPage::Files))
        for(int y=56;y<64;++y)for(int x=12;x<112;++x)check(display.pixels[y*240+x]==TFT_BLACK,"LOADING must be replaced by SD access");
    if(loading && screen==ScreenId::Settings && page==static_cast<int>(SettingsPage::DatasetList))
        for(int y=27;y<43;++y)for(int x=4;x<180;++x)check(display.pixels[y*240+x]==TFT_BLACK,"dataset loading must be replaced by SD access");
    check(!mask[0] && mask[3] && mask[11] && mask[11*W],"SD silhouette corner and body");
    check(mask[5*W+2] && mask[5*W+4] && mask[6*W+6] && mask[6*W+8],"small SD lettering");
    check(!mask[3*W+3] && !mask[3*W+5] && !mask[3*W+7],"three SD contacts");
    auto ascii=[&](int left,int top,const char* value,uint16_t color) {
        for(unsigned n=0;value[n];++n)for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
            const bool ink=font8x8_c64[(value[n]-32)*8+x]&(1u<<y);
            check(display.pixels[(top+y)*240+left+n*8+x]==(ink?color:TFT_BLACK),"log list filename/footer pixels");
        }
    };
    if(screen==ScreenId::Logs && !loading && !dialog) {
        ascii(20,27,"20261026_120000_0",TFT_CYAN);
        ascii(4,106,"1/11",TFT_LIGHTGREY);ascii(172,106,"3527 PTS",TFT_LIGHTGREY);
        if(!sounding){model.keyPressed(UiKey::Down,201);renderer.draw(model);
        ascii(188,106,"99 PTS",TFT_LIGHTGREY);}
    }
    if(output) {
        std::printf("P6\n240 135\n255\n");
        for(auto p:display.pixels) {
            const unsigned red=(p>>11)&31,green=(p>>5)&63,blue=p&31;
            std::putchar((red<<3)|(red>>2));std::putchar((green<<2)|(green>>4));std::putchar((blue<<3)|(blue>>2));
        }
    }
    return mask;
}
}
// A completed label must remain pixel-identical across the next point's
// Waiting/Searching states. The normal off-screen canvas is the only renderer
// buffer; snapshots below exist solely in this host regression test.
void replayLabelContinuity(bool map_mode,bool japanese,int depth) {
    UiData data{};data.settings=defaultSettings();data.settings.dataset=1;
    data.settings.language=japanese?UiLanguage::Japanese:UiLanguage::English;
    data.storage.card_state=CardState::Mounted;data.dataset.state=PlaceState::Ready;
    data.place_state=PlaceState::Ready;
    std::strcpy(data.place[0],"OLD REGION");std::strcpy(data.place[1],"OLD DISTRICT");
    auto& view=data.editBrowse();view.summary.records=2;view.point_loaded=true;view.map_mode=map_mode;
    view.playing=map_mode;view.point={35.68,140.02,0,true};
    GpsUiModel model;check(model.setScreen({ScreenId::LogPoint,DisplayMode::View,1,data,0,ActionResult::None})==OperationStatus::Ok,"place continuity initial screen");
    M5GFX display;GpsUiRenderer renderer(display);alignas(4) uint8_t backing[240*135*2+4]{};
    check(renderer.begin(depth,backing,sizeof(backing)),"place continuity renderer begin");renderer.draw(model);
    constexpr unsigned First=84*240,End=116*240;
    std::array<uint16_t,End-First> old{};
    std::copy(display.pixels.begin()+First,display.pixels.begin()+End,old.begin());
    check(std::any_of(old.begin(),old.end(),[](uint16_t color){return color!=TFT_BLACK;}),"continuity fixture actually draws the old label");
    view.index=view.target=1;view.point.latitude+=.01;
    for(auto state:{PlaceState::Waiting,PlaceState::Searching}) {
        data.place_state=state;++data.revision;
        check(model.updateContent({DisplayMode::View,1,data,0,ActionResult::None})==OperationStatus::Ok,"place continuity updates current point");
        renderer.draw(model);
        check(std::equal(old.begin(),old.end(),display.pixels.begin()+First),"place rows must not blink or disappear while the next point is searched");
    }
    PlaceResult result{};result.state=PlaceState::Ready;result.request.fix=view.point;
    std::strcpy(result.lines[0],"NEW REGION");std::strcpy(result.lines[1],"NEW DISTRICT");
    completePlacePresentation(data,result);++data.revision;
    check(model.updateContent({DisplayMode::View,1,data,0,ActionResult::None})==OperationStatus::Ok,"place continuity accepts completed labels");
    renderer.draw(model);
    check(!std::equal(old.begin(),old.end(),display.pixels.begin()+First),"completed lookup replaces the label instead of freezing the old name");
}

void headerOverlayContinuity(ScreenId screen,bool japanese,int depth) {
    UiData data{};data.settings=defaultSettings();data.settings.language=japanese?UiLanguage::Japanese:UiLanguage::English;
    data.storage.card_state=CardState::Mounted;data.recording.state=RecordingState::Paused;
    GpsUiModel model;check(model.setScreen({screen,DisplayMode::View,1,data,0,ActionResult::None})==OperationStatus::Ok,"overlay fixture screen");
    model.setBattery(72,false,true);model.tick(200);
    M5GFX display;GpsUiRenderer renderer(display);alignas(4) uint8_t backing[240*135*2+4]{};
    check(renderer.begin(depth,backing,sizeof(backing)),"overlay fixture begin");renderer.draw(model);
    const auto during=display.pixels;check(model.screenNameVisible(),"top overlay initially visible");
    checkHeaderName(display,model);
    model.tick(1200);check(!model.screenNameVisible()&&model.takeRedraw(),"overlay expires and requests redraw");renderer.draw(model);
    check(std::equal(during.begin()+16*240,during.end(),display.pixels.begin()+16*240),"overlay never covers or changes body/footer pixels");
    for(int y=0;y<16;++y)for(int x=gps_header::IconsLeft;x<240;++x)
        check(during[y*240+x]==display.pixels[y*240+x],"bell, SD and battery remain visible through the overlay");
}

int main(int argc,char** argv) {
    try {
        if(argc==3) {
            const bool active=std::atoi(argv[2])!=0;const char* screen=argv[1];
            const bool notice=std::strstr(screen,"notice-")!=nullptr,alert=notice||std::strstr(screen,"alert-")!=nullptr;
            const auto selected=std::strstr(screen,"usb")?ScreenId::UsbStorage:std::strstr(screen,"settings")?ScreenId::Settings:
                std::strstr(screen,"gps")?ScreenId::GpsLogger:std::strstr(screen,"logs")?ScreenId::Logs:std::strstr(screen,"map")?ScreenId::Map:ScreenId::LivePlace;
            render(selected,0,active,std::strstr(screen,"-ja")!=nullptr,4,false,true,72,false,true,false,
                config::LogReplayDefaultSpeed,false,false,alert,false,notice);
            return 0;
        }
        const auto reference=render(ScreenId::LivePlace,0,false,false,4);
        unsigned cases=0;
        for(bool ja:{false,true})for(int depth:{4,16})for(bool active:{false,true}) {
            for(auto screen:{ScreenId::LivePlace,ScreenId::Map,ScreenId::GpsLogger,ScreenId::Logs,ScreenId::LogPoint,ScreenId::LogProperties,ScreenId::LogStatistics,ScreenId::Menu,ScreenId::Boot}) {
                check(render(screen,0,active,ja,depth)==reference,"screen changes the card shape");++cases;
            }
            check(render(ScreenId::LogPoint,-1,active,ja,depth)==reference,"log map changes the card shape");++cases;
            for(unsigned page=0;page<=static_cast<unsigned>(SettingsPage::Alarm);++page) {
                check(render(ScreenId::Settings,page,active,ja,depth)==reference,"settings page changes the card shape");++cases;
            }
            for(unsigned page=0;page<=static_cast<unsigned>(UsbPage::Busy);++page) {
                check(render(ScreenId::UsbStorage,page,active,ja,depth)==reference,"USB page changes the card shape");++cases;
            }
            check(render(ScreenId::LivePlace,0,active,ja,depth,true)==reference,"dialog changes brightness of access icon");++cases;
        }
        for(bool ja:{false,true})for(int depth:{4,16})for(bool playing:{false,true})
            for(bool reverse:{false,true})for(unsigned rate=0;rate<replay::SpeedCount;++rate) {
                check(render(ScreenId::LogPoint,-1,true,ja,depth,false,false,72,false,true,false,rate,reverse,playing)==reference,"replay controls change SD icon");++cases;
            }
        for(int percent:{-1,0,10,20,72,100})for(bool charging:{false,true})
            for(auto screen:{ScreenId::LivePlace,ScreenId::GpsLogger,ScreenId::Settings,ScreenId::UsbStorage}) {
                check(render(screen,0,true,false,4,false,false,percent,charging)==reference,"battery state changes the SD shape");++cases;
            }
        for(auto screen:{ScreenId::LivePlace,ScreenId::GpsLogger,ScreenId::Settings,ScreenId::UsbStorage}) {
            check(render(screen,0,false,false,4,false,false,72,true,false)==reference,"unknown charging state changes SD shape");++cases;
        }
        for(auto screen:{ScreenId::LogPoint,ScreenId::UsbStorage,ScreenId::Settings}) {
            const int page=screen==ScreenId::UsbStorage?static_cast<int>(UsbPage::Files):screen==ScreenId::Settings?static_cast<int>(SettingsPage::DatasetList):0;
            check(render(screen,page,true,false,4,false,false,72,false,true,true)==reference,"loading changes common status");++cases;
        }
        for(auto screen:{ScreenId::LivePlace,ScreenId::Map,ScreenId::GpsLogger,ScreenId::Logs,ScreenId::LogProperties,ScreenId::LogStatistics,ScreenId::Settings})
            for(bool ja:{false,true})for(int depth:{4,16})for(bool sounding:{false,true}){
                check(render(screen,0,true,ja,depth,false,false,72,false,true,false,config::LogReplayDefaultSpeed,false,false,true,sounding)==reference,"armed/ringing bell preserves SD icon");++cases;
            }
        for(auto screen:{ScreenId::LivePlace,ScreenId::Map,ScreenId::GpsLogger,ScreenId::Logs})
            for(bool ja:{false,true})for(int depth:{4,16})headerOverlayContinuity(screen,ja,depth);
        std::puts("PASS: 16 header-only overlay cases; body/footer and status icons remain pixel-identical across expiry");
        for(bool map_mode:{false,true})for(bool ja:{false,true})for(int depth:{4,16})replayLabelContinuity(map_mode,ja,depth);
        std::puts("PASS: 8 log label continuity cases; Ready/Waiting/Searching pixel identity and completed replacement, text/map, both languages and color depths");
        std::printf("PASS: %u renderer cases; common shortcut footer and SD/battery header on all screens/pages, battery thresholds/charging/unknown, loading, dialogs, languages and 4/16-bit pixels\n",cases);
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}

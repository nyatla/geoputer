#include "task/ui/UiRenderer.h"
#include "task/ui/GpsControls.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
using namespace geoputer;
namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
constexpr int X=160,Y=2,W=12,H=12;
using Mask=std::array<bool,W*H>;
Mask render(ScreenId screen,int page,bool active,bool japanese,int depth,bool dialog=false,bool output=false,int percent=100,bool charging=false,bool charge_known=true,bool loading=false) {
    UiData data{};data.settings=defaultSettings();data.settings.language=japanese?UiLanguage::Japanese:UiLanguage::English;
    data.storage.sd={1,1,active};data.storage.card_state=CardState::Mounted;
    data.storage.settings_state=SettingsState::Loaded;data.storage.settings=data.settings;
    data.storage.usb.ready=true;data.storage.usb.connected=true;
    data.recording.state=RecordingState::Paused;
    data.gps.reception=GpsReception::Fix;data.gps.fix={35.68,140.02,0,true};
    if(screen==ScreenId::Settings)data.editDatasets().loading=loading;
    if(isLogBrowser(screen)||screen==ScreenId::UsbStorage) {
        auto& view=data.editBrowse();view.summary.records=3527;view.index=321;view.point=data.gps.fix;
        view.point_loaded=true;view.map_mode=page==-1;view.loading=loading;
        std::strcpy(view.name,"20261026_120000_0.gps.dat");
    }
    if(screen==ScreenId::Logs) {
        auto& list=data.editBrowse();list.count=2;list.total_files=61;
        list.list_records[0]=3527;list.list_records[1]=99;
        std::strcpy(list.names[0],"20261026_120000_0.gps.dat");std::strcpy(list.names[1],"20261025_120000_0.gps.dat");
    }
    UiModel model;check(model.setScreen({screen,DisplayMode::View,1,data,0,ActionResult::None})==OperationStatus::Ok,"test screen");
    if(screen==ScreenId::Settings)model.screen_resources_.settings.page=static_cast<SettingsPage>(page);
    if(screen==ScreenId::UsbStorage)model.screen_resources_.usb.page=static_cast<UsbPage>(page);
    model.setBattery(percent,charging,charge_known);model.tick(200);
    if(dialog) {model.keyPressed(UiKey::GpsControl,201);check(model.confirming(),"dialog must open");}
    M5GFX display;UiRenderer renderer(display);
    alignas(4) uint8_t backing[240*135*2+4]{};
    check(renderer.begin(depth,backing,sizeof(backing)),"renderer begin");renderer.draw(model);
    Mask mask{};
    const uint16_t expected=active?TFT_WHITE:TFT_DARKGREY;
    for(int y=0;y<16;++y)for(int x=156;x<176;++x) {
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
        model.keyPressed(UiKey::Down,201);renderer.draw(model);
        ascii(188,106,"99 PTS",TFT_LIGHTGREY);
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
int main(int argc,char** argv) {
    try {
        if(argc==3) {
            const bool active=std::atoi(argv[2])!=0;const char* screen=argv[1];
            render(!std::strcmp(screen,"usb")?ScreenId::UsbStorage:!std::strcmp(screen,"settings")?ScreenId::Settings:ScreenId::LivePlace,0,active,false,4,false,true);
            return 0;
        }
        const auto reference=render(ScreenId::LivePlace,0,false,false,4);
        unsigned cases=0;
        for(bool ja:{false,true})for(int depth:{4,16})for(bool active:{false,true}) {
            for(auto screen:{ScreenId::LivePlace,ScreenId::Map,ScreenId::GpsLogger,ScreenId::Logs,ScreenId::LogPoint,ScreenId::LogProperties,ScreenId::LogStatistics,ScreenId::Menu,ScreenId::Boot}) {
                check(render(screen,0,active,ja,depth)==reference,"screen changes the card shape");++cases;
            }
            check(render(ScreenId::LogPoint,-1,active,ja,depth)==reference,"log map changes the card shape");++cases;
            for(unsigned page=0;page<=static_cast<unsigned>(SettingsPage::System);++page) {
                check(render(ScreenId::Settings,page,active,ja,depth)==reference,"settings page changes the card shape");++cases;
            }
            for(unsigned page=0;page<=static_cast<unsigned>(UsbPage::Busy);++page) {
                check(render(ScreenId::UsbStorage,page,active,ja,depth)==reference,"USB page changes the card shape");++cases;
            }
            check(render(ScreenId::LivePlace,0,active,ja,depth,true)==reference,"dialog changes brightness of access icon");++cases;
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
        std::printf("PASS: %u renderer cases; common SD/battery header on all screens/pages, battery thresholds/charging/unknown, loading, dialogs, languages and 4/16-bit pixels\n",cases);
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}

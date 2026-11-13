#include "PanelRenderer.h"
#include "../../task/ui/SdCardIcon.h"
#include "../../task/ui/EscapeIcon.h"
#include "../../task/ui/EnterIcon.h"
#include <algorithm>
#include <cstdio>
namespace geoputer {
constexpr int ViewHeaderTextY=4;
void PanelRenderer::drawUsb(const UsbUiModel& model) {
    const auto page=model.usbPage();
    const auto& data=model.data();const auto& usb=data.storage.usb;
    const auto& job=data.storage.export_job;const auto& files=data.browse();
    const bool ja=data.settings.language==UiLanguage::Japanese;
    const auto selected=model.snapshot().selected_setting;
    auto large=[&](int x,int y,const char* value,uint16_t color=TFT_WHITE) {
        font_.draw(frame_,x,y,value,TextRole::Ascii8x16,color);
    };
    auto centered=[&](int y,const char* value,uint16_t color=TFT_WHITE) {
        large((240-PixelFont::width(value,TextRole::Ascii8x16))/2,y,value,color);
    };
    auto shell=[&](const char* title) {
        text(4,8,title);frame_.drawFastHLine(0,22,240,TFT_DARKGREY);
        frame_.drawFastHLine(0,118,240,TFT_DARKGREY);
    };
    frame_.setDimmed(model.confirming() || page==UsbPage::WaitEject);
    frame_.fillSprite(TFT_BLACK);
    char line[80];
    if(page==UsbPage::Menu || page==UsbPage::WaitEject) {
        shell("USB");
        const char* state=!usb.ready?"NO SD":usb.io_error?"SD ERROR":"SD READY";
        text(sd_icon::HeaderRight-8*std::strlen(state),8,state,!usb.ready||usb.io_error?TFT_RED:TFT_GREEN);
        large(12,31,ja?(usb.ejected?"取り外し済み":usb.connected?"接続中":"未接続"):
                       (usb.ejected?"Safely ejected":usb.connected?"PC connected":"Not connected"),
              usb.connected&&!usb.ejected?TFT_GREEN:TFT_YELLOW);
        std::snprintf(line,sizeof(line),"%.28s",data.message[0]?data.message:"GPX / KML / CSV");
        text(12,53,line,data.message[0]?TFT_RED:TFT_LIGHTGREY);
        text(4,selected?100:76,">",TFT_CYAN);
        large(20,72,ja?"ログを書き出す":"Export logs",!usb.ready?TFT_DARKGREY:!selected?TFT_CYAN:TFT_WHITE);
        large(20,96,ja?"ロガーへ戻る":"Return to logger",selected?TFT_CYAN:TFT_WHITE);
        frame_.setDimmed(false);
        if(page==UsbPage::WaitEject) {
            frame_.fillRect(8,34,224,73,TFT_BLACK);
            frame_.drawFastHLine(8,34,224,TFT_WHITE);frame_.drawFastHLine(8,106,224,TFT_WHITE);
            centered(40,ja?"PCで安全に取り外し":"Eject safely on PC",TFT_YELLOW);
            centered(60,ja?"取り外し後に自動で進む":"Then continue automatically");
            const char* cancel=ja?"取消":"Cancel";
            const int x=(240-PixelFont::width(cancel,TextRole::Ascii8x16))/2;
            text(x-16,88,">",TFT_CYAN);large(x,84,cancel,TFT_CYAN);

        }
        return;
    }
    if(page==UsbPage::Files) {
        shell("EXPORT");
        std::snprintf(line,sizeof(line),"%u/%u",files.count?selected+1:0,files.count);text(sd_icon::HeaderRight-8*std::strlen(line),8,line,TFT_LIGHTGREY);
        large(12,28,ja?"ログを選択":"Select a log");
        if(!files.loading && files.error!=LogReadError::None)text(4,56,logReadErrorName(files.error),TFT_RED);
        else if(!files.loading && !files.count)large(12,56,ja?"ログがありません":"No logs found",TFT_LIGHTGREY);
        else if(!files.loading)for(unsigned n=0;n<files.count && n<4;++n) {
            if(n==selected)text(4,52+n*18,">",TFT_CYAN);
            char name[28];std::snprintf(name,sizeof(name),"%.27s",files.names[n]);
            text(20,52+n*18,name,n==selected?TFT_CYAN:TFT_WHITE);
        }
    } else if(page==UsbPage::Format) {
        shell("EXPORT");
        std::snprintf(line,sizeof(line),"%u/3",selected+1);text(sd_icon::HeaderRight-24,8,line,TFT_LIGHTGREY);
        large(12,28,ja?"形式を選択":"Select a format");
        const char* name=model.usbFile()<files.count?files.names[model.usbFile()]:"";
        std::snprintf(line,sizeof(line),"%.29s",name);text(4,49,line,TFT_LIGHTGREY);
        if(std::strlen(name)>29)text(4,59,name+29,TFT_LIGHTGREY);
        for(unsigned n=0;n<3;++n) {
            if(n==selected)text(4,74+n*16,">",TFT_CYAN);
            large(20,70+n*16,exportFormatName(static_cast<ExportFormat>(n)),n==selected?TFT_CYAN:TFT_WHITE);
        }
    } else if(page==UsbPage::Progress || page==UsbPage::Busy) {
        shell("EXPORT");
        text(sd_icon::HeaderRight-24,8,exportFormatName(job.format),TFT_CYAN);
        large(12,31,ja?(page==UsbPage::Progress?"書き出し中":"準備中"):
                             (page==UsbPage::Progress?"Exporting":"Preparing"),TFT_YELLOW);
        std::snprintf(line,sizeof(line),"%.29s",job.name);text(4,53,line);
        if(std::strlen(job.name)>29)text(4,63,job.name+29);
        if(page==UsbPage::Progress) {
            const unsigned percent=job.total?static_cast<uint64_t>(job.processed)*100/job.total:0;
            std::snprintf(line,sizeof(line),"%lu / %lu",static_cast<unsigned long>(job.processed),static_cast<unsigned long>(job.total));
            text(12,78,line);
            std::snprintf(line,sizeof(line),"%u%%",percent);large(236-PixelFont::width(line,TextRole::Ascii8x16),74,line,TFT_GREEN);
            frame_.drawRect(12,98,216,8,TFT_DARKGREY);frame_.fillRect(13,99,214*std::min(percent,100u)/100,6,TFT_GREEN);
        }
    } else if(page==UsbPage::Complete) {
        shell("EXPORT");text(sd_icon::HeaderRight-24,8,exportFormatName(job.format),TFT_CYAN);
        large(12,31,ja?"書き出し完了":"Export complete",TFT_GREEN);
        text(12,55,"/geoputer/export/",TFT_LIGHTGREY);
        char name[ExportNameBytes];std::snprintf(name,sizeof(name),"%s%s",job.name,exportExtension(job.format));
        std::snprintf(line,sizeof(line),"%.29s",name);text(4,69,line);
        if(std::strlen(name)>29)text(4,79,name+29);
        large(12,96,ja?(usb.connected?"PCへ公開しました":"USB接続待ち"):
                            (usb.connected?"Available on PC":"Waiting for USB"),usb.connected?TFT_CYAN:TFT_YELLOW);
    } else {
        shell("EXPORT");
        large(12,31,job.output_created?(ja?"出力済み・公開失敗":"Saved; USB unavailable"):
                                        (ja?"書き出し失敗":"Export failed"),TFT_RED);
        text(4,57,exportErrorName(job.error),TFT_RED);
        std::snprintf(line,sizeof(line),"%.29s",job.name);text(4,75,line,TFT_LIGHTGREY);
        if(std::strlen(job.name)>29)text(4,85,job.name+29,TFT_LIGHTGREY);
        if(job.error==ExportError::Corrupt) {
            std::snprintf(line,sizeof(line),"RECORD: %lu",static_cast<unsigned long>(job.failed_record));text(12,96,line);
        }
    }
}
void PanelRenderer::drawWifi(const WifiUiModel& model) {
    const auto& data=model.data();const auto& wifi=data.storage.wifi;const auto page=model.wifiPage();
    const bool ja=data.settings.language==UiLanguage::Japanese;
    text(4,ViewHeaderTextY,"WI-FI FILES");
    char number[8];std::snprintf(number,sizeof(number),"%u/3",static_cast<unsigned>(page)+1);text(128,ViewHeaderTextY,number,TFT_LIGHTGREY);
    if(page==WifiPage::Text) {
        text(4,25,"SSID",TFT_LIGHTGREY);font_.draw(frame_,4,36,wifi.ssid,TextRole::Ascii8x16,TFT_CYAN);
        text(4,56,"PASSWORD",TFT_LIGHTGREY);font_.draw(frame_,4,67,wifi.password,TextRole::Ascii8x16,TFT_WHITE);
        text(4,88,wifi.url,TFT_CYAN);
        char state[40];std::snprintf(state,sizeof(state),"%s / %u CLIENT / %s",wifiPhaseName(wifi.phase),wifi.clients,
            data.storage.card_state==CardState::Mounted?"SD OK":"NO SD");
        text(4,103,wifi.error!=WifiError::None?wifiErrorName(wifi.error):data.message[0] && wifi.phase!=WifiPhase::Failed?data.message:state,wifi.phase==WifiPhase::Failed?TFT_RED:TFT_LIGHTGREY);
        return;
    }
    if(wifi_qr_ && wifi_qr_->valid()) {
        constexpr int left=4,top=18;
        frame_.fillRect(left,top,wifi::QrCode::Area,wifi::QrCode::Area,TFT_WHITE);
        const auto cell=wifi_qr_->cell(),offset=wifi_qr_->offset();
        for(unsigned y=0;y<wifi_qr_->size();++y)for(unsigned x=0;x<wifi_qr_->size();++x)
            if(wifi_qr_->dark(x,y))frame_.fillRect(left+offset+x*cell,top+offset+y*cell,cell,cell,TFT_BLACK);
    } else {text(12,55,wifi.phase==WifiPhase::Starting?"WAITING":"NO QR",TFT_LIGHTGREY);}
    const bool network=page==WifiPage::ConnectionQr;
    text(110,25,network?"CONNECT QR":"WEB QR",TFT_CYAN);
    font_.draw(frame_,110,40,network?(ja?"QRで接続":"Scan to join"):(ja?"管理画面":"Open browser"),
        ja?TextRole::UiLarge:TextRole::Ascii8x16,TFT_WHITE);
    if(network) {
        text(110,64,"SSID",TFT_LIGHTGREY);
        char prefix[16]{};std::strncpy(prefix,wifi.ssid,15);font_.draw(frame_,110,74,prefix,TextRole::Ascii8x16,TFT_WHITE);
        if(std::strlen(wifi.ssid)>15)font_.draw(frame_,110,90,wifi.ssid+15,TextRole::Ascii8x16,TFT_WHITE);
    } else {
        text(110,65,"HTTP",TFT_LIGHTGREY);
        const auto* host=std::strstr(wifi.url,"://");font_.draw(frame_,110,77,host?host+3:wifi.url,TextRole::Ascii8x16,TFT_WHITE);
        text(110,95,wifi.portal?"PORTAL READY":"SCAN TO OPEN",TFT_LIGHTGREY);
    }
    const auto* state=wifi.phase==WifiPhase::Stopping?"RESTARTING...":wifi.phase==WifiPhase::Failed?"ERROR / TEXT >":wifi.clients?"CONNECTED":"NOT CONNECTED";
    text(110,109,state,wifi.phase==WifiPhase::Failed?TFT_RED:wifi.clients?TFT_GREEN:TFT_LIGHTGREY);
}
void PanelRenderer::header(BatteryStatus battery,bool active) {
    frame_.fillRect(sd_icon::ReservedLeft,0,240-sd_icon::ReservedLeft,16,TFT_BLACK);
    for(int y=0;y<sd_icon::Height;++y)
        for(int x=0;x<sd_icon::Width;++x)
            if(sd_icon::Rows[y]&(1u<<(sd_icon::Width-1-x)))
                frame_.drawPixel(sd_icon::X+x,sd_icon::Y+y,
                    sd_icon::InkRows[y]&(1u<<(sd_icon::Width-1-x))?
                        (active?TFT_WHITE:TFT_DARKGREY):(active?TFT_BLUE:TFT_BLACK));
    char line[8];
    const uint16_t battery_frame = battery.charge_known && battery.charging ? TFT_CYAN : TFT_WHITE;
    const uint16_t battery_fill = battery.percent >= 0 && battery.percent <= 10 ? TFT_RED :
        battery.percent >= 0 && battery.percent <= 20 ? TFT_YELLOW : TFT_GREEN;
    frame_.drawRect(180, 3, 18, 10, battery_frame);
    frame_.fillRect(198, 6, 2, 4, battery_frame);
    if (battery.percent >= 0) {
        const unsigned bars = battery.percent == 0 ? 0 : (battery.percent + 24) / 25;
        for (unsigned i = 0; i < bars; ++i) frame_.fillRect(182 + i * 4, 5, 3, 6, battery_fill);
        std::snprintf(line, sizeof(line), "%d%%", battery.percent);
    } else std::snprintf(line, sizeof(line), "--%%");
    valueText(204,236,4,line,battery.percent >= 0 && battery.percent <= 20 ? battery_fill : TFT_WHITE);
}
void PanelRenderer::arrow(int x, int y, UiKey direction, bool enabled) {
    const auto color = enabled ? TFT_WHITE : TFT_DARKGREY;
    auto pixel = [&](int col,int row) {
        if(direction==UiKey::Down) row=6-row;
        else if(direction==UiKey::Left) std::swap(col,row);
        else if(direction==UiKey::Right) { std::swap(col,row);col=6-col; }
        frame_.drawPixel(x+col,y+row,color);
    };
    for(int i=0;i<7;++i)pixel(3,i);
    for(int i=1;i<4;++i){pixel(3-i,i);pixel(3+i,i);}
}

void PanelRenderer::dialog(const ChoiceDialog& d,UiLanguage language){
 const bool ja=language==UiLanguage::Japanese;const auto role=ja?TextRole::UiLarge:TextRole::Ascii8x16;
 const char* title=d.kind()==DialogKind::RegenerateWifi?(ja?"パスワード再生成？":"Renew password?"):d.kind()==DialogKind::ExitWifiMode?(ja?"Wi-Fiを終了？":"Exit Wi-Fi mode?"):(ja?"USBモードを終了？":"Exit USB mode?");
 const char* detail=d.kind()==DialogKind::RegenerateWifi?(ja?"接続を切り再起動します":"Disconnect and restart Wi-Fi"):d.kind()==DialogKind::ExitWifiMode?(ja?"転送を中断して再起動":"Stop transfers and restart"):(ja?"PCの保存完了後に終了":"Finish PC saves first");
 const int top=34;frame_.fillRect(8,top,224,73,TFT_BLACK);frame_.drawFastHLine(8,top,224,TFT_WHITE);frame_.drawFastHLine(8,106,224,TFT_WHITE);
 font_.draw(frame_,(240-PixelFont::width(title,role))/2,40,title,role,TFT_YELLOW);font_.draw(frame_,(240-PixelFont::width(detail,role))/2,60,detail,role,TFT_WHITE);
 font_.draw(frame_,(d.selected()?152:56)-(ja?24:16),84,">",role,TFT_CYAN);font_.draw(frame_,56,84,ja?"取消":"Cancel",role,d.selected()?TFT_WHITE:TFT_CYAN);
 font_.draw(frame_,152,84,d.kind()==DialogKind::RegenerateWifi?(ja?"再生成":"Renew"):(ja?"終了":"Exit"),role,d.selected()?TFT_CYAN:TFT_WHITE);
}
template<class Model>void PanelRenderer::present(const Model& model,bool /*isWifi*/){
 frame_.setDimmed(false);if(model.confirming())dialog(model.dialog(),model.data().settings.language);
 frame_.fillRect(0,121,240,14,TFT_BLACK);
    const auto row=model.shortcutGuide();
    int x=4;
    for(unsigned i=0;i<row.size();++i) {
        const auto hint=row[i];
        const auto color=hint.tone==ShortcutTone::Disabled?TFT_DARKGREY:
                         hint.tone==ShortcutTone::Accent?TFT_CYAN:TFT_WHITE;
        if(hint.key==UiKey::Escape) {
            for(unsigned y=0;y<escape_icon::Height;++y)for(unsigned col=0;col<escape_icon::Width;++col)
                if(escape_icon::Rows[y]&((1u<<(escape_icon::Width-1))>>col))frame_.drawPixel(x+col,122+y,color);
        } else if(hint.key==UiKey::Enter) {
            for(unsigned y=0;y<enter_icon::Height;++y)for(unsigned col=0;col<enter_icon::Width;++col)
                if(enter_icon::Rows[y]&((1u<<(enter_icon::Width-1))>>col))frame_.drawPixel(x+col,122+y,color);
        } else if(const auto character=shortcutCharacter(hint.key)) {
            const char text_value[]={character,0};text(x,124,text_value,color);
        } else arrow(x,124,hint.key,hint.tone!=ShortcutTone::Disabled);
        x+=shortcutWidth(hint.key);
    }
 header(model.battery(),model.sdAccessLit());frame_.pushSprite(0,0);
}
void PanelRenderer::draw(const WifiUiModel& model,const wifi::QrCode& qr){display_.setBrightness(model.data().settings.brightness*255/100);wifi_qr_=&qr;frame_.setDimmed(model.confirming());frame_.fillSprite(TFT_BLACK);drawWifi(model);present(model,true);}
void PanelRenderer::draw(const UsbUiModel& model){display_.setBrightness(model.data().settings.brightness*255/100);drawUsb(model);present(model,false);}
}

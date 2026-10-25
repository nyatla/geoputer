#include "UiRenderer.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace geoputer {
void UiRenderer::drawUsb(const UiModel& model) {
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
    auto shell=[&](const char* title,const char* guide) {
        text(4,8,title);frame_.drawFastHLine(0,22,240,TFT_DARKGREY);
        frame_.drawFastHLine(0,118,240,TFT_DARKGREY);text(4,124,guide,TFT_LIGHTGREY);
    };
    frame_.setDimmed(model.confirming() || page==UsbPage::WaitEject);
    frame_.fillSprite(TFT_BLACK);
    char line[80];
    if(page==UsbPage::Menu || page==UsbPage::WaitEject) {
        shell("USB STORAGE","[ENTER] SELECT  [ESC] EXIT");
        const char* state=!usb.ready?"NO SD":usb.io_error?"SD ERROR":"SD READY";
        text(236-8*std::strlen(state),8,state,!usb.ready||usb.io_error?TFT_RED:TFT_GREEN);
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
            frame_.fillRect(0,119,240,16,TFT_BLACK);text(4,124,"[ENTER/ESC] CANCEL",TFT_LIGHTGREY);
        }
        return;
    }
    if(page==UsbPage::Files) {
        shell("EXPORT","[ENTER] SELECT  [ESC] BACK");
        std::snprintf(line,sizeof(line),"%u/%u",files.count?selected+1:0,files.count);text(236-8*std::strlen(line),8,line,TFT_LIGHTGREY);
        large(12,28,ja?"ログを選択":"Select a log");
        if(files.loading)text(12,56,"LOADING...",TFT_YELLOW);
        else if(files.error!=LogReadError::None)text(4,56,logReadErrorName(files.error),TFT_RED);
        else if(!files.count)large(12,56,ja?"ログがありません":"No logs found",TFT_LIGHTGREY);
        else for(unsigned n=0;n<files.count && n<4;++n) {
            if(n==selected)text(4,52+n*18,">",TFT_CYAN);
            char name[28];std::snprintf(name,sizeof(name),"%.27s",files.names[n]);
            text(20,52+n*18,name,n==selected?TFT_CYAN:TFT_WHITE);
        }
    } else if(page==UsbPage::Format) {
        shell("EXPORT","[ENTER] START  [ESC] BACK");
        std::snprintf(line,sizeof(line),"%u/3",selected+1);text(212,8,line,TFT_LIGHTGREY);
        large(12,28,ja?"形式を選択":"Select a format");
        const char* name=model.usbFile()<files.count?files.names[model.usbFile()]:"";
        std::snprintf(line,sizeof(line),"%.29s",name);text(4,49,line,TFT_LIGHTGREY);
        if(std::strlen(name)>29)text(4,59,name+29,TFT_LIGHTGREY);
        for(unsigned n=0;n<3;++n) {
            if(n==selected)text(4,74+n*16,">",TFT_CYAN);
            large(20,70+n*16,exportFormatName(static_cast<ExportFormat>(n)),n==selected?TFT_CYAN:TFT_WHITE);
        }
    } else if(page==UsbPage::Progress || page==UsbPage::Busy) {
        shell("EXPORT","PLEASE WAIT");
        text(212,8,exportFormatName(job.format),TFT_CYAN);
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
        shell("EXPORT","[ENTER] BACK");text(212,8,exportFormatName(job.format),TFT_CYAN);
        large(12,31,ja?"書き出し完了":"Export complete",TFT_GREEN);
        text(12,55,"/export/",TFT_LIGHTGREY);
        char name[ExportNameBytes];std::snprintf(name,sizeof(name),"%s%s",job.name,exportExtension(job.format));
        std::snprintf(line,sizeof(line),"%.29s",name);text(4,69,line);
        if(std::strlen(name)>29)text(4,79,name+29);
        large(12,96,ja?(usb.connected?"PCへ公開しました":"USB接続待ち"):
                            (usb.connected?"Available on PC":"Waiting for USB"),usb.connected?TFT_CYAN:TFT_YELLOW);
    } else {
        shell("EXPORT","[ENTER/ESC] BACK");
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
}

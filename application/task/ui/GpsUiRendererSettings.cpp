#include "GpsUiRenderer.h"
#include "SettingsLayout.h"
#include "SettingsText.h"
#include "AlarmText.h"
#include "SettingsAvailability.h"
#include "DatasetPresentation.h"
#include "PlacePresentation.h"
#include "LivePlaceLayout.h"
#include "LicenseText.h"
#include "GpsHeaderLayout.h"
#include "../../BuildInfo.h"
#include <cstdio>
#include <cstring>

namespace geoputer {
namespace {
constexpr bool showsSettingsStatus(SettingsPage page) {
    return page == SettingsPage::Root || page == SettingsPage::Operation ||
        page == SettingsPage::System || page == SettingsPage::Storage ||
        page == SettingsPage::Reset || page == SettingsPage::DatasetChoice;
}
}

void GpsUiRenderer::drawDatasetSettings(const GpsUiModel& model, const UiSnapshot& content, const Settings& settings) {
    const auto& data = model.data();
    const auto language = settings.language;
    const bool japanese = language == UiLanguage::Japanese;
    const auto item_role = japanese ? TextRole::UiLarge : TextRole::Ascii8x16;
    auto item = [&](int x, int y, const char* value, uint16_t color = TFT_WHITE) {
        font_.draw(frame_, x, y, value, item_role, color);
    };
    auto notice = [&](const char* value, uint16_t color = TFT_LIGHTGREY) {
        frame_.fillRect(0, 106, 240, 12, TFT_BLACK);
        text(4, 108, value, color);
    };
    char line[64];

    if (isDatasetBrowserPage(content.page)) {
        const auto& view = data.datasets();
        frame_.fillRect(0,0,240,SettingsLayout::HeaderBottom,TFT_BLACK);
        text(4,SettingsLayout::HeaderTextY,content.page==SettingsPage::DatasetDetail?"DETAIL":
            content.page==SettingsPage::DatasetChoice?"DATASET":
            view.purpose==DatasetPurpose::Rebuild?"REBUILD":view.purpose==DatasetPurpose::Verify?"VERIFY":"SELECT",TFT_WHITE);
        if (view.loading || view.applying) {
            if(view.applying)item(4,27,view.progress.verifying?(japanese?"検証中です":"Verifying..."):(japanese?"適用中です":"Applying..."),TFT_YELLOW);
            if(view.applying) {
                item(4,51,indexPhaseText(view.progress.phase,japanese),TFT_WHITE);
                frame_.drawRect(4,76,232,12,TFT_LIGHTGREY);frame_.fillRect(6,78,228*view.progress.percent/100,8,TFT_GREEN);
                std::snprintf(line,sizeof(line),"%s %u%%  %lus",view.progress.language,view.progress.percent,static_cast<unsigned long>(view.progress.elapsed_ms/1000));text(4,96,line);
            }
        } else if (view.error != DatasetError::None) {
            char message_line[80];
            for (unsigned row=0;row<3;++row) {
                datasetTextLines(datasetErrorText(view.error,japanese),japanese,row,message_line,sizeof(message_line));
                item(4,27+row*20,message_line,TFT_RED);
            }
            if (view.error == DatasetError::TooManyChunks) {
                std::snprintf(line,sizeof(line),"%llu / %u",static_cast<unsigned long long>(view.info.chunks),static_cast<unsigned>(config::MaxDatasetChunks));
                text(4,91,line,TFT_YELLOW);
            }
            if(content.page==SettingsPage::DatasetChoice && datasetRecoverable(view.error))
                text(4,108,"ENTER RETRY",TFT_YELLOW);
        } else if (content.page == SettingsPage::DatasetList) {
            if (!view.count) item(4,27,japanese?"候補がありません":"No datasets",TFT_YELLOW);
            for (unsigned row=0;row<view.count;++row) {
                const int y=27+row*14;
                const auto blocked=datasetRowBlock(data,row);
                const bool active=view.rows[row].id==data.storage.selected_dataset &&
                    bool(data.settings.dataset)==bool(view.rows[row].id);
                const auto color=blocked!=DatasetError::None?TFT_DARKGREY:active?TFT_GREEN:
                    row==content.selected_setting?TFT_CYAN:TFT_WHITE;
                text(4,y,row==content.selected_setting?">":" ",row==content.selected_setting?TFT_CYAN:color);
                const auto& entry=view.rows[row];
                text(20,y,entry.id?entry.id.c_str():"NONE",color);
            }
            const auto reason=view.count?datasetRowBlock(data,content.selected_setting):DatasetError::None;
            if(reason!=DatasetError::None) notice(datasetErrorText(reason,japanese));
            else {
                std::snprintf(line,sizeof(line),"PAGE %u%s",unsigned(view.page)+1u,view.has_newer?"  MORE":"");
                text(4,110,line,TFT_LIGHTGREY);
            }
        } else if (content.page == SettingsPage::DatasetChoice) {
            text(4,19,view.id.c_str(),TFT_WHITE);
            if(view.id==data.storage.selected_dataset && bool(data.settings.dataset))
                text(168,19,"SELECTED",TFT_GREEN);
            const auto offset=model.datasetTitleOffset();
            if(offset)drawKeyArrow(4,31,UiKey::Left,true);
            datasetSingleLine(uiTextAt(view.info.title,offset),japanese,line,sizeof(line),200);
            item(20,27,line,content.selected_setting==0?TFT_CYAN:TFT_WHITE);
            if(uiTextCanScroll(view.info.title,japanese,offset,200))drawKeyArrow(228,31,UiKey::Right,true);
            const auto candidate=model.datasetWordbookIndex();
            const auto type=candidate<view.info.language_count?view.info.wordbook_types[candidate]:view.info.language;
            datasetSingleLine(type,false,line,sizeof(line),112);
            const auto word_color=candidate!=view.info.language_index?TFT_YELLOW:
                content.selected_setting==1?TFT_CYAN:TFT_WHITE;
            item(4,47,content.selected_setting==1?">":" ",TFT_CYAN);
            item(20,47,japanese?"辞書":"WORD",content.selected_setting==1?TFT_CYAN:TFT_WHITE);
            font_.draw(frame_,SettingsLayout::ValueRight-PixelFont::width(line,TextRole::Ascii8x16),47,
                line,TextRole::Ascii8x16,word_color);
            item(4,67,content.selected_setting==2?">":" ",TFT_CYAN);
            item(20,67,japanese?"詳細":"Details",content.selected_setting==2?TFT_CYAN:TFT_WHITE);
            const auto blocked=datasetSelectionBlock(data,true);
            const auto apply_color=blocked!=DatasetError::None?TFT_DARKGREY:content.selected_setting==3?TFT_CYAN:TFT_WHITE;
            item(4,87,content.selected_setting==3?">":" ",apply_color);
            item(20,87,japanese?"適用する":"Apply",apply_color);
            if(content.selected_setting==3 && blocked!=DatasetError::None)notice(datasetErrorText(blocked,japanese));
        } else {
            const auto count=datasetDetailRows(view,japanese);
            const unsigned first=content.selected_setting;
            for (unsigned n=0;n<DatasetDetailVisibleRows && first+n<count;++n) {
                const int y=27+n*20;
                const bool ascii=datasetDetailLine(view,first+n,japanese,line,sizeof(line));
                font_.draw(frame_,4,y,line,ascii?TextRole::Ascii8x16:item_role,TFT_WHITE);
            }
            std::snprintf(line,sizeof(line),"%u/%u",first+1u,count);text(4,110,line);
        }
    } else if (content.page == SettingsPage::DatasetMenu) {
        frame_.fillRect(0,0,240,SettingsLayout::HeaderBottom,TFT_BLACK);text(4,SettingsLayout::HeaderTextY,"DATASET");
        if(settings.dataset) std::snprintf(line,sizeof(line),"CURRENT %s",data.storage.selected_dataset.c_str());
        else std::snprintf(line,sizeof(line),"CURRENT NONE");
        text(4,26,line,TFT_LIGHTGREY);
        for(unsigned row=0;row<DatasetMenuRows;++row) {
            const auto blocked=datasetMenuBlock(data,row);
            const auto color=blocked!=DatasetError::None?TFT_DARKGREY:row==content.selected_setting?TFT_CYAN:TFT_WHITE;
            const int y=43+row*20;item(4,y,row==content.selected_setting?">":" ",color);
            item(20,y,datasetMenuLabel(row,japanese),color);
        }
        if(data.storage.dataset_failure.revision && !settings.dataset) {
            formatDatasetRelease(data.storage.dataset_failure,line,sizeof(line));text(4,106,line,TFT_YELLOW);
        }
        const auto reason=datasetMenuBlock(data,content.selected_setting);
        if(reason!=DatasetError::None) notice(datasetErrorText(reason,japanese));
        else if(data.storage.dataset_failure.save_failed) notice("RELEASE NOT SAVED",TFT_RED);
    } else if (content.page == SettingsPage::DatasetIndex) {
        const auto& view=data.datasets();const auto& progress=view.progress;
        frame_.fillRect(0,0,240,SettingsLayout::HeaderBottom,TFT_BLACK);text(4,SettingsLayout::HeaderTextY,view.purpose==DatasetPurpose::Verify?"VERIFY CACHE":"REBUILD CACHE");
        std::snprintf(line,sizeof(line),"TARGET %s / ALL LANGUAGES",view.id.c_str());text(4,26,line,TFT_LIGHTGREY);
        item(4,43,view.applying?(view.purpose==DatasetPurpose::Verify?(japanese?"検証中です":"Verifying..."):(japanese?"再生成中です":"Rebuilding...")):
            view.error==DatasetError::None?(japanese?"完了しました":"Completed"):(japanese?"失敗しました":"Failed"),
            view.applying?TFT_YELLOW:view.error==DatasetError::None?TFT_GREEN:TFT_RED);
        item(4,64,indexPhaseText(progress.phase,japanese));
        frame_.drawRect(4,87,232,12,TFT_LIGHTGREY);frame_.fillRect(6,89,228*progress.percent/100,8,TFT_GREEN);
        if (!view.applying && view.error!=DatasetError::None) notice(datasetErrorText(view.error,japanese),TFT_RED);
        else {
            std::snprintf(line,sizeof(line),"%s %u%%  %lu.%03lus",progress.language,progress.percent,
                static_cast<unsigned long>(progress.elapsed_ms/1000),static_cast<unsigned long>(progress.elapsed_ms%1000));text(4,105,line);
        }
    }
}

void GpsUiRenderer::drawSettings(const GpsUiModel& model, const UiSnapshot& content, const Settings& settings) {
    const auto& data = model.data();
    const auto language = settings.language;
    const bool japanese = language == UiLanguage::Japanese;
    const auto item_role = japanese ? TextRole::UiLarge : TextRole::Ascii8x16;
    auto item = [&](int x,int y,const char* value,uint16_t color=TFT_WHITE) {
        font_.draw(frame_,x,y,value,item_role,color);
    };
    char line[64];
    auto notice = [&](const char* value, uint16_t color = TFT_LIGHTGREY) {
        frame_.fillRect(0,106,240,12,TFT_BLACK);
        text(4,108,value,color);
    };
    const char* title = model.cardInfo() ? "CARD INFO" :
                        content.page == SettingsPage::Root ? (data.boot.maintenance ? "MAINTENANCE" : "SETTINGS") :
                        content.page == SettingsPage::Operation ? "OPERATION" :
                        content.page == SettingsPage::System ? "SYSTEM" :
                        content.page == SettingsPage::Storage ? "STORAGE" :
                        content.page == SettingsPage::Alarm ? "ALERTS" :
                        content.page == SettingsPage::Reset ? "RESET" :
                        content.page == SettingsPage::About ? "ABOUT" : "LICENSES";
    auto label = [&](SettingsLabel id) { return settingsLabel(language, id); };
    if (!isDatasetSettingsPage(content.page))
        text(4, SettingsLayout::HeaderTextY, title, TFT_LIGHTGREY);
    if(content.page==SettingsPage::Alarm) {
        const auto& profile=data.storage.alarm;
        const auto first=SettingsLayout::firstVisible(content.selected_setting,AlarmRows);
        for(unsigned visible=0;visible<SettingsLayout::VisibleRows && first+visible<AlarmRows;++visible) {
            const auto row=first+visible;const int y=SettingsLayout::FirstRowY+visible*SettingsLayout::RowStep;
            const bool selected=row==content.selected_setting;
            const bool enabled=alarmSettingEnabled(data,row);
            const auto color=!enabled?TFT_DARKGREY:selected?(model.editDirty()?TFT_YELLOW:TFT_CYAN):TFT_WHITE;
            item(4,y,selected?">":" ",TFT_CYAN);item(SettingsLayout::LabelX,y,alarmLabel(row,japanese),color);
            char value[32];alarmValueText(row,selected&&model.editing()?model.editValue():alarmValue(profile.settings,row),japanese,value,sizeof(value));
            // Keep long English choices from crossing the field label.
            font_.draw(frame_,SettingsLayout::ValueRight-PixelFont::width(value,item_role),y,value,item_role,!enabled?TFT_DARKGREY:selected&&model.editDirty()?TFT_YELLOW:TFT_CYAN);
        }
        const char* state=data.storage.nvs_error?"NVS ERROR / ALERT OFF":
            data.storage.busy?"STORAGE BUSY":alarm::stateName(profile.state);
        char status[48];std::snprintf(status,sizeof(status),"DS %s",state);
        notice(status,profile.usable() && !data.storage.nvs_error?TFT_LIGHTGREY:TFT_RED);
        return;
    }
    if (content.page == SettingsPage::Licenses) {
        std::snprintf(line,sizeof(line),"%lu/%lu",static_cast<unsigned long>(model.licenseOffset()+1),static_cast<unsigned long>(licenseLineCount()));
        valueText(80,gps_header::TitleRight,SettingsLayout::HeaderTextY,line,TFT_LIGHTGREY);
    }
    if (isDatasetSettingsPage(content.page)) {
        drawDatasetSettings(model, content, settings);
    } else if (model.cardInfo()) {
        if (data.storage.card_state == CardState::Mounted)
            std::snprintf(line, sizeof(line), "FAT%u %s", data.storage.fat_type, settingsCardState(language, data.storage.card_state));
        else if (data.storage.fat_type)
            std::snprintf(line, sizeof(line), "FAT%u %s", data.storage.fat_type, label(SettingsLabel::Unsupported));
        else std::snprintf(line, sizeof(line), "%s", settingsCardState(language, data.storage.card_state));
        item(4, 27, line, data.storage.card_state == CardState::Mounted ? TFT_GREEN : TFT_YELLOW);
        constexpr double GiB = 1024.0 * 1024.0 * 1024.0;
        std::snprintf(line, sizeof(line), "%s %.1fG", label(SettingsLabel::CardSize), data.storage.card_bytes / GiB); item(4, 47, line);
        std::snprintf(line, sizeof(line), "%s %.1fG", label(SettingsLabel::Total), data.storage.total_bytes / GiB); item(4, 67, line);
        std::snprintf(line, sizeof(line), "%s %.1fG", label(SettingsLabel::Free), data.storage.free_bytes / GiB); item(4, 87, line);
    } else if (content.page == SettingsPage::Root) {
        const unsigned first = SettingsLayout::firstVisible(content.selected_setting, SettingsRootCount);
        for (unsigned visible = 0; visible < SettingsLayout::VisibleRows && first + visible < SettingsRootCount; ++visible) {
            const unsigned row = first + visible;
            const int y = SettingsLayout::FirstRowY + visible * SettingsLayout::RowStep;
            const auto color = row == content.selected_setting ? TFT_CYAN : TFT_WHITE;
            item(4, y, row == content.selected_setting ? ">" : " ", TFT_CYAN);
            item(20, y, settingsRootLabel(language, row), color);

        }
        std::snprintf(line, sizeof(line), "%u/%u", content.selected_setting + 1u, SettingsRootCount);
        text(4, LivePlaceLayout::LogY, line);
    } else if (content.page == SettingsPage::System) {
        const uint8_t usb = model.editing() ? model.editValue() : model.systemUsbMode();
        const char* reason = systemModeBlockText(data, model.systemUsbMode(), language);
        for (unsigned row = 0; row < SettingsSystemCount; ++row) {
            const int y = SettingsLayout::FirstRowY + row * SettingsLayout::RowStep;
            const bool selected = row == content.selected_setting;
            const bool enabled=systemSettingEnabled(data,model.systemUsbMode(),row);
            const auto color = !enabled ? TFT_DARKGREY : selected ? (model.editDirty()?TFT_YELLOW:TFT_CYAN) : TFT_WHITE;
            item(4,y,selected ? ">" : " ",color);
            item(SettingsLayout::LabelX,y,settingsSystemLabel(language,row),color);
            if (row < 2) {
                const char* value = row == 0 ? settingsModeValue(language,usb) : "MESH";
                font_.draw(frame_,SettingsLayout::ValueRight-PixelFont::width(value,item_role),y,value,item_role,!enabled?TFT_DARKGREY:selected&&model.editDirty()?TFT_YELLOW:TFT_CYAN);
            }
        }
        const char* help = content.selected_setting == 1 ? (japanese ? "外付けGPSは未対応" : "External GPS unsupported") :
            content.selected_setting == 2 ? reason : "";
        if (help[0]) {
            notice(help);
        }
    } else if (content.page == SettingsPage::Operation) {
        const unsigned first = SettingsLayout::firstVisible(content.selected_setting, OperationRowCount);
        for (unsigned visible = 0; visible < SettingsLayout::VisibleRows && first + visible < OperationRowCount; ++visible) {
            const unsigned row = first + visible;
            const unsigned setting = operationSettingIndex(row);
            const int y = SettingsLayout::FirstRowY + visible * SettingsLayout::RowStep;
            const bool selected = row == content.selected_setting;
            const bool enabled=operationSettingEnabled(data,row);
            const uint16_t color = !enabled ? TFT_DARKGREY : selected ? (model.editDirty()?TFT_YELLOW:TFT_CYAN) : TFT_WHITE;
            item(4, y, selected ? ">" : " ", TFT_CYAN);
            item(SettingsLayout::LabelX, y, settingsOperationLabel(language, row), color);
            char value[24];
            settingsValueText(language, setting, selected && model.editing() ? model.editValue() : settingValue(settings, setting), value, sizeof(value));
            const auto value_role = setting == static_cast<unsigned>(SettingId::Language) ? TextRole::Ascii8x16 : item_role;
            font_.draw(frame_, SettingsLayout::ValueRight - PixelFont::width(value, value_role), y, value, value_role, !enabled?color:selected&&model.editDirty()?TFT_YELLOW:TFT_CYAN);
        }
        std::snprintf(line, sizeof(line), "%u/%u", content.selected_setting + 1u, OperationRowCount);
        text(4, LivePlaceLayout::LogY, line, TFT_WHITE);
    } else if (content.page == SettingsPage::Storage) {
        const unsigned first=SettingsLayout::firstVisible(content.selected_setting,SettingsStorageCount);
        for (unsigned row = first; row < first+SettingsLayout::VisibleRows && row<SettingsStorageCount; ++row) {
            const int y = SettingsLayout::FirstRowY + (row-first) * SettingsLayout::RowStep;
            item(4, y, row == content.selected_setting ? ">" : " ", TFT_CYAN);
            item(20, y, settingsStorageLabel(language, row), row == content.selected_setting ? TFT_CYAN : TFT_WHITE);
        }
    } else if (content.page == SettingsPage::About) {
        font_.draw(frame_, 4, 27, ProductName, TextRole::Ascii8x16, TFT_WHITE);
        std::snprintf(line, sizeof(line), "VERSION %s", ApplicationVersion);
        font_.draw(frame_, 4, 47, line, TextRole::Ascii8x16, TFT_CYAN);
        std::snprintf(line, sizeof(line), "BUILD %s", BuildDate); text(4, 69, line);
        std::snprintf(line, sizeof(line), "      %s", BuildTime); text(4, 79, line);
        std::snprintf(line, sizeof(line), "GIT %s%s", BuildGitRevision, buildGitSuffix());
        font_.draw(frame_, 4, 97, line, TextRole::Ascii8x16, TFT_LIGHTGREY);
    } else if (content.page == SettingsPage::Reset) {
        item(4, 27, ">", TFT_CYAN);
        item(20, 27, label(SettingsLabel::ResetAction), TFT_CYAN);
        item(4, 57, label(SettingsLabel::ResetScope), TFT_LIGHTGREY);
        item(4, 87, label(SettingsLabel::KeepSd), TFT_LIGHTGREY);
    } else {
        for (unsigned row = 0; row < SettingsLayout::LicenseRows; ++row) {
            text(4, SettingsLayout::FirstRowY + row * SettingsLayout::LicenseRowStep,
                 licenseLine(model.licenseOffset() + row));
        }
    }
    if (!model.confirming() && showsSettingsStatus(content.page)) {
        const bool settings_error = data.storage.settings_state == SettingsState::Failed ||
                                    data.storage.settings_state == SettingsState::Unavailable ||
                                    data.storage.settings_state == SettingsState::Invalid;
        if (data.storage.busy || data.message[0] || settings_error) frame_.fillRect(0, 106, 240, 12, TFT_BLACK);
        if (data.storage.busy) text(4, 108, label(SettingsLabel::Working), TFT_YELLOW);
        else if (data.message[0]) text(4, 108, settingsMessage(language, data.message), TFT_YELLOW);
        else if (settings_error) text(4, 108, label(SettingsLabel::Error), TFT_RED);
    }
}

// Native 8x8 arrow cells; no scaled glyphs or font-dependent Unicode arrows.

}  // namespace geoputer

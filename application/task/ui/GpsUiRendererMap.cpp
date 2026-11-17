#include "GpsUiRenderer.h"
#include "PlacePresentation.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace geoputer {

void GpsUiRenderer::drawMap(const GpsUiModel& model,const UiSnapshot& content) {
    const auto& data=model.data();const auto& received=model.mapFrame();const auto& image=received.image();const auto& info=received.info();
    frame_.fillRect(0,17,240,103,TFT_BLACK);
    for(unsigned y=0;y<image.height();++y)for(unsigned x=0;x<image.width();++x) {
        const auto pixel=image.get(x,y);if(pixel)frame_.drawPixel(x,y+18,pixel==2?TFT_WHITE:TFT_DARKGREY);
    }
    // Glyph-only outlines keep the entire map visible behind the labels.
    auto overlay=[&](int x,int y,const char* value,TextRole role,uint16_t color) {
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
            if(dx || dy)font_.draw(frame_,x+dx,y+dy,value,role,TFT_BLACK);
        font_.draw(frame_,x,y,value,role,color);
    };
    const auto shown_scale=image.bytes()?info.request.scale:data.map.scale;
    overlay(4,22,map::scaleText(shown_scale),TextRole::UI,TFT_CYAN);
    overlay(227,22,"N",TextRole::UI,TFT_WHITE);
    if(viewingLogPoint(content.screen)) {
        char point[16];std::snprintf(point,sizeof(point),"#%lu",static_cast<unsigned long>(data.browse().index+1));
        overlay(220-static_cast<int>(std::strlen(point))*8,22,point,TextRole::UI,TFT_WHITE);
    }
    frame_.fillRect(229,34,3,13,TFT_BLACK);
    frame_.fillRect(230,35,1,11,TFT_LIGHTGREY);
    for(int i=0;i<4;++i){frame_.drawPixel(230-i,35+i,TFT_LIGHTGREY);frame_.drawPixel(230+i,35+i,TFT_LIGHTGREY);}
    if(image.bytes()) {
        const double desired=50*info.meters_per_pixel,base=std::pow(10.0,std::floor(std::log10(desired)));
        double distance=base;
        for(double factor:{1.,2.,5.,10.})if(std::abs(base*factor-desired)<std::abs(distance-desired) && std::lround(base*factor/info.meters_per_pixel)<=70)distance=base*factor;
        const int length=std::max(1,int(std::lround(distance/info.meters_per_pixel))),left=44,right=left+length;
        char scale[24];if(distance>=1000)std::snprintf(scale,sizeof(scale),"%.1fkm",distance/1000);
        else if(distance>=1)std::snprintf(scale,sizeof(scale),"%.0fm",distance);
        else std::snprintf(scale,sizeof(scale),"%.3fm",distance);
        overlay(left,22,scale,TextRole::UI,TFT_WHITE);
        frame_.fillRect(left-1,33,length+3,3,TFT_BLACK);
        frame_.fillRect(left-1,30,3,9,TFT_BLACK);frame_.fillRect(right-1,30,3,9,TFT_BLACK);
        frame_.drawFastHLine(left,34,length+1,TFT_LIGHTGREY);
        frame_.fillRect(left,31,1,7,TFT_LIGHTGREY);frame_.fillRect(right,31,1,7,TFT_LIGHTGREY);
    }
    // Current-position names arrive through the same point/code/name caches as
    // Live Place. Neither an old frame nor an in-flight frame owns these rows.
    const auto issue_state=viewingLogPoint(content.screen)?data.place_state:placeIssue(data);
    if(datasetInactive(issue_state)) {
        const auto issue=datasetIssueText(issue_state,data.settings.language==UiLanguage::Japanese);
        overlay(4,88,issue.first,TextRole::UI,TFT_WHITE);
        overlay(4,100,issue.second,data.settings.language==UiLanguage::Japanese?TextRole::Place:TextRole::Ascii8x16,TFT_WHITE);
    } else if(hasPlace(data) || data.place_state==PlaceState::Ready) {
        char first[config::PlaceCachedLineBytes],second[config::PlaceCachedLineBytes];
        fitPlaceDisplayLine(data.place[0],first,data.dataset.place_text_mode,true);
        fitPlaceDisplayLine(data.place[1],second,data.dataset.place_text_mode);
        overlay(4,88,first,TextRole::UI,TFT_WHITE);
        overlay(4,100,second,placeRole(data.dataset.place_text_mode),TFT_WHITE);
    } else if(data.place_state==PlaceState::NotFound) {
        overlay(4,88,"NO REGION",TextRole::UI,TFT_WHITE);
    }
    if(data.map.error!=map::Error::None)
        overlay(4,48,map::errorText(data.map.error),TextRole::UI,TFT_RED);
    // Draw last so the moving marker remains visible over the two place rows.
    const auto cursor=model.mapCursor();
    const double px=info.width/2+cursor.x,py=info.height/2+cursor.y;
    if(image.bytes() && cursor.valid && px>=0 && px<image.width() && py>=0 && py<image.height()) {
        const int cx=std::lround(px),cy=18+std::lround(py);
        auto pixel=[&](int x,int y,uint16_t color) {
            if(x>=0 && x<240 && y>=18 && y<119)frame_.drawPixel(x,y,color);
        };
        const auto color=data.map.error==map::Error::None && displayedFix(content.screen,data).valid && (viewingLogPoint(content.screen) || data.gps.reception==GpsReception::Fix)?TFT_CYAN:TFT_DARKGREY;
        for(int y=-8;y<=8;++y)for(int x=-8;x<=8;++x)pixel(cx+x,cy+y,TFT_BLACK);
        for(int y=-5;y<=5;++y)for(int x=-5;x<=5;++x)if(std::abs(x)+std::abs(y)==5)pixel(cx+x,cy+y,color);
        for(int i=8;i<=10;++i) {pixel(cx-i,cy,color);pixel(cx+i,cy,color);pixel(cx,cy-i,color);pixel(cx,cy+i,color);}
        pixel(cx,cy,TFT_WHITE);
    }
}

}  // namespace geoputer

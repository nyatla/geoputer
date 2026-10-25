#include <cmath>
#include <cstring>
#include <stdexcept>
#include "task/ui/LivePlaceLayout.h"
#include "task/ui/GpsPresentation.h"
using namespace geoputer;
namespace {
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
}
void testLiveLayout() {
    check(speedTextWidth("1.00") == 56 && speedTextWidth("14.2") == 56 && speedTextWidth("123") == 48 && speedTextWidth("--") == 32,
          "three speed digits and a compact decimal fit without reducing digit size");
    GpsSnapshot gps{};
    gps.reception=GpsReception::Fix;
    gps.fix={35.681641,140.027194,0,true};
    gps.fix.fields=GpsSpeed|GpsCourse|GpsAltitude|GpsSatellites|GpsHdop;
    gps.fix.speed_kmh=4.2; gps.fix.course_deg=140; gps.fix.altitude_m=12.3; gps.fix.satellites=12;
    auto text=formatGpsText(gps,{});
    check(std::strcmp(text.speed,"4.20")==0 && std::strcmp(text.heading,"140")==0 &&
          std::strcmp(text.altitude,"12.3")==0 && std::strcmp(text.satellites,"12")==0,"live GPS values and units");
    GpsFix displayed={-90,-180,0,true};
    text=formatGpsText(gps,displayed);
    check(std::strcmp(text.latitude,"-90.000000")==0 && std::strcmp(text.longitude,"-180.000000")==0,
          "coordinates remain paired with the displayed address and fit the right panel");
    gps.fix.speed_kmh=14.2; gps.fix.altitude_m=-12.3;
    text=formatGpsText(gps,{});
    check(std::strcmp(text.speed,"14.2")==0 && std::strcmp(text.altitude,"-12.3")==0,
          "three speed digits retain their decimal; negative altitude is valid");
    const struct { float speed; const char* value; } cases[] = {
        {0,"0"},{0.42f,"0"},{0.9994f,"0"},{0.9996f,"0"},
        {1,"1.00"},{1.234f,"1.23"},{4.2f,"4.20"},
        {9.96f,"9.96"},{9.994f,"9.99"},{9.996f,"10.0"},
        {10,"10.0"},{14.2f,"14.2"},{99.94f,"99.9"},{99.96f,"100"},
        {100,"100"},{123.4f,"123"},{999.4f,"999"},{999.6f,"OVR"}
    };
    for (const auto& example:cases) {
        gps.fix.speed_kmh=example.speed; text=formatGpsText(gps,{});
        check(std::strcmp(text.speed,example.value)==0,
              "fixed km/h unit, three-digit precision, and rounding boundaries");
        check(speedTextWidth(text.speed)<=56,"every speed stays within its native three-digit viewport");
    }
    gps.fix.speed_kmh=999.94f;
    text=formatGpsText(gps,{});
    check(std::strcmp(text.speed,"OVR")==0,"rounded speeds above 999 use the bounded overflow indicator");
    gps.fix.speed_kmh=999.96f; gps.fix.course_deg=359.6;
    text=formatGpsText(gps,{});
    check(std::strcmp(text.speed,"OVR")==0 && std::strcmp(text.heading,"0")==0,
          "speed rounding overflow and heading wrapping stay in their panels");
    gps.fix.speed_kmh=0; gps.fix.course_deg=0; gps.fix.altitude_m=0; gps.fix.satellites=0;
    text=formatGpsText(gps,{});
    check(std::strcmp(text.speed,"0")==0 && std::strcmp(text.heading,"0")==0 && std::strcmp(text.altitude,"0.0")==0 &&
          std::strcmp(text.satellites,"0")==0,"valid zeros must not be mistaken for missing values");
    gps.fix.fields=GpsHdop;
    text=formatGpsText(gps,{});
    check(std::strcmp(text.speed,"--")==0 && std::strcmp(text.heading,"--")==0 && std::strcmp(text.altitude,"--")==0 &&
          std::strcmp(text.satellites,"--")==0,"missing optional fields do not reuse old values or HDOP");
    gps.fix.fields=GpsSpeed|GpsCourse|GpsAltitude;
    gps.fix.speed_kmh=-1;
    text=formatGpsText(gps,{});
    check(std::strcmp(text.speed,"--")==0,"negative speed remains unavailable");
    gps.fix.speed_kmh=NAN; gps.fix.course_deg=-1; gps.fix.altitude_m=INFINITY;
    text=formatGpsText(gps,{});
    check(std::strcmp(text.speed,"--")==0 && std::strcmp(text.heading,"--")==0 && std::strcmp(text.altitude,"--")==0,
          "non-finite and invalid optional values stay unavailable");
    char unit_text[16];
    gps.fix.altitude_m=12.3;gps.fix.fields=GpsAltitude;
    check(!std::strcmp(liveAltitude(formatGpsText(gps),unit_text),"12.3m"),"LIVE appends its bounded altitude unit");
    gps.fix.altitude_m=-21474836.f;
    check(!std::strcmp(liveAltitude(formatGpsText(gps),unit_text),"OVR"),"LIVE altitude width includes the unit");
    gps.fix.valid=false; gps.reception=GpsReception::NoFix;
    text=formatGpsText(gps,displayed);
    check(std::strcmp(text.latitude,"--")==0 && std::strcmp(text.longitude,"--")==0 && std::strcmp(text.heading,"--")==0,
          "fix loss clears all displayed GPS fields even with a retained search fix");
}

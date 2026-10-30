#pragma once
#include <cstdint>
#include <limits>
namespace geoputer::alarm {
struct Cell {int x=0,y=0;};
// Integer cell neighbors, independent of dataset resolution and latitude.
// Longitude wraps at the date line; points beyond the poles are unavailable.
inline bool neighborCell(int cx,int cy,int dx,int dy,int invx,int invy,Cell& out) {
    if(invx<=0 || invy<=0 || dx < -1 || dx>1 || dy < -1 || dy>1)return false;
    int64_t x=int64_t(cx)+dx;const int64_t y=int64_t(cy)+dy;
    const int64_t east=int64_t(invx)*180,north=int64_t(invy)*90;
    if(y < -north || y>north)return false;
    if(x>east)x-=east*2;else if(x < -east)x+=east*2;
    if(x<std::numeric_limits<int>::min() || x>std::numeric_limits<int>::max() ||
       y<std::numeric_limits<int>::min() || y>std::numeric_limits<int>::max())return false;
    out={static_cast<int>(x),static_cast<int>(y)};return true;
}
} // namespace geoputer::alarm

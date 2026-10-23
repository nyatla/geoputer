#pragma once
inline unsigned keyboard_settles = 0;
inline void delayMicroseconds(unsigned value) { if (value >= 200) ++keyboard_settles; }

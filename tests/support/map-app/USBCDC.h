#pragma once
#include <cstdio>
class USBCDC { public: template<class... Args> void printf(const char* format, Args... args) { std::printf(format,args...); } };

#pragma once
#include <cstdio>
class USBCDC {public:
    template<class... Args> void printf(const char* f,Args... args){std::printf(f,args...);}
    void println(const char* value){std::puts(value);}
};

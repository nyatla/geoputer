#pragma once
#include <string>
#include <cstdint>
constexpr unsigned SERIAL_8N1=0;
class HardwareSerial {
public:
    explicit HardwareSerial(int){}
    bool opened=false, fail_write=false;
    std::string sent, incoming;
    size_t setRxBufferSize(size_t size){return size;}
    void begin(unsigned,unsigned,int,int){opened=true;}
    explicit operator bool()const{return opened;}
    int available()const{return incoming.size();}
    int read(){if(incoming.empty())return -1;const auto c=static_cast<unsigned char>(incoming[0]);incoming.erase(0,1);return c;}
    size_t write(const uint8_t* bytes,size_t n){if(fail_write)return 0;sent.append(reinterpret_cast<const char*>(bytes),n);return n;}
    void flush(){}
};

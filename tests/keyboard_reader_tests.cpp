#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <Arduino.h>
#include "hardware/AdvKeyboardReader.h"
using namespace geoputer;
using Chip=Adafruit_TCA8418;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool has(const AdvKeyboardReader& reader,int x,int y) {
    return std::find(reader.keyList().begin(),reader.keyList().end(),Point2D_t{x,y})!=reader.keyList().end();
}
int main() {
    try {
        // No press event remains after resetting the MCU; the physical key stays down.
        Chip::reset();Chip::physical=1;Chip::regs[TCA8418_REG_KP_GPIO_1]=0x7f;Chip::regs[TCA8418_REG_KP_GPIO_2]=0xff;
        AdvKeyboardReader first;first.begin();
        check(first.ready() && has(first,0,0) && first.keyList().size()==1,"held ESC must survive empty-FIFO warm reset");
        check(keyboard_settles==8 && Chip::regs[TCA8418_REG_GPIO_DIR_2]==0 && Chip::regs[TCA8418_REG_KP_GPIO_1]==0x7f &&
              Chip::regs[TCA8418_REG_KP_GPIO_2]==0xff,"physical scan must restore keypad ownership");
        AdvKeyboardReader reset;reset.begin();
        check(reset.ready() && has(reset,0,0),"a second MCU reset must still find the held key without a new edge");
        Chip::fifo={0x81,0x81};reset.update();check(reset.keyList().size()==1,"late duplicate press must not grow or repeat the held state");
        Chip::physical=0;Chip::fifo={0x01};reset.update();check(reset.keyList().empty(),"release is consumed without any interrupt callback");
        Chip::physical=1;Chip::fifo={0x81};reset.update();check(has(reset,0,0),"new press is consumed while IRQ edges are absent");
        for(unsigned col=0;col<8;++col)for(unsigned row=0;row<7;++row) {
            Chip::reset();Chip::physical=uint64_t{1}<<(col*7+row);AdvKeyboardReader reader;reader.begin();
            check(reader.ready() && reader.keyList().size()==1 && has(reader,row*2+(col>3),(col+4)%4),"all 56 matrix keys map to physical coordinates");
        }
        Chip::reset();Chip::physical=1|(uint64_t{1}<<14);AdvKeyboardReader modified;modified.begin();
        check(has(modified,0,0) && has(modified,0,2) && modified.keyList().size()==2,"ESC with Fn must retain both physical keys");
        Chip::reset();AdvKeyboardReader none;none.begin();check(none.ready() && none.keyList().empty(),"stale latched LOW must not create a phantom key");
        Chip::fifo={0,0xff,91,0x89};none.update();check(none.keyList().empty(),"invalid and unused keypad events must be ignored");
        Chip::physical=1;Chip::regs[TCA8418_REG_INT_STAT]=TCA8418_REG_STAT_OVR_FLOW_INT;none.update();
        check(has(none,0,0),"FIFO overflow recovers from physical levels");
        Chip::physical=0;Chip::regs[TCA8418_REG_INT_STAT]=TCA8418_REG_STAT_OVR_FLOW_INT;none.update();
        check(none.keyList().empty(),"overflow recovery also releases stale keys");
        Chip::reset();Chip::physical=1;Chip::fail_gpio_read=3;AdvKeyboardReader failed;failed.begin();
        check(!failed.ready() && failed.keyList().empty() && Chip::regs[TCA8418_REG_GPIO_DIR_2]==0 &&
              Chip::regs[TCA8418_REG_KP_GPIO_2]==0xff,"I2C failure cannot report a held key and must restore matrix mode");
        Chip::reset();Chip::online=false;AdvKeyboardReader offline;offline.begin();offline.update();
        check(!offline.ready() && offline.keyList().empty(),"missing controller returns without waiting");
        std::cout<<"PASS: ADV held-key warm reset, matrix snapshot, FIFO polling, duplicate/release, overflow and I2C failure\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

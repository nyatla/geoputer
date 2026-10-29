#include <Arduino.h>
#include <M5Unified.h>
#include <utility/Keyboard/Keyboard.h>
#include <utility/Keyboard/KeyboardReader/IOMatrix.h>
#include <utility/Keyboard/KeyboardReader/TCA8418.h>
#include "hardware/AdvKeyboardReader.h"
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace {
bool measuring=false;
size_t allocations=0,deallocations=0;
void check(bool ok,const char* message) {
    if(!ok) {measuring=false;throw std::runtime_error(message);}
}
struct NoHeap {
    NoHeap() {allocations=deallocations=0;measuring=true;}
    ~NoHeap() {measuring=false;}
    void verify() const {check(allocations==0 && deallocations==0,"keyboard polling allocated or freed heap memory");}
};
uint8_t event(unsigned x,unsigned y,bool down=true) {
    return static_cast<uint8_t>((down?0x80:0)+x/2*10+y+(x%2?4:0)+1);
}
void refresh(Keyboard_Class& keyboard) {keyboard.updateKeyList();keyboard.updateKeysState();}
bool has(const Keyboard_Class& /*unused*/,const std::vector<Point2D_t>& keys,int x,int y) {
    return std::find(keys.begin(),keys.end(),Point2D_t{x,y})!=keys.end();
}
void advLimit() {
    using Chip=Adafruit_TCA8418;
    Chip::reset();
    Keyboard_Class keyboard(3);
    auto reader=std::make_unique<geoputer::AdvKeyboardReader>(keyboard.maxKeys());
    auto* state=reader.get();
    keyboard.begin(std::move(reader));
    check(state->ready() && keyboard.maxKeys()==3,"ADV reader initialization");
    {
        NoHeap probe;
        Chip::fifo={event(0,1)};refresh(keyboard);
        check(keyboard.keysState().tab && keyboard.keysState().word.empty() && keyboard.getKeyValue({0,1}).value_first==KEY_TAB,"physical Tab is a special key, not ASCII plus");
        Chip::fifo={event(0,1,false)};refresh(keyboard);check(!keyboard.keysState().tab,"physical Tab releases its flag");
        // a + Fn + Shift fill the shared limit, including modifiers.
        Chip::fifo={event(2,2),event(0,2),event(1,2)};refresh(keyboard);
        check(keyboard.keyList().size()==3 && keyboard.keysState().fn && keyboard.keysState().shift &&
              keyboard.keysState().word.size()==1 && keyboard.keysState().word[0]=='A',"modifier ordering within fixed capacity");
        Chip::fifo={event(6,3),event(2,2)};refresh(keyboard);
        check(keyboard.keyList().size()==3 && !has(keyboard,keyboard.keyList(),6,3),"excess and duplicate press must be ignored");
        Chip::fifo={event(2,2,false)};refresh(keyboard);
        check(keyboard.keyList().size()==2 && keyboard.keysState().word.empty(),"release at capacity must work");
        // An ignored press does not appear later just because space became free.
        refresh(keyboard);check(!has(keyboard,keyboard.keyList(),6,3),"discarded press must not be deferred");
        Chip::fifo={event(6,3,false),event(6,3)};refresh(keyboard);
        check(has(keyboard,keyboard.keyList(),6,3),"new press can occupy freed slot");
        for(unsigned i=0;i<200;++i) {
            Chip::fifo={event(6,3,false),event(6,3),event(6,3),event(3,2)};refresh(keyboard);
            check(keyboard.keyList().size()==3,"repeated over-limit input");
        }
        Chip::physical=(uint64_t{1}<<56)-1;
        Chip::regs[TCA8418_REG_INT_STAT]=TCA8418_REG_STAT_OVR_FLOW_INT;refresh(keyboard);
        check(keyboard.keyList().size()==3,"overflow snapshot must honor capacity");
        Chip::physical=0;Chip::regs[TCA8418_REG_INT_STAT]=TCA8418_REG_STAT_OVR_FLOW_INT;refresh(keyboard);
        check(keyboard.keyList().empty() && !keyboard.keysState().shift && !keyboard.keysState().fn,"snapshot releases ignored and accepted keys cleanly");
        probe.verify();
    }
}
void physicalLimit(size_t requested) {
    using Chip=Adafruit_TCA8418;
    Chip::reset();Chip::physical=(uint64_t{1}<<56)-1;
    Keyboard_Class keyboard(requested);
    keyboard.begin(std::make_unique<geoputer::AdvKeyboardReader>(keyboard.maxKeys()));
    const auto limit=std::min(requested,size_t{56});
    check(keyboard.keyList().size()==limit,"held startup snapshot must use construction limit");
    NoHeap probe;
    keyboard.updateKeysState();
    for(unsigned i=0;i<50;++i) {
        Chip::physical=0;Chip::regs[TCA8418_REG_INT_STAT]=TCA8418_REG_STAT_OVR_FLOW_INT;refresh(keyboard);
        Chip::physical=(uint64_t{1}<<56)-1;Chip::regs[TCA8418_REG_INT_STAT]=TCA8418_REG_STAT_OVR_FLOW_INT;refresh(keyboard);
        check(keyboard.keyList().size()==limit && keyboard.keysState().word.size()<=limit &&
              keyboard.keysState().hid_keys.size()<=limit,"all-key snapshot and state lists remain bounded");
    }
    probe.verify();
}
void builtinReaders() {
    // Compile and exercise both upstream readers, rather than replacing them in the test.
    M5.board=m5::board_M5Cardputer;
    keyboard_matrix_held=0;
    Keyboard_Class matrix(3);matrix.begin();
    {
        NoHeap probe;
        for(unsigned i=0;i<100;++i) {
            keyboard_matrix_held=(uint64_t{1}<<56)-1;refresh(matrix);
            check(matrix.keyList().size()==3,"built-in matrix reader limit");
            keyboard_matrix_held=0;refresh(matrix);check(matrix.keyList().empty(),"built-in matrix release");
        }
        probe.verify();
    }
    Adafruit_TCA8418::reset();
    M5.board=m5::board_M5CardputerADV;
    Keyboard_Class tca(2);tca.begin();
    {
        NoHeap probe;
        Adafruit_TCA8418::fifo={event(2,2),event(3,2),event(4,2),event(2,2)};
        keyboard_irq(keyboard_irq_arg);
        for(unsigned i=0;i<4;++i)refresh(tca);
        check(tca.keyList().size()==2 && !has(tca,tca.keyList(),4,2),"built-in TCA reader excess/duplicate press");
        Adafruit_TCA8418::fifo={event(2,2,false),event(4,2,false),event(4,2)};
        keyboard_irq(keyboard_irq_arg);
        for(unsigned i=0;i<3;++i)refresh(tca);
        check(tca.keyList().size()==2 && has(tca,tca.keyList(),4,2),"built-in TCA release and subsequent press");
        probe.verify();
    }
}
void incompatibleReader() {
    Keyboard_Class keyboard(1);
    auto reader=std::make_unique<geoputer::AdvKeyboardReader>(2);
    keyboard.begin(std::move(reader));
    check(keyboard.keyList().empty(),"larger injected reader must be rejected");
    NoHeap probe;
    refresh(keyboard);check(keyboard.keyList().empty(),"rejected reader cannot drive state allocation");
    probe.verify();
}
}
void* operator new(std::size_t bytes) {
    if(void* p=std::malloc(bytes?bytes:1)) {if(measuring)++allocations;return p;}
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {if(measuring && p)++deallocations;std::free(p);}
void operator delete(void* p,std::size_t) noexcept {::operator delete(p);}
void* operator new[](std::size_t bytes) {return ::operator new(bytes);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
int main() {
    try {
        advLimit();
        for(size_t limit:{0u,1u,3u,56u,100u})physicalLimit(limit);
        builtinReaders();incompatibleReader();
        std::cout<<"PASS: fixed keyboard limits (0/1/3/56/clamp), all 56 held at boot, excess/duplicate/release, "
                   "Fn/Shift, FIFO recovery, both built-in readers; zero polling allocations and frees\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

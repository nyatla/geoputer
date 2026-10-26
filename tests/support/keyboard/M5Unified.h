#pragma once
namespace m5 {enum board_t {board_M5Cardputer,board_M5CardputerADV};}
struct KeyboardTestBoard {
    m5::board_t board=m5::board_M5Cardputer;
    m5::board_t getBoard() const {return board;}
};
inline KeyboardTestBoard M5;

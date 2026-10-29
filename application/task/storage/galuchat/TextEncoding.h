#pragma once

#include <cstring>
#include "../../../Config.h"
#include "../../../utils/Utf8.h"

namespace geoputer {

// Dictionary and display strings are UTF-8; no character-code conversion.
struct PlaceDisplayFormat {
    char lines[2][config::PlaceFormatBytes]{};
    bool specified = false;
    std::string_view line(unsigned row) const { return lines[row]; }
    // The parser validates syntax for every language before retaining a row.
    bool setLine(unsigned row,std::string_view value) {
        if(row>=2 || value.size()>=sizeof(lines[0]) || value.find('\0')!=std::string_view::npos)return false;
        if(!value.empty())std::memmove(lines[row],value.data(),value.size());
        lines[row][value.size()]='\0';return true;
    }
};
bool validPlaceFormat(std::string_view format);

}  // namespace geoputer

#include "TextEncoding.h"

namespace geoputer {
bool validPlaceFormat(std::string_view format) {
    if (format.size()>=config::PlaceFormatBytes || !validUtf8(format)) return false;
    for (size_t i = 0; i < format.size(); ++i) {
        if (format[i] != '%') continue;
        if (++i == format.size() || (format[i] != '%' && (format[i] < '0' || format[i] > '9'))) return false;
    }
    return true;
}

}  // namespace geoputer

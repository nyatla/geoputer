#pragma once
#include <vector>
struct Point2D_t { int x=0,y=0; bool operator==(const Point2D_t& p) const { return x==p.x && y==p.y; } };
class KeyboardReader {
public:
    virtual ~KeyboardReader()=default;
    virtual void begin() {}
    virtual void update() {}
    const std::vector<Point2D_t>& keyList() const { return _key_list; }
protected:
    std::vector<Point2D_t> _key_list;
};

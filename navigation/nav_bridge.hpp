#pragma once
#include "nav_math.hpp"
#include <cstring>

namespace navbridge {
constexpr unsigned select=0x43574e31,status=0x43574e32,clear=0x43574e33;
constexpr unsigned sidebar=0x43575231;
constexpr const char* provider="local.cubeworld.waypoint";
constexpr const char* consumer="local.cubeworld.region_explorer";
struct Request {unsigned version=1;nav::Point point{};char scope[96]{};char label[96]{};};
struct Sidebar {unsigned version=1;float x=0,y=0,w=0,h=0;};
inline bool valid(const Request& r,const std::string& scope) {
    return r.version==1&&nav::finite(r.point)&&r.scope[95]==0&&r.label[95]==0&&
        scope==r.scope&&std::strlen(r.label)>0&&std::none_of(r.label,r.label+std::strlen(r.label),
            [](unsigned char c){return c<32||c==127;});
}
}

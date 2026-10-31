#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace maplight {
constexpr unsigned controllerGlobal=0x76b1c8,lightFunction=0x448170;
constexpr unsigned mapOpenOffset=0x8006e4,shaderOffset=0x800580;
constexpr unsigned timeOffset=0x800440;
// Match the current loader's day/night convention (game/offsets.h).
constexpr int dayMs=86400000,dayStartMs=6*3600000,nightStartMs=20*3600000;
inline bool night(int timeMs) {
    return timeMs>=0&&timeMs<dayMs&&(timeMs<dayStartMs||timeMs>=nightStartMs);
}
struct Palette {
    std::array<float,4> diffuse,ambient;
    std::array<float,3> direction;
    std::array<float,4> fill;
};
// Same palette/direction used by the native map's landmark pass (0x5ffbb0 range).
inline constexpr Palette daylight{{1,1,1,1},{.2f,.3f,.4f,1},{0,.5f,-1},{.4f,.4f,.4f,1}};
inline bool finite(const Palette& p) {
    const auto valid=[](const auto& a){return std::all_of(a.begin(),a.end(),[](float f){return std::isfinite(f)&&std::abs(f)<=16;});};
    const auto& d=p.direction;
    return valid(p.diffuse)&&valid(p.ambient)&&valid(d)&&valid(p.fill)&&d[0]*d[0]+d[1]*d[1]+d[2]*d[2]>1e-8f;
}
template<class Read> bool mapContext(Read read,unsigned gc,unsigned shader) {
    unsigned char open=0;unsigned expected=0;
    return gc&&shader&&read(gc+mapOpenOffset,&open,1)&&open==1&&
        read(gc+shaderOffset,&expected,4)&&expected==shader;
}
template<class Read> bool palette(Read read,const std::array<unsigned,4>& addresses,Palette& p) {
    return std::all_of(addresses.begin(),addresses.end(),[](unsigned a){return a!=0;})&&
        read(addresses[0],p.diffuse.data(),16)&&read(addresses[1],p.ambient.data(),16)&&
        read(addresses[2],p.direction.data(),12)&&read(addresses[3],p.fill.data(),16)&&finite(p);
}
}

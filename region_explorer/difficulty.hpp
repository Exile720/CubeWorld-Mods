#pragma once
#include <cmath>
#include <cstdint>

namespace explore {
enum class Difficulty {Unknown,Lower,Similar,Higher,City};
// Exact float curve used by the verified client's map labels at 0x43CA60.
inline float levelCurve(unsigned level) {
    return 1.0f-1.0f/((static_cast<float>(level)-1.0f)*.05f+1.0f);
}
inline Difficulty difficulty(unsigned areaLevel,int playerLevel,bool city=false) {
    if(city)return Difficulty::City;
    if(areaLevel>100000||playerLevel<0||playerLevel>100000)return Difficulty::Unknown;
    const float area=levelCurve(areaLevel),player=levelCurve(static_cast<unsigned>(playerLevel));
    // Native 0x4CACC4..0x4CAD27: <= player-.1 stays white; <= player+.1 is cyan.
    return area<=player-.1f?Difficulty::Lower:area<=player+.1f?Difficulty::Similar:Difficulty::Higher;
}
inline const char* difficultyLabel(Difficulty d) {
    switch(d){case Difficulty::Lower:return "较低";case Difficulty::Similar:return "相近";
        case Difficulty::Higher:return "偏高";case Difficulty::City:return "城镇";default:return "难度未知";}
}
// Packed RGBA, matching ImGui's default layout and the native map RGB values.
inline std::uint32_t difficultyColor(Difficulty d) {
    switch(d){case Difficulty::Lower:case Difficulty::City:return 0xffffffffu;
        case Difficulty::Similar:return 0xffffff00u;case Difficulty::Higher:return 0xff3333ffu;
        default:return 0xffb6ac9cu;}
}
}

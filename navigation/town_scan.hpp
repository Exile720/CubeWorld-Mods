#pragma once
#include "cube_mod.hpp"
#include "town.hpp"
#include <cstdint>
#include <unordered_set>

namespace town {
// Read only the same resident shop entities used by the native map. No furniture scan.
inline std::vector<Place> scan(cube::Mod& mod,unsigned gc,nav::Point player) {
    struct Fixed {std::int64_t x,y,z;};
    struct Node {unsigned left,parent,right;};
    const unsigned head=mod.read<unsigned>(gc+0x2e8);
    std::vector<Place> result;
    if(!head)return result;
    std::vector<unsigned> pending{mod.read<unsigned>(head+4)};
    std::unordered_set<unsigned> visited;
    while(!pending.empty()&&visited.size()<4096) {
        const unsigned node=pending.back();pending.pop_back();
        if(!node||node==head||!visited.insert(node).second||!mod.readable(node,0x1c))continue;
        const auto links=mod.read<Node>(node);pending.push_back(links.left);pending.push_back(links.right);
        const unsigned entity=mod.read<unsigned>(node+0x18);
        if(!entity||!mod.readable(entity,0x141))continue;
        const int type=markerType(mod.read<std::uint8_t>(entity+0x60),mod.read<std::uint8_t>(entity+0x140));
        if(!type)continue;
        // +0x10 is the shopkeeper's current position and can drift. The constructor at
        // 0x61009c copies Spawn.fixedXYZ to +0x1c0, the stable original shop location.
        if(!mod.readable(entity+0x1c0,sizeof(Fixed)))continue;
        const auto p=mod.read<Fixed>(entity+0x1c0),live=mod.read<Fixed>(entity+0x10);
        Place place{type,{p.x/65536.0,p.y/65536.0,p.z/65536.0}};
        const nav::Point current{live.x/65536.0,live.y/65536.0,live.z/65536.0};
        if(valid(place)&&nav::finite(current)&&nav::distance(current,place.position)<64&&
           std::abs(current.z-place.position.z)<64&&nav::distance(player,place.position)<2200)
            merge(result,place);
    }
    return result;
}
}

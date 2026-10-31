#pragma once
#include "nav_math.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

namespace explore {
constexpr unsigned recordSize=0x68,chunkPlaces=0x34000,placesPerChunk=64;
constexpr int chunkBlocks=16384,placeBlocks=2048,maxChunk=1024;
using Reader=std::function<bool(unsigned,void*,unsigned)>;
enum class BossSource {Unknown,Confirmed,Predicted,Special};
struct BossInfo {BossSource source=BossSource::Unknown;unsigned combatClass=0;};
struct Place {
    unsigned id=0,category=0,subtype=0,seed=0,level=0,mission=0,missionSeed=0,missionRace=0,missionLevel=0,state=0;
    BossInfo boss{};
    nav::Point position{};
    std::string name;
    bool city() const {return category==1;}
    // A cleared current mission ID can leave a native progress/completion state.
    // Keep that state visible, without claiming a current task or full history.
    bool challenge() const {return mission!=0||state==1||state==2;}
    bool done() const {return challenge()&&state==2;}
};
struct Snapshot {std::vector<Place> places;unsigned chunks=0,rejected=0;};
template<class T> inline T field(const unsigned char* bytes,unsigned offset) {
    T value{};std::memcpy(&value,bytes+offset,sizeof(value));return value;
}
inline bool parse(const unsigned char* bytes,unsigned blockX,unsigned blockY,double z,Place& out) {
    Place p;p.id=(blockX<<16)|blockY;p.category=field<unsigned>(bytes,0x18);
    // The native landmark pass excludes 0 and 10. Unknown types stay generic.
    if(!p.category||p.category==10||p.category>64)return false;
    p.subtype=field<unsigned>(bytes,0x1c);p.seed=field<unsigned>(bytes,0x20);p.level=field<unsigned>(bytes,0x24);
    p.position={field<std::int64_t>(bytes,0)/65536.0,field<std::int64_t>(bytes,8)/65536.0,z};
    p.missionSeed=field<unsigned>(bytes,0x30);p.mission=field<unsigned>(bytes,0x34);
    p.missionRace=field<unsigned>(bytes,0x38);p.missionLevel=field<unsigned>(bytes,0x3c);p.state=bytes[0x41];
    if(!nav::finite(p.position)||p.position.x<0||p.position.y<0||
       p.position.x>=16777216||p.position.y>=16777216||p.level>100000)return false;
    if(std::abs(std::floor(p.position.x/placeBlocks)-blockX)>1||
       std::abs(std::floor(p.position.y/placeBlocks)-blockY)>1)return false;
    if(p.state>2)return false;
    out=std::move(p);return true;
}
// Read the game's resident coarse map only. Never invokes a generator or changes discovery flags.
inline Snapshot scan(const Reader& read,unsigned gc,nav::Point player) {
    Snapshot out;if(!nav::finite(player)||player.x<0||player.y<0||
        player.x>=16777216||player.y>=16777216)return out;
    const int cx=static_cast<int>(player.x/chunkBlocks),cy=static_cast<int>(player.y/chunkBlocks);
    std::array<unsigned char,placesPerChunk*recordSize> bytes{};
    for(int x=std::max(0,cx-3);x<=std::min(maxChunk-1,cx+3);++x)
        for(int y=std::max(0,cy-3);y<=std::min(maxChunk-1,cy+3);++y) {
            unsigned chunk=0;
            if(!read(gc+0x800d44+0xb0+static_cast<unsigned>(x*1024+y)*4,&chunk,4)||!chunk)continue;
            if(!read(chunk+chunkPlaces,bytes.data(),static_cast<unsigned>(bytes.size())))continue;
            ++out.chunks;
            for(unsigned i=0;i<placesPerChunk;++i) {
                const auto* record=bytes.data()+i*recordSize;Place p;
                if(parse(record,x*8+i/8,y*8+i%8,player.z,p))out.places.push_back(std::move(p));
                else if(field<unsigned>(record,0x18)!=0&&field<unsigned>(record,0x18)!=10)++out.rejected;
            }
        }
    return out;
}
using Names=std::array<std::string,40>;
// Native place-name syllables are read from the user's verified executable, not redistributed.
constexpr std::array<unsigned,40> nameRvas={
    3262332,3262340,3262348,3262356,3262364,3262372,3262384,3262392,3262404,3262416,
    3262504,3262512,3262520,3262528,3262536,3262548,3262560,3262568,3262580,3262588,
    3262424,3262432,3262440,3262448,3262456,3262464,3262472,3262480,3262488,3262496,
    3262596,3262604,3262612,3262620,3262632,3262640,3262648,3134054,3262668,3262676};
inline std::string name(unsigned seed,const Names& names) {
    // NameGen_initFirstNameTables, default race (-1), native uint32 overflow.
    const auto& first=names[(seed/7+seed*13)%20];const auto& last=names[20+(seed*7+seed/10)%20];
    return first.empty()||last.empty()?std::string{}:first+last;
}
inline const char* kind(const Place& p) {
    if(p.city())return "城镇";
    if(!p.mission&&p.state)return p.state==2?"留存完成":"留存进度";
    if(p.challenge())return p.done()?"已完成":p.state==1?"挑战中":"未挑战";
    return "地标";
}
// Verified from World+0x800124's (category, subtype) name-key tree in this build.
// In particular, category 14 alone cannot distinguish a palace from a pyramid.
inline const char* placeType(const Place& p) {
    if(p.category==5) {
        if(p.subtype<4)return "废墟";
        return p.subtype==4?"墓地":"地标";
    }
    if(p.category==9)return p.subtype==0?"洞穴":"地标";
    if(p.category==14) {
        static constexpr const char* names[]={"城堡","废墟","地下墓穴","宫殿","神庙","金字塔"};
        return p.subtype<6?names[p.subtype]:"地标";
    }
    if(p.subtype>9)return "地标";
    switch(p.category) {
    case 1:return "城镇";case 2:return "山脉";case 3:return "森林";case 4:return "湖泊";
    case 6:return "峡谷";case 7:return "山谷";case 8:return "环形坑";case 11:return "巨岩";
    case 12:return "巨树";case 13:return "山峰";case 15:return "岛屿";
    default:return "地标";
    }
}
inline std::string title(const Place& p) {
    return std::string(placeType(p))+" · "+(p.name.empty()?"未命名地点":p.name);
}
inline std::string reviewedTargetLabel(const std::string& label) {
    const std::pair<const char*,const char*> prefixes[]={
        {"树木 · ","巨树 · "},{"岩石 · ","巨岩 · "},{"陨石坑 · ","环形坑 · "}};
    for(const auto& prefix:prefixes)if(label.rfind(prefix.first,0)==0)
        return std::string(prefix.second)+label.substr(std::strlen(prefix.first));
    return label;
}
struct Counts {unsigned total=0,done=0,retained=0;};
inline Counts counts(const std::vector<Place>& places) {
    Counts c;c.total=static_cast<unsigned>(places.size());
    for(const auto& p:places){if(p.done())++c.done;if(!p.mission&&p.state)++c.retained;}
    return c;
}
inline std::vector<Place> rows(const std::vector<Place>& places,nav::Point player,int tab) {
    std::vector<Place> out;
    for(const auto& p:places)if((tab==0&&p.city())||(tab==1&&p.challenge()&&!p.city())||
        (tab==2&&!p.city()&&!p.challenge()))out.push_back(p);
    std::sort(out.begin(),out.end(),[&](const Place& a,const Place& b) {
        if(tab==1&&a.done()!=b.done())return !a.done();
        const auto da=nav::distance(player,a.position),db=nav::distance(player,b.position);
        return da==db?a.id<b.id:da<db;
    });return out;
}
inline bool safeLabel(const std::string& name) {
    if(name.empty()||name.size()>95)return false;
    return std::none_of(name.begin(),name.end(),[](unsigned char c){return c<32||c==127;});
}
inline std::string encodeTarget(nav::Point p,const std::string& label) {
    return "CW_EXPLORE_1\n"+nav::encode(p)+'\n'+label;
}
inline bool decodeTarget(const std::string& data,nav::Point& p,std::string& label) {
    const auto split=data.find('\n',13);nav::Point next;
    if(data.size()>512||data.rfind("CW_EXPLORE_1\n",0)!=0||split==std::string::npos||
       !nav::decode(data.substr(13,split-13),next)||!safeLabel(data.substr(split+1)))return false;
    p=next;label=data.substr(split+1);return true;
}
}

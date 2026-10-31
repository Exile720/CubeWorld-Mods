#pragma once
#include "explore.hpp"
#include <map>
#include <set>

namespace explore {
inline const char* combatClassName(unsigned value) {
    switch(value) {case 1:return "战士";case 2:return "游侠";case 3:return "法师";case 4:return "盗贼";default:return "职业未知";}
}
inline std::string bossLabel(const Place& p) {
    if(p.done())return "首领职业未记录";
    if(p.boss.source==BossSource::Special)return "特殊首领 · 已确认";
    if((p.boss.source==BossSource::Confirmed||p.boss.source==BossSource::Predicted)&&p.boss.combatClass>=1&&p.boss.combatClass<=4)
        return std::string("首领：")+combatClassName(p.boss.combatClass)+(p.boss.source==BossSource::Confirmed?" · 已确认":" · 预测");
    return "首领职业未知";
}
namespace boss {
// This list follows the regular archetype branch at native 0x5C7730. Creatures
// with their own moves stay special; a nonzero class byte alone is insufficient.
inline bool regular(unsigned species) {
    if(species<=18&&species!=6)return true;
    switch(species) {
    case 44:case 48:case 49:case 51:case 52:case 54:case 77:case 78:case 79:
    case 83:case 84:case 94:case 96:case 97:case 110:return true;
    default:return false;
    }
}
inline unsigned predict(unsigned taskSeed,std::uint64_t id,unsigned species,unsigned flags,unsigned priorClass) {
    // Mission type 5 re-seeds immediately before initialization. Do not call the
    // game's srand/rand: this local uint32 calculation leaves its RNG untouched.
    if(!regular(species)||(flags&0x1200)!=0x1200||(flags&0x58)||priorClass>4)return 0;
    if(species==97)return 4; // The native Vampire override follows the random roll.
    const std::uint32_t state=(taskSeed+static_cast<std::uint32_t>(id))*214013u+2531011u;
    return ((state>>16)&0x7fffu)%4+1;
}
inline bool readAt(const Reader& read,std::uint64_t address,void* out,unsigned size) {
    return address>=0x10000&&address+size<=0x100000000ull&&read(static_cast<unsigned>(address),out,size);
}
template<class T>inline bool readValue(const Reader& read,std::uint64_t address,T& out) {return readAt(read,address,&out,sizeof(out));}
inline unsigned zoneAt(const Reader& read,unsigned gc,unsigned x,unsigned y) {
    if(x>=65536||y>=65536)return 0;
    unsigned region=0,zone=0;
    if(!readValue(read,std::uint64_t(gc)+0x2e4+0xbc+((x/64)*1024+y/64)*4,region)||!region)return 0;
    return readValue(read,std::uint64_t(region)+0x10018+((x%64)*64+y%64)*4,zone)?zone:0;
}
inline bool sameTask(const Reader& read,unsigned gc,const Place& p) {
    const unsigned x=p.id>>16,y=p.id&0xffff;unsigned region=0;
    if(x>=8192||y>=8192||!readValue(read,std::uint64_t(gc)+0x2e4+0xbc+((x/8)*1024+y/8)*4,region)||!region)return false;
    std::array<unsigned char,recordSize> bytes{};Place live;
    if(!readAt(read,std::uint64_t(region)+0x14018+((x%8)*8+y%8)*recordSize,bytes.data(),recordSize)||
       !parse(bytes.data(),x,y,p.position.z,live))return false;
    return live.category==p.category&&live.subtype==p.subtype&&live.seed==p.seed&&
        live.mission==5&&live.mission==p.mission&&live.missionSeed==p.missionSeed&&
        live.missionRace==p.missionRace&&live.missionLevel==p.missionLevel&&!live.done();
}
enum class Found {Missing,Present,Unreadable};
inline Found entityAt(const Reader& read,unsigned gc,std::uint64_t id,unsigned& entity) {
    unsigned head=0,node=0;
    if(!readValue(read,std::uint64_t(gc)+0x2e8,head)||!head||!readValue(read,std::uint64_t(head)+4,node))return Found::Unreadable;
    std::set<unsigned> seen;
    while(node!=head) {
        std::array<unsigned char,0x1c> bytes{};
        if(!node||seen.size()>=64||!seen.insert(node).second||!readAt(read,node,bytes.data(),static_cast<unsigned>(bytes.size()))||bytes[0xd])return Found::Unreadable;
        const auto key=field<std::uint64_t>(bytes.data(),0x10);
        if(key==id){entity=field<unsigned>(bytes.data(),0x18);return entity?Found::Present:Found::Unreadable;}
        node=field<unsigned>(bytes.data(),id<key?0:8);
    }
    return Found::Missing;
}
struct Stats {unsigned zones=0,spawns=0,candidates=0,confirmed=0,predicted=0,mismatches=0;bool truncated=false;};
// Scan only resident detail-zone vectors near the player, on the open challenge
// tab. A boss must carry BOTH native mission and giant-leader spawn bits. The
// original spawn ID, home area and current task must all match before display.
using Observer=std::function<void(const Place&,std::uint64_t,unsigned)>;
inline Stats enrich(Snapshot& snapshot,const Reader& read,unsigned gc,nav::Point player,bool allowPredictions=true,const Observer& observe={},unsigned radius=3) {
    Stats stats;std::map<unsigned,Place*> tasks;
    for(auto& p:snapshot.places){p.boss={};if(!p.city()&&p.mission==5&&!p.done())tasks.emplace(p.id,&p);}
    if(tasks.empty()||!nav::finite(player)||player.x<0||player.y<0||player.x>=16777216||player.y>=16777216)return stats;
    struct Candidate {unsigned count=0;BossInfo info{};std::uint64_t id=0;unsigned entity=0;};std::map<unsigned,Candidate> results;
    std::map<unsigned,bool> validTasks;
    const int zx=static_cast<int>(player.x/256),zy=static_cast<int>(player.y/256);
    const int range=static_cast<int>(std::min(radius,3u));
    for(int x=std::max(0,zx-range);x<=std::min(65535,zx+range);++x)
        for(int y=std::max(0,zy-range);y<=std::min(65535,zy+range);++y) {
            const auto task=tasks.find((static_cast<unsigned>(x/8)<<16)|static_cast<unsigned>(y/8));
            if(task==tasks.end())continue;
            const auto zone=zoneAt(read,gc,x,y);if(!zone)continue;
            std::array<unsigned,2> vector{};
            if(!readAt(read,std::uint64_t(zone)+0x18,vector.data(),sizeof(vector))||vector[1]<vector[0]||(vector[1]-vector[0])%4)continue;
            const auto count=(vector[1]-vector[0])/4;
            if(count>4096||stats.spawns+count>8192){stats.truncated=true;continue;}
            std::vector<unsigned> spawns(count);
            if(count&&!readAt(read,vector[0],spawns.data(),count*4))continue;
            ++stats.zones;stats.spawns+=count;
            for(unsigned i=0;i<count;++i) {
                std::array<unsigned char,0x80> spawn{};
                if(!spawns[i]||!readAt(read,spawns[i],spawn.data(),static_cast<unsigned>(spawn.size())))continue;
                const auto* b=spawn.data();const auto flags=field<std::uint16_t>(b,0x7a);
                if(field<unsigned>(b,0x28)!=1||(flags&0x1200)!=0x1200)continue;
                const auto id=field<std::uint64_t>(b,0x48);
                const auto expected=((std::uint64_t(y)<<16)+x)*256+i;
                const nav::Point origin{field<std::int64_t>(b,0x10)/65536.0,field<std::int64_t>(b,0x18)/65536.0,field<std::int64_t>(b,0x20)/65536.0};
                if(id!=expected||!nav::finite(origin)||std::floor(origin.x/placeBlocks)!=x/8||std::floor(origin.y/placeBlocks)!=y/8)continue;
                auto& p=*task->second;
                if(!validTasks.count(p.id))validTasks[p.id]=sameTask(read,gc,p);
                if(!validTasks[p.id])continue;
                auto& result=results[p.id];++result.count;++stats.candidates;
                const unsigned species=field<unsigned>(b,0x2c),predicted=predict(p.missionSeed,id,species,flags,b[0x30]);
                unsigned entity=0;const auto found=entityAt(read,gc,id,entity);
                if(found==Found::Missing) {
                    if(predicted)result.info={BossSource::Predicted,predicted};
                    continue;
                }
                std::array<unsigned char,0x1a9> creature{};
                if(found!=Found::Present||!readAt(read,entity,creature.data(),static_cast<unsigned>(creature.size())))continue;
                const auto* c=creature.data();const auto health=field<float>(c,0x16c);
                if(c[0x60]!=1||field<unsigned>(c,0x64)!=species||field<unsigned>(c,0x190)!=field<unsigned>(b,0x34)||
                   !std::isfinite(health)||health<=0||c[0x1a8]!=b[0x58])continue;
                if(regular(species)&&c[0x140]>=1&&c[0x140]<=4) {
                    result.info={BossSource::Confirmed,c[0x140]};
                    if(predicted&&predicted!=c[0x140])++stats.mismatches;
                } else if(!regular(species))result.info={BossSource::Special,0};
                result.id=id;result.entity=entity;
            }
        }
    for(const auto& result:results) {
        // Multiple leaders, an incomplete vector scan or a live mismatch must
        // never silently turn into a single confident prediction.
        if(result.second.count!=1||stats.truncated)continue;
        const auto info=result.second.info;
        if((stats.mismatches||!allowPredictions)&&info.source==BossSource::Predicted)continue;
        tasks.at(result.first)->boss=info;
        if(observe&&result.second.entity)observe(*tasks.at(result.first),result.second.id,result.second.entity);
        if(info.source==BossSource::Confirmed||info.source==BossSource::Special)++stats.confirmed;
        if(info.source==BossSource::Predicted)++stats.predicted;
    }
    return stats;
}
}
}

#pragma once
#include "travel.hpp"
#include "boss.hpp"
#include <windows.h>

namespace visits {
using Reader=explore::Reader;
using Rebase=std::function<unsigned(unsigned)>;
inline std::string utf8(const wchar_t* data,unsigned count){
    const int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,data,int(count),nullptr,0,nullptr,nullptr);
    if(bytes<=0||bytes>256)return {};std::string text(bytes,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,data,int(count),text.data(),bytes,nullptr,nullptr);return text;
}
inline std::string wideString(const Reader& read,unsigned address){
    std::array<unsigned char,24> bytes{};if(!explore::boss::readAt(read,address,bytes.data(),unsigned(bytes.size())))return {};
    const auto n=explore::field<unsigned>(bytes.data(),0x10),capacity=explore::field<unsigned>(bytes.data(),0x14);
    if(!n||n>64||capacity<n||capacity>4096)return {};
    const auto data=capacity<8?address:explore::field<unsigned>(bytes.data(),0);std::array<wchar_t,65> text{};
    if(!explore::boss::readAt(read,data,text.data(),(n+1)*2)||text[n])return {};
    for(unsigned i=0;i<n;++i)if(text[i]<32)return {};return utf8(text.data(),n);
}
inline std::pair<unsigned,unsigned> nameTables(unsigned species){
    // Native NameGen switch at 0x5A0ED0: first/last UTF-16 string tables.
    switch(species){
    case 0:return {0x76bf90,0x76c080};case 1:return {0x76c170,0x76c260};
    case 2:case 18:case 43:case 83:return {0x76bbd0,0x76bcc0};
    case 3:case 45:case 84:return {0x76bdb0,0x76bea0};case 4:return {0x76c350,0x76c440};
    case 5:return {0x76c530,0x76c620};case 7:return {0x76c710,0x76c800};case 8:return {0x76c8f0,0x76c9e0};
    case 9:return {0x76cad0,0x76cbc0};case 10:return {0x76ccb0,0x76cda0};case 11:return {0x76ce90,0x76cf80};
    case 12:return {0x76d070,0x76d160};case 13:return {0x76d610,0x76d700};case 14:return {0x76d7f0,0x76d8e8};
    case 15:return {0x76d250,0x76d340};case 16:return {0x76d430,0x76d520};default:return {0x76b808,0x76b9f0};}
}
inline std::string bossName(const Reader& read,const Rebase& rebase,unsigned entity,std::uint64_t expected){
    std::uint64_t id=0;unsigned species=0;std::uint16_t flags=0;std::array<char,16> custom{};
    if(!explore::boss::readValue(read,std::uint64_t(entity)+8,id)||id!=expected||!explore::boss::readValue(read,std::uint64_t(entity)+0x64,species)||
       !explore::boss::readAt(read,std::uint64_t(entity)+0x1168,custom.data(),unsigned(custom.size())))return {};
    const auto end=std::find(custom.begin(),custom.end(),char(0));if(end==custom.end())return {};
    if(end!=custom.begin()){
        std::string name(custom.begin(),end);wchar_t converted[32]{};
        if(std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32||c==127;}))return {};
        // Native custom ASCII/UTF-8 names are copied; unverified encodings stay unknown.
        const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),int(name.size()),converted,32);
        return count>0?utf8(converted,unsigned(count)):std::string{};
    }
    if(!explore::boss::readValue(read,std::uint64_t(entity)+0x7e,flags)||!(flags&0x200))return {};
    const auto tables=nameTables(species);const unsigned seed=unsigned(id),count=tables.first==0x76b808?20:10;
    const auto first=wideString(read,rebase(tables.first)+((seed/7+seed*13)%count)*24),last=wideString(read,rebase(tables.second)+((seed*7+seed/10)%count)*24);
    return first.empty()||last.empty()||first.size()+last.size()>256?std::string{}:first+last;
}
inline std::uint64_t taskKey(const explore::Place& p){
    if(!p.mission)return 0;std::uint64_t value=14695981039346656037ull;
    for(unsigned item:{p.category,p.subtype,p.seed,p.mission,p.missionSeed,p.missionRace,p.missionLevel})for(unsigned i=0;i<4;++i){value^=(item>>(i*8))&255;value*=1099511628211ull;}return value;
}
inline std::uint64_t placeKey(const explore::Place& p){
    auto plain=p;plain.mission=0xffffffffu;plain.missionSeed=plain.missionRace=plain.missionLevel=0;return taskKey(plain);
}
inline bool specialSpecies(unsigned species){
    switch(species){case 0x65:case 0x6b:case 0x6c:case 0x6d:case 0x72:case 0x73:case 0x74:case 0x75:case 0x76:case 0x77:return true;default:return false;}
}
inline bool placeAt(const Reader& read,unsigned gc,unsigned x,unsigned y,double z,explore::Place& place){
    if(x>=8192||y>=8192)return false;unsigned chunk=0;
    std::array<unsigned char,explore::recordSize> bytes{};
    return explore::boss::readValue(read,std::uint64_t(gc)+0x800d44+0xb0+((x/8)*1024+y/8)*4,chunk)&&chunk&&
        explore::boss::readAt(read,std::uint64_t(chunk)+explore::chunkPlaces+((x%8)*8+y%8)*explore::recordSize,bytes.data(),unsigned(bytes.size()))&&
        explore::parse(bytes.data(),x,y,z,place)&&!place.city()&&std::strcmp(explore::placeType(place),"地标");
}
struct SeenBoss {explore::Place place;travel::Point origin;std::uint64_t id=0;unsigned combatClass=0;bool special=false;std::string name;};
// Observe actual nearby entities independently of movement, combat and mission
// type. Original spawn identity associates a leader with its home landmark.
inline std::vector<SeenBoss> nearbyBosses(const Reader& read,const Rebase& rebase,unsigned gc,travel::Point player){
    std::vector<SeenBoss> out;if(!travel::finite(player)||player.x<0||player.y<0)return out;
    struct Candidate {SeenBoss boss;bool aimed=false;};std::map<unsigned,std::vector<Candidate>> found;
    std::uint64_t aimedId=0;explore::boss::readValue(read,std::uint64_t(gc)+0x800a70,aimedId);
    const int zx=int(player.x/256),zy=int(player.y/256);unsigned total=0;bool truncated=false;
    for(int x=std::max(0,zx-3);x<=std::min(65535,zx+3);++x)for(int y=std::max(0,zy-3);y<=std::min(65535,zy+3);++y){
        const auto zone=explore::boss::zoneAt(read,gc,x,y);if(!zone)continue;std::array<unsigned,2> range{};
        if(!explore::boss::readAt(read,std::uint64_t(zone)+0x18,range.data(),sizeof(range))||range[1]<range[0]||(range[1]-range[0])%4)continue;
        const auto count=(range[1]-range[0])/4;if(count>4096||total+count>8192){truncated=true;continue;}total+=count;
        std::vector<unsigned> pointers(count);if(count&&!explore::boss::readAt(read,range[0],pointers.data(),count*4))continue;
        for(unsigned i=0;i<count;++i){
            std::array<unsigned char,0x80> spawn{};if(!explore::boss::readAt(read,pointers[i],spawn.data(),unsigned(spawn.size())))continue;
            const auto* b=spawn.data();const auto flags=explore::field<std::uint16_t>(b,0x7a);const auto species=explore::field<unsigned>(b,0x2c);
            if(explore::field<unsigned>(b,0x28)!=1||(!(flags&0x200)&&!specialSpecies(species)))continue;
            const auto id=explore::field<std::uint64_t>(b,0x48);if(id!=((std::uint64_t(y)<<16)+x)*256+i)continue;
            const auto origin=travel::units(explore::field<travel::Fixed>(b,0x10));
            if(!travel::finite(origin)||std::floor(origin.x/2048)!=x/8||std::floor(origin.y/2048)!=y/8)continue;
            unsigned entity=0;if(explore::boss::entityAt(read,gc,id,entity)!=explore::boss::Found::Present)continue;
            std::array<unsigned char,0x1a9> data{};if(!explore::boss::readAt(read,entity,data.data(),unsigned(data.size())))continue;
            const auto* c=data.data();const auto position=travel::units(explore::field<travel::Fixed>(c,0x10));const auto hp=explore::field<float>(c,0x16c);
            if(explore::field<std::uint64_t>(c,8)!=id||c[0x60]!=1||explore::field<unsigned>(c,0x64)!=species||
                explore::field<unsigned>(c,0x190)!=explore::field<unsigned>(b,0x34)||c[0x1a8]!=b[0x58]||!std::isfinite(hp)||hp<0||
                (!(explore::field<std::uint16_t>(c,0x7e)&0x200)&&!specialSpecies(species))||!travel::finite(position)||travel::horizontal(position,player)>512||std::abs(position.z-player.z)>256)continue;
            explore::Place place;if(!placeAt(read,gc,x/8,y/8,player.z,place))continue;
            // Mission leaders still require the current detailed task identity;
            // ordinary/static leaders do not depend on a mission ID existing.
            if((flags&0x1000)&&place.mission==5&&!explore::boss::sameTask(read,gc,place))continue;
            const bool special=!explore::boss::regular(species);const unsigned cls=!special&&c[0x140]>=1&&c[0x140]<=4?c[0x140]:0;
            found[place.id].push_back({{place,origin,id,cls,special,bossName(read,rebase,entity,id)},id==aimedId});
        }
    }
    if(truncated)return out;
    for(const auto& v:found){const auto& candidates=v.second;const auto aimed=std::find_if(candidates.begin(),candidates.end(),[](const auto& c){return c.aimed;});
        if(aimed!=candidates.end())out.push_back(aimed->boss);else if(candidates.size()==1)out.push_back(candidates[0].boss);}
    return out;
}
inline travel::VisitInfo information(const SeenBoss& seen){
    const auto& p=seen.place;return {p.id+1,p.level,explore::title(p),p.mission?taskKey(p):placeKey(p),seen.id,seen.combatClass,seen.special,seen.name};
}
inline bool matches(const travel::VisitInfo& info,const explore::Place& p){return info.placeId==p.id+1&&(!p.mission||info.taskKey==taskKey(p));}
struct Dwell {
    unsigned place=0;travel::Point first{};unsigned long long since=0;
    bool ready(unsigned id,travel::Point point,unsigned long long now){
        if(!place||place!=id||travel::horizontal(point,first)>4||std::abs(point.z-first.z)>2){place=id;first=point;since=now;return false;}
        return now>=since&&now-since>=2500;
    }
};
inline travel::VisitInfo observe(const explore::Place& place,const travel::VisitInfo& previous,const Reader& read,const Rebase& rebase,unsigned gc,travel::Point point,bool readBoss,bool* bossNear=nullptr){
    if(bossNear)*bossNear=false;
    auto info=previous;const auto key=taskKey(place);
    info.placeId=place.id+1;info.level=place.level;info.title=explore::title(place);
    if(key&&key!=info.taskKey){info.taskKey=key;info.bossId=0;info.bossName.clear();info.bossClass=0;info.special=false;}
    // When a task is cleared, retain prior confirmed observations as history.
    if(readBoss&&place.mission==5&&!place.done()){
        explore::Snapshot snapshot;snapshot.places.push_back(place);
        explore::boss::enrich(snapshot,read,gc,{point.x,point.y,point.z},false,[&](const explore::Place& verified,std::uint64_t id,unsigned entity){
            std::uint64_t liveId=0;if(!explore::boss::readValue(read,std::uint64_t(entity)+8,liveId)||liveId!=id)return;
            if(bossNear){travel::Fixed fixed{};if(explore::boss::readAt(read,std::uint64_t(entity)+0x10,&fixed,sizeof(fixed))){const auto position=travel::units(fixed);*bossNear=travel::finite(position)&&travel::horizontal(position,point)<=96&&std::abs(position.z-point.z)<=32;}}
            const auto name=bossName(read,rebase,entity,id);
            if(info.bossId!=id)info.bossName.clear();info.bossId=id;info.bossClass=verified.boss.combatClass;info.special=verified.boss.source==explore::BossSource::Special;
            if(!name.empty())info.bossName=name;
        },1);
    }
    return info;
}
inline std::string classLabel(const travel::VisitInfo& v){return v.special?"特殊首领":v.bossClass?explore::combatClassName(v.bossClass):"职业未知";}
inline bool same(const travel::VisitInfo& a,const travel::VisitInfo& b){return a.placeId==b.placeId&&a.level==b.level&&a.title==b.title&&a.taskKey==b.taskKey&&a.bossId==b.bossId&&a.bossClass==b.bossClass&&a.special==b.special&&a.bossName==b.bossName;}
inline std::string text(const travel::VisitInfo& v){
    if(!v.placeId)return {};
    std::string result="自动记录："+v.title+" · 地区等级 "+std::to_string(v.level);
    if(v.bossId)return result+"\nBoss："+(v.bossName.empty()?"名字未知":v.bossName)+" · "+classLabel(v)+"（上次现场记录）";
    return result+(v.taskKey?"\nBoss：名字未知 · 职业未知":"\nBoss：未发现可确认记录");
}
}

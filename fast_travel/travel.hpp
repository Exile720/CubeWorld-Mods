#pragma once
#include "cube_sdk.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace travel {
constexpr size_t maxStations=32;
constexpr size_t maxNoteBytes=1024,maxDataBytes=131072;
struct Point {double x=0,y=0,z=0;};
struct Fixed {std::int64_t x=0,y=0,z=0;};
struct VisitInfo {unsigned placeId=0,level=0;std::string title;std::uint64_t taskKey=0,bossId=0;unsigned bossClass=0;bool special=false;std::string bossName;};
struct Station {std::string name;Point point;unsigned cityId=0;std::string note;VisitInfo visit;};
inline Fixed fixed(Point p){return {std::llround(p.x*65536),std::llround(p.y*65536),std::llround(p.z*65536)};}
inline Point units(Fixed p){return {p.x/65536.0,p.y/65536.0,p.z/65536.0};}
inline Point position(const CubePlayer& p){return {p.x,p.y,p.z};}
inline bool finite(Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::abs(p.x)<=16777216&&std::abs(p.y)<=16777216&&std::abs(p.z)<100000;}
inline double horizontal(Point a,Point b){return std::hypot(double(a.x)-b.x,double(a.y)-b.y);}
inline bool label(const std::string& s){return !s.empty()&&s.size()<=64&&s.find_first_not_of(' ')!=std::string::npos&&std::none_of(s.begin(),s.end(),[](unsigned char c){return c<32||c==127;});}
inline bool note(const std::string& s){return s.size()<=maxNoteBytes&&std::none_of(s.begin(),s.end(),[](unsigned char c){return (c<32&&c!='\n'&&c!='\r'&&c!='\t')||c==127;});}
inline bool validVisit(const VisitInfo& v){
    const auto plain=[](const std::string& s){return s.size()<=256&&std::none_of(s.begin(),s.end(),[](unsigned char c){return c<32||c==127;});};
    if(v.placeId>0x20000000u||v.level>100000||!plain(v.title)||!plain(v.bossName)||v.bossClass>4)return false;
    if(!v.placeId)return !v.level&&v.title.empty()&&!v.taskKey&&!v.bossId&&!v.bossClass&&!v.special&&v.bossName.empty();
    if(v.title.empty())return false;
    return v.bossId?(v.taskKey&&(!v.special||!v.bossClass)):(!v.bossClass&&!v.special&&v.bossName.empty());
}
inline bool single(const CubeSession& s){return s.gameState==CUBE_STATE_IN_WORLD&&s.hasNetwork&&s.networkMode==CUBE_NET_SINGLEPLAYER&&s.playerCount==1;}
inline bool standing(const CubePlayer& p){return p.address&&p.alive&&p.health>0&&p.hasPosition&&p.hasState&&p.onGround&&p.action==CUBE_ACTION_IDLE&&!p.knockedDown&&finite(position(p))&&std::isfinite(p.velX)&&std::isfinite(p.velY)&&std::isfinite(p.velZ)&&std::hypot(p.velX,p.velY)<.2&&std::abs(p.velZ)<.2;}
inline std::string encode(const std::vector<Station>& stations){
    std::ostringstream s;s.imbue(std::locale::classic());s<<"CWTRAVEL4\n"<<stations.size()<<'\n'<<std::setprecision(17);
    for(const auto& v:stations){const auto& a=v.visit;s<<std::quoted(v.name)<<' '<<v.point.x<<' '<<v.point.y<<' '<<v.point.z<<' '<<v.cityId<<' '<<std::quoted(v.note)<<' '<<a.placeId<<' '<<a.level<<' '<<std::quoted(a.title)<<' '<<a.taskKey<<' '<<a.bossId<<' '<<a.bossClass<<' '<<(a.special?1:0)<<' '<<std::quoted(a.bossName)<<'\n';}return s.str();
}
inline bool decode(const std::string& text,std::vector<Station>& out){
    if(text.size()>maxDataBytes)return false;std::istringstream s(text);s.imbue(std::locale::classic());std::string magic;size_t count=0;
    if(!std::getline(s,magic)||(magic!="CWTRAVEL1"&&magic!="CWTRAVEL2"&&magic!="CWTRAVEL3"&&magic!="CWTRAVEL4")||!(s>>count)||count>maxStations)return false;
    std::vector<Station> next;for(size_t i=0;i<count;++i){Station v;if(!(s>>std::quoted(v.name)>>v.point.x>>v.point.y>>v.point.z)||(magic!="CWTRAVEL1"&&!(s>>v.cityId))||((magic=="CWTRAVEL3"||magic=="CWTRAVEL4")&&!(s>>std::quoted(v.note)))||!label(v.name)||!note(v.note)||!finite(v.point)||v.cityId>0x20000000u)return false;
        if(magic=="CWTRAVEL4"){int special=0;auto& a=v.visit;if(!(s>>a.placeId>>a.level>>std::quoted(a.title)>>a.taskKey>>a.bossId>>a.bossClass>>special>>std::quoted(a.bossName))||special<0||special>1)return false;a.special=special!=0;}
        if(!validVisit(v.visit))return false;
        for(const auto& old:next)if(old.name==v.name||(v.cityId&&v.cityId==old.cityId))return false;next.push_back(v);}
    s>>std::ws;if(!s.eof())return false;out=std::move(next);return true;
}
enum class Step {None,Hold,Arrived,Return};
struct Journey {
    bool active=false;Point source{},destination{};unsigned player=0;unsigned long long started=0;unsigned groundFrames=0;
    void begin(const CubePlayer& p,Point to,unsigned long long now){active=true;source=position(p);destination=to;player=p.address;started=now;groundFrames=0;}
    Step step(const CubePlayer& p,unsigned long long now){
        if(!active)return Step::None;
        // A character switch, unavailable player or death must never move a new entity.
        if(!p.address||p.address!=player||!p.alive||!p.hasPosition||!finite(position(p))){active=false;return Step::None;}
        if(now-started<1500)return Step::Hold;
        if(now-started>8000||p.z<destination.z-2||horizontal(position(p),destination)>4){active=false;return Step::Return;}
        if(p.hasState&&p.onGround&&horizontal(position(p),destination)<=1.5&&std::abs(p.z-destination.z)<1){
            if(++groundFrames>=2){active=false;return Step::Arrived;}
        }else groundFrames=0;
        return Step::None;
    }
};
}

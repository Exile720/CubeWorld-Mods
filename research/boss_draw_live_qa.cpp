// Temporary real-game test helper. Never installed in the user's playtest.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "cube_mod.hpp"
#include <fstream>
#include <string>
#include <vector>
#include <cmath>
namespace {
bool allowed=false,boosted=false;ULONGLONG next=0;
bool isolated(){
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
    if(_wcsicmp(path,L"C:\\Projects\\CubeWorld-Mods\\build\\boss-draw-live-qa-20261030\\Cube.exe"))return false;
    std::ifstream f(L"C:\\Projects\\CubeWorld-Mods\\build\\boss-draw-live-qa-20261030\\qa-only.txt");
    std::string token;std::getline(f,token);return token=="BOSS-DRAW-REALGAME-QA";
}
void tick(CubeEventArgs*){
    if(!allowed)return;auto* a=cube::mod().raw();CubePlayer p{};CubeSession s{};
    const auto now=GetTickCount64();if(now<next)return;next=now+100;
    if(!a->player.get(a,&p)||!a->session.get(a,&s)||s.gameState!=CUBE_STATE_IN_WORLD||s.networkMode!=CUBE_NET_SINGLEPLAYER||s.connected||s.playerCount!=1)return;
    a->player.setStat(a,CUBE_STAT_HEALTH,10000);a->player.setStat(a,CUBE_STAT_MANA,100);a->player.setStat(a,CUBE_STAT_STAMINA,1);
    if(!boosted){boosted=a->player.setStat(a,CUBE_STAT_BASE_DAMAGE,1000000)!=0;cube::mod().log.info("QA ONLY isolated copied character damage boost=%d; F6 prepares a loaded boss for one actual attack",boosted?1:0);}
    if(!(GetAsyncKeyState(VK_F6)&1))return;
    std::vector<CubeCreature> all(4096);const int n=a->creatures.list(a,all.data(),int(all.size()));
    unsigned chosen=0;double distance=1e20;int level=0;
    for(int i=0;i<n&&i<int(all.size());++i){const auto& c=all[i];unsigned short flags=0;
        if(c.address==p.address||c.category!=1||c.health<=0||!a->mem.read(a,c.address+0x7e,&flags,2)||!(flags&0x200)||(flags&0x800))continue;
        const double d=std::hypot(double(c.x-p.x),double(c.y-p.y));if(d<distance){chosen=c.address;distance=d;level=c.level;}}
    if(!chosen){cube::mod().log.warn("QA ONLY no living native boss in loaded entities; no death or reward simulated");return;}
    const bool moved=a->creatures.teleport(a,chosen,p.x,p.y+2,p.z)!=0;
    const bool hp=a->creatures.setStat(a,chosen,CUBE_STAT_HEALTH,1)!=0;
    cube::mod().log.info("QA ONLY prepared native boss=%08x level=%d previousDistance=%.2f moved=%d hpOne=%d; still requires real player hit",chosen,level,distance,moved?1:0,hp?1:0);
}
}
CUBE_MOD("Boss Draw isolated QA","0.0.1","Local verification"){
    mod.setId("local.cubeworld.boss-draw-isolated-qa");mod.setCapabilities(cube::Capability::Writes|cube::Capability::RawMem);
    allowed=isolated();if(!allowed){mod.log.error("QA helper refused outside the exact isolated directory");return;}
    const auto* a=mod.raw();if(!a->player.setStat||!a->creatures.list||!a->creatures.setStat||!a->creatures.teleport||!a->mem.read){allowed=false;return;}
    mod.eventListener.on(cube::Event::Frame,[]{tick(nullptr);});mod.log.info("QA ONLY exact isolated copy guard verified; real Boss Draw DLL unchanged");
}

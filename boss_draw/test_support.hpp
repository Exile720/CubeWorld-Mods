#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "cube_sdk.h"
#include "loot.hpp"
#include <map>
#include <cstdio>
#include <cstdlib>
namespace fixture {
inline unsigned assertions=0,menuCount=0,closedMenus=0,removals=0;
inline unsigned sessionReads=0;
inline unsigned imageDelta=0;
inline bool failMenu=false,failEvent=false,failHook=false,failStore=false,failScope=false;
inline std::map<unsigned,unsigned char> memory;
inline std::map<std::string,std::string> blobs;inline std::string currentScope;
inline std::map<unsigned,CubeHookFn> hooks;inline CubeEventFn readyFn=nullptr,frameFn=nullptr;
inline CubePlayer player{};inline CubeSession session{};inline CubeWorld world{};inline CubeUi ui{};
constexpr unsigned gc=0x10000000,pc=0x20000000,enemy=0x21000000,head=0x22000000,node=0x22000100,rows=0x23000000,cells=0x23001000;
inline void check(bool value,const char* text){++assertions;if(!value){std::printf("FAIL: %s\n",text);std::exit(1);}}
inline void bytes(unsigned a,const void* p,unsigned n){const auto* b=static_cast<const unsigned char*>(p);for(unsigned i=0;i<n;++i)memory[a+i]=b[i];}
template<class T>void put(unsigned a,const T& v){bytes(a,&v,sizeof(v));}
inline void zeros(unsigned a,unsigned n){for(unsigned i=0;i<n;++i)memory[a+i]=0;}
inline int32_t CUBE_CALL memRead(const CubeApi*,unsigned a,void* out,unsigned n){auto* p=static_cast<unsigned char*>(out);for(unsigned i=0;i<n;++i){const auto it=memory.find(a+i);if(it==memory.end())return 0;p[i]=it->second;}return 1;}
inline unsigned CUBE_CALL rebase(const CubeApi*,unsigned a){return a+imageDelta;}
inline void CUBE_CALL log(const CubeApi*,CubeLogLevel,const char*){}
inline int32_t CUBE_CALL getPlayer(const CubeApi*,CubePlayer* out){*out=player;return 1;}
inline int32_t CUBE_CALL getSession(const CubeApi*,CubeSession* out){++sessionReads;*out=session;return 1;}
inline int32_t CUBE_CALL getWorld(const CubeApi*,CubeWorld* out){*out=world;return 1;}
inline int32_t CUBE_CALL getUi(const CubeApi*,CubeUi* out){*out=ui;return 1;}
inline unsigned CUBE_CALL subscribe(const CubeApi*,CubeEvent e,CubeEventFn fn,void*){if(e==CUBE_EVENT_FRAME){if(failEvent)return 0;frameFn=fn;}if(e==CUBE_EVENT_READY)readyFn=fn;return e==CUBE_EVENT_FRAME?1:2;}
inline int32_t CUBE_CALL unsubscribe(const CubeApi*,unsigned){frameFn=nullptr;return 1;}
inline unsigned CUBE_CALL menu(const CubeApi*,CubeOverlayDrawFn,void*,unsigned key,int){check(key==0||key==VK_F9,"F9 menu and passthrough HUD only");if(failMenu)return 0;return ++menuCount;}
inline int32_t CUBE_CALL unregisterMenu(const CubeApi*,unsigned){++closedMenus;return 1;}
inline int32_t CUBE_CALL visibility(const CubeApi*,unsigned,int){return 1;}
inline int32_t CUBE_CALL getVisible(const CubeApi*,unsigned){return 1;}
inline int32_t CUBE_CALL key(const CubeApi*,unsigned,unsigned){return 1;}
inline int32_t CUBE_CALL onRaw(const CubeApi*,unsigned a,CubeCallConv cc,int n,CubeHookFn fn,void*){check(a==loot::deathFunction+imageDelta&&cc==CUBE_CC_THISCALL&&n==2,"death observer uses audited rebased ABI");if(failHook)return 0;hooks[a]=fn;return 1;}
inline int32_t CUBE_CALL removeRaw(const CubeApi*,unsigned a){removals+=unsigned(hooks.erase(a));return 1;}
inline int32_t CUBE_CALL setScope(const CubeApi*,const char* scopeName){if(failScope)return 0;currentScope=scopeName;return 1;}
inline std::string storageKey(const char* key){return currentScope+"/"+key;}
inline int32_t CUBE_CALL has(const CubeApi*,const char* key){return blobs.count(storageKey(key))?1:0;}
inline int32_t CUBE_CALL store(const CubeApi*,const char* key,const void* data,int32_t n){if(failStore)return 0;blobs[storageKey(key)]=std::string(static_cast<const char*>(data),std::size_t(n));return 1;}
inline int32_t CUBE_CALL fetch(const CubeApi*,const char* key,void* data,int32_t n){const auto it=blobs.find(storageKey(key));if(it==blobs.end())return 0;if(!data)return int(it->second.size());const auto amount=(std::min)(std::size_t(n),it->second.size());std::memcpy(data,it->second.data(),amount);return int(amount);}
inline CubeApi api(){CubeApi a{};a.abiVersion=CUBE_ABI_VERSION;a.structSize=sizeof(a);a.environment=CUBE_ENV_CLIENT;
    a.mem.read=&memRead;a.mem.rebase=&rebase;a.log.write=&log;a.player.get=&getPlayer;a.world.get=&getWorld;a.session.get=&getSession;a.ui.get=&getUi;
    a.events.subscribe=&subscribe;a.events.unsubscribe=&unsubscribe;a.overlay.registerMenu=&menu;a.overlay.unregisterMenu=&unregisterMenu;
    a.overlay.setVisible=&visibility;a.overlay.isVisible=&getVisible;a.overlay.setToggleKey=&key;a.overlay.setPassthrough=&visibility;
    a.hooks.onRaw=&onRaw;a.hooks.removeRaw=&removeRaw;a.storage.setScope=&setScope;a.storage.has=&has;a.storage.put=&store;a.storage.get=&fetch;return a;}
inline void image(bool unsupported=false){const unsigned base=reinterpret_cast<unsigned>(GetModuleHandleW(nullptr));IMAGE_DOS_HEADER dos{};dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=0x80;put(base,dos);
    IMAGE_NT_HEADERS32 nt{};nt.Signature=IMAGE_NT_SIGNATURE;nt.FileHeader.Machine=IMAGE_FILE_MACHINE_I386;nt.FileHeader.TimeDateStamp=unsupported?0:0x51ea955e;nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC;nt.OptionalHeader.SizeOfImage=0x3bc000;nt.OptionalHeader.AddressOfEntryPoint=0x28e1e0;put(base+0x80,nt);
    auto entry=std::array<unsigned char,10>{0x55,0x8b,0xec,0x6a,0xff,0x68,0,0,0,0};const unsigned handler=0x6f207b+imageDelta;std::memcpy(entry.data()+6,&handler,4);
    put(loot::deathFunction+imageDelta,entry);put(loot::addFunction+imageDelta,std::array<unsigned char,9>{0x55,0x8b,0xec,0x81,0xec,0x54,2,0,0});put(0x59622e+imageDelta,std::array<unsigned char,5>{0xe8,0x4d,0x4f,0,0});}
inline void context(){player={};player.address=pc;player.alive=1;player.health=100;player.hasName=1;std::memcpy(player.name,"Tester",7);player.classForm=3;player.hasState=1;player.onGround=1;
    session={};session.address=gc;session.gameState=CUBE_STATE_IN_WORLD;session.hasNetwork=1;session.networkMode=CUBE_NET_SINGLEPLAYER;session.playerCount=1;
    world={};world.address=gc;world.hasSeed=1;world.seed=42;ui={};ui.valid=1;
    put(gc+0x8006d0,pc);put(gc+0x800a0c,0);zeros(gc+0x378,24);const char name[]="Test";bytes(gc+0x378,name,sizeof(name));put(gc+0x388,4u);put(gc+0x38c,15u);
    zeros(pc,0x2000);put(pc+8,std::uint64_t(0));put(pc+0x11dc,loot::Vector{});
    zeros(enemy,0x2000);put(enemy,0x6fd8ccu);put(enemy+8,std::uint64_t(500));put(enemy+0x60,static_cast<unsigned char>(1));put(enemy+0x7e,static_cast<unsigned short>(0x200));put(enemy+0x190,40);put(enemy+0x1a8,static_cast<unsigned char>(2));
    put(enemy+0x13a4,head);zeros(head,32);put(head+4,node);zeros(node,32);put(node,head);put(node+8,head);put(node+0x10,std::uint64_t(0));put(node+0x18,12.f);
    loot::Rng r{123};put(enemy+0xaa8,loot::make(1,40,3,r));
}
}

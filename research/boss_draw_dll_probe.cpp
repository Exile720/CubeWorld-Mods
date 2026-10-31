// Offline binary integration probe. Never opens a game process or executes game code.
// The production DLL runs against a private SDK API fixture, real RawHook/MinHook,
// an isolated on-disk ledger, a shared ImGui context and private native ABI targets.
#include "../boss_draw/test_support.hpp"
#include "boss_draw_probe_focus.hpp"
#include "game/gamehooks/rawpool.h"
#include "hooks/detour.h"
#include "core/log.h"
#include "core/faultguard.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace fs=std::filesystem;
namespace fx=fixture;
namespace probe {
struct Menu {CubeOverlayDrawFn draw=nullptr;void* user=nullptr;unsigned key=0;bool visible=false;};
std::map<unsigned,Menu> menus;
std::ofstream evidence;
fs::path output;
CubeHookFn callback=nullptr;
unsigned hookAddress=0,rawCalls=0,nativeDeaths=0,nativeAdds=0,checks=0,menuSerial=0;
unsigned wantedSelf=0,wantedArg0=0,wantedArg1=0;
bool inDeath=false,failStore=false,failAdd=false;
std::vector<loot::Item> inventory;
ImGuiContext* gui=nullptr;
CubeApi api{};
HMODULE module=nullptr;
CubeModShutdownFn shutdownFn=nullptr;
std::string version;

void say(const std::string& message){std::cout<<message<<std::endl;if(evidence)evidence<<message<<std::endl;}
void check(bool good,const char* message){++checks;if(!good){say(std::string("FAIL: ")+message);throw std::runtime_error(message);}say(std::string("PASS: ")+message);}
std::string fileText(const fs::path& p){std::ifstream f(p,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});}
fs::path storagePath(const char* key){
    if(!key||(std::strcmp(key,"ledger")&&std::strcmp(key,"ledger_backup")))throw std::runtime_error("unexpected storage key");
    if(fx::currentScope.empty()||fx::currentScope.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)throw std::runtime_error("unsafe storage scope");
    return output/fx::currentScope/(std::string(key)+".bin");
}
int32_t CUBE_CALL stored(const CubeApi*,const char* key){return fs::is_regular_file(storagePath(key))?1:0;}
int32_t CUBE_CALL fetch(const CubeApi*,const char* key,void* data,int32_t n){
    const auto path=storagePath(key);if(!fs::is_regular_file(path))return 0;const auto text=fileText(path);
    if(!data)return int32_t(text.size());if(n<0)return 0;const auto count=(std::min)(std::size_t(n),text.size());std::memcpy(data,text.data(),count);return int32_t(count);
}
int32_t CUBE_CALL store(const CubeApi*,const char* key,const void* data,int32_t n){
    if(inDeath)throw std::runtime_error("death callback attempted storage");
    if(failStore||n<0||n>int32_t(loot::maxBytes))return 0;const auto path=storagePath(key);fs::create_directories(path.parent_path());
    std::ofstream f(path,std::ios::binary|std::ios::trunc);f.write(static_cast<const char*>(data),n);f.flush();return f.good()?1:0;
}
loot::State disk(){loot::State state;const auto path=storagePath("ledger");if(!fs::is_regular_file(path))return state;check(loot::decode(fileText(path),state),"persisted ledger checksum and schema");return state;}
std::size_t pending(){return disk().tickets.size();}

void publishInventory(){
    fx::put(fx::pc+0x11dc,loot::Vector{fx::rows,fx::rows+12,fx::rows+12});
    const unsigned end=fx::cells+unsigned(inventory.size())*0x11c;fx::put(fx::rows,loot::Vector{fx::cells,end,end});
    for(unsigned i=0;i<inventory.size();++i){fx::put(fx::cells+i*0x11c,1);fx::put(fx::cells+i*0x11c+4,inventory[i]);}
}
__declspec(noinline) int __fastcall privateDeath(void* self,void*,unsigned entity,unsigned source){
    ++nativeDeaths;
    check(reinterpret_cast<unsigned>(self)==wantedSelf&&entity==wantedArg0&&source==wantedArg1,"original private death target receives unchanged ECX and two stack arguments");
    return 0x314159;
}
__declspec(noinline) void __fastcall privateAdd(void* self,void*,const loot::Item* item,int target){
    ++nativeAdds;check(reinterpret_cast<unsigned>(self)==fx::pc+0x11dc&&target==-1&&item&&loot::valid(*item),"DLL nativeAdd invokes private receiver with real thiscall ABI and a valid item");
    const auto state=disk();check(!state.tickets.empty()&&state.tickets.front().stage==loot::Stage::Claiming,"claim intent is durable before private native add");
    if(!failAdd){inventory.push_back(*item);publishInventory();}
}
unsigned CUBE_CALL rebase(const CubeApi*,unsigned a){if(a==loot::deathFunction)return reinterpret_cast<unsigned>(&privateDeath);if(a==loot::addFunction)return reinterpret_cast<unsigned>(&privateAdd);return a;}
int32_t CUBE_CALL onRaw(const CubeApi*,unsigned address,CubeCallConv cc,int32_t count,CubeHookFn fn,void*){
    check(address==reinterpret_cast<unsigned>(&privateDeath)&&cc==CUBE_CC_THISCALL&&count==2,"installed DLL registers its audited Thiscall/2 ABI");
    callback=fn;hookAddress=address;return game::gamehooks::rawpool::install(address,cc,count)?1:0;
}
int32_t CUBE_CALL removeRaw(const CubeApi*,unsigned address){const bool removed=game::gamehooks::rawpool::remove(address);callback=nullptr;hookAddress=0;return removed?1:0;}
void CUBE_CALL log(const CubeApi*,CubeLogLevel level,const char* text){say(std::string("DLL[")+std::to_string(int(level))+"]: "+(text?text:""));}
unsigned CUBE_CALL registerMenu(const CubeApi*,CubeOverlayDrawFn fn,void* user,unsigned key,int open){const auto id=++menuSerial;menus[id]={fn,user,key,open!=0};return id;}
int32_t CUBE_CALL unregisterMenu(const CubeApi*,unsigned id){return int32_t(menus.erase(id));}
int32_t CUBE_CALL visible(const CubeApi*,unsigned id,int value){if(!menus.count(id))return 0;menus[id].visible=value!=0;return 1;}
int32_t CUBE_CALL isVisible(const CubeApi*,unsigned id){return menus.count(id)&&menus[id].visible?1:0;}
int32_t CUBE_CALL key(const CubeApi*,unsigned id,unsigned value){if(!menus.count(id))return 0;menus[id].key=value;return 1;}
void* CUBE_CALL context(const CubeApi*){return gui;}
void CUBE_CALL allocFuncs(const CubeApi*,void** alloc,void** release,void** user){ImGuiMemAllocFunc a=nullptr;ImGuiMemFreeFunc f=nullptr;ImGui::GetAllocatorFunctions(&a,&f,user);*alloc=reinterpret_cast<void*>(a);*release=reinterpret_cast<void*>(f);}
float CUBE_CALL scale(const CubeApi*){return 1.f;}
void tick(){if(!fx::frameFn)throw std::runtime_error("FRAME not registered");CubeEventArgs e{};e.structSize=sizeof(e);e.event=CUBE_EVENT_FRAME;fx::frameFn(&e);}
void render(float delta=.02f){
    ImGui::SetCurrentContext(gui);auto& io=ImGui::GetIO();io.DeltaTime=delta;ImGui::NewFrame();
    for(auto& entry:menus)if(entry.second.visible)entry.second.draw(entry.second.user);
    ImGui::Render();check(ImGui::GetDrawData()->TotalVtxCount>0,"production DLL panel emits render geometry through shared SDK context");
}
void openMenu(){for(auto& entry:menus)if(entry.second.key==VK_F9)entry.second.visible=true;render();}
void clickPrimary(){
    openMenu();auto* window=ImGui::FindWindowByName("首领抽奖###boss_draw");check(window!=nullptr,"production draw window is accessible in the shared ImGui context");
    // Production footer: 16 px inset, main action before the close button.
    const float buttonHeight=(std::max)(44.f,window->FontRefSize+18.f);
    const auto p=ImVec2(window->Pos.x+window->Size.x*.3f,window->Pos.y+window->Size.y-16.f-buttonHeight*.5f);
    auto& io=ImGui::GetIO();io.AddMousePosEvent(p.x,p.y);render();io.AddMouseButtonEvent(0,true);render();io.AddMouseButtonEvent(0,false);render();tick();
}
void death(unsigned receiver=fx::gc+0x2e4,bool worker=false){
    auto invoke=[&]{wantedSelf=receiver;wantedArg0=fx::enemy;wantedArg1=0x76543210;const auto before=nativeDeaths;const auto rawBefore=rawCalls;
        inDeath=true;using Fn=int(__thiscall*)(void*,unsigned,unsigned);const auto address=hookAddress;
        const int result=reinterpret_cast<Fn>(address)(reinterpret_cast<void*>(receiver),wantedArg0,wantedArg1);inDeath=false;
        check(result==0x314159&&nativeDeaths==before+1&&rawCalls==rawBefore+1,"actual MinHook/RawHook dispatch observes one call and preserves original return");};
    if(worker){std::thread thread(invoke);thread.join();}else invoke();
}
void image(){
    fx::image();fx::context();publishInventory();
    // supported() reads audited game bytes through its API, while hook/add rebasing
    // points to unrelated private targets. No original executable is loaded.
    fx::put(reinterpret_cast<unsigned>(&privateDeath),std::array<unsigned char,10>{0x55,0x8b,0xec,0x6a,0xff,0x68,0x7b,0x20,0x6f,0});
    fx::put(reinterpret_cast<unsigned>(&privateAdd),std::array<unsigned char,9>{0x55,0x8b,0xec,0x81,0xec,0x54,2,0,0});
}
void load(const fs::path& path){
    menus.clear();fx::frameFn=fx::readyFn=nullptr;fx::currentScope.clear();
    module=LoadLibraryW(path.c_str());check(module!=nullptr,"LoadLibrary of the supplied actual DLL");
    probe_focus::install(module);
    auto init=reinterpret_cast<CubeModInitFn>(GetProcAddress(module,"CubeMod_Init"));shutdownFn=reinterpret_cast<CubeModShutdownFn>(GetProcAddress(module,"CubeMod_Shutdown"));check(init&&shutdownFn,"actual DLL exported lifecycle");
    auto* info=init(&api);check(info&&info->id&&std::strcmp(info->id,"local.cubeworld.boss-draw")==0,"actual DLL manifest identity");version=info->version?info->version:"unknown";say("DLL_VERSION="+version);
    check(fx::readyFn&&fx::frameFn,"actual DLL registers READY and FRAME");CubeEventArgs ready{};ready.structSize=sizeof(ready);ready.event=CUBE_EVENT_READY;fx::readyFn(&ready);
    check(callback&&hookAddress&&menus.size()==2,"actual DLL enabled independent RawHook/menu/HUD");tick();check(!fx::currentScope.empty(),"valid single-player context chooses an isolated role/world scope");
}
void unload(){
    if(!module)return;shutdownFn();check(!callback&&!hookAddress&&menus.empty(),"DLL shutdown releases owned raw hook and menus");
    probe_focus::restore();FreeLibrary(module);module=nullptr;shutdownFn=nullptr;
}
void setupApi(){api=fx::api();api.mem.rebase=&rebase;api.log.write=&log;api.hooks.onRaw=&onRaw;api.hooks.removeRaw=&removeRaw;
    api.storage.has=&stored;api.storage.get=&fetch;api.storage.put=&store;api.overlay.registerMenu=&registerMenu;api.overlay.unregisterMenu=&unregisterMenu;
    api.overlay.setVisible=&visible;api.overlay.isVisible=&isVisible;api.overlay.setToggleKey=&key;api.overlay.context=&context;api.overlay.allocFuncs=&allocFuncs;api.overlay.uiScale=api.overlay.dpiScale=&scale;
}
void run(const fs::path& dll){
    image();setupApi();gui=ImGui::CreateContext();auto& io=ImGui::GetIO();io.DisplaySize={1000,800};io.DeltaTime=.02f;io.IniFilename=io.LogFilename=nullptr;
    unsigned char* pixels=nullptr;int w=0,h=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    load(dll);check(pending()==0,"isolated test begins with no pending card");
    fx::put(fx::enemy+0x7e,static_cast<unsigned short>(0));death();tick();check(pending()==0,"ordinary enemy cannot earn an extra draw");fx::put(fx::enemy+0x7e,static_cast<unsigned short>(0x200));
    death(fx::gc);tick();check(pending()==0,"controller receiver rejected instead of embedded World");
    fx::session.networkMode=CUBE_NET_MULTIPLAYER;death();tick();check(pending()==0,"multiplayer context is rejected");fx::session.networkMode=CUBE_NET_SINGLEPLAYER;tick();
    const auto before=fs::exists(storagePath("ledger"))?fileText(storagePath("ledger")):std::string();death(fx::gc+0x2e4,true);
    check((fs::exists(storagePath("ledger"))?fileText(storagePath("ledger")):std::string())==before&&nativeAdds==0,"worker death only queues; no ledger write or native add before FRAME");
    tick();const auto sealed=disk();check(sealed.tickets.size()==1&&sealed.tickets.front().stage==loot::Stage::Sealed&&nativeAdds==0,"embedded World death becomes exactly one durable sealed card");
    check(sealed.tickets.front().bossId==500&&sealed.tickets.front().pool.size()==1&&loot::equal(sealed.tickets.front().pool.front(),fx::memory.empty()?loot::Item{}:[] {loot::Item i;fx::memRead(nullptr,fx::enemy+0xaa8,&i,sizeof(i));return i;}()),"durable card retains observed boss identity and exact worn item");
    death();tick();check(pending()==1,"duplicate native death cannot create a second card");
    unload();load(dll);check(pending()==1&&disk().tickets.front().stage==loot::Stage::Sealed,"actual DLL unload/reload preserves the sealed card on disk");
    clickPrimary();const auto revealed=disk();check(revealed.tickets.size()==1&&revealed.tickets.front().stage==loot::Stage::Revealed&&nativeAdds==0,"real production button input reveals and persists without granting");
    const auto exact=fileText(storagePath("ledger"));const auto reward=revealed.tickets.front().reward;
    unload();load(dll);openMenu();render(1.2f);clickPrimary();
    check(nativeAdds==1&&inventory.size()==1&&loot::equal(inventory.front(),reward)&&pending()==0,"reloaded revealed card grants its exact stored item once through private native ABI");
    check(exact!=fileText(storagePath("ledger")),"successful claim durably consumes the revealed card");clickPrimary();check(nativeAdds==1&&pending()==0,"repeated production UI input cannot grant twice");
    unload();load(dll);check(pending()==0&&nativeAdds==1,"second actual DLL reopen retains the consumed state without regrant");unload();
    ImGui::DestroyContext(gui);gui=nullptr;game::gamehooks::rawpool::shutdown();hooks::detour::shutdown();
    say("PASS_ALL checks="+std::to_string(checks)+" raw_calls="+std::to_string(rawCalls)+" private_deaths="+std::to_string(nativeDeaths)+" private_adds="+std::to_string(nativeAdds));
}
}

// Boundary adapters only. The real rawpool, detour and MinHook sources below these
// adapters perform the native ECX/stack capture, trampoline and target restoration.
namespace logger {void write(Level,const char*,const char*,...) {}}
namespace faultguard {bool enabled(){return false;}bool runGuarded(const char*,const CubeApi*,GuardThunk fn,void* ctx){fn(ctx);return true;}}
namespace game::gamehooks {
int32_t dispatchRaw(uint32_t address,CubeHookCall& call){
    ++probe::rawCalls;probe::check(address==probe::hookAddress&&call.self==probe::wantedSelf&&unsigned(call.argi[0])==probe::wantedArg0&&unsigned(call.argi[1])==probe::wantedArg1&&call.argCount==2,"real rawpool capture exposes unchanged ECX and both stack arguments to SDK callback");
    if(probe::callback)probe::callback(&call);probe::check(!call.cancel&&!call.overrideReturn,"production observer does not cancel or override the private original call");return call.cancel;
}
}
int wmain(int argc,wchar_t** argv){
    try {
        if(argc!=3)throw std::runtime_error("usage: boss_draw_dll_probe.exe <absolute DLL path> <new isolated output directory>");
        const fs::path dll=fs::weakly_canonical(argv[1]);probe::output=fs::absolute(argv[2]).lexically_normal();
        if(!fs::path(argv[1]).is_absolute()||!fs::is_regular_file(dll))throw std::runtime_error("DLL must be an existing absolute path");
        // A fresh child of the workspace build directory is the only writable storage.
        const auto build=fs::weakly_canonical(fs::current_path()/"build");const auto relative=probe::output.lexically_relative(build);
        if(relative.empty()||relative=="."||*relative.begin()==".."||fs::exists(probe::output))throw std::runtime_error("output must be a NEW directory below this workspace build/");
        fs::create_directories(probe::output);probe::evidence.open(probe::output/"stages.txt",std::ios::binary);
        probe::say("Offline actual-DLL integration: "+dll.u8string());probe::say("Fixtures: context/memory; real RawHook/MinHook; file storage; shared ImGui; IAT focus substitutes in private DLL mapping only.");probe::say("No game process, game death function, original native add, real ledger or saved game access.");
        probe::run(dll);return 0;
    }catch(const std::exception& e){probe::say(std::string("PROBE_FAILED: ")+e.what());return 1;}
}

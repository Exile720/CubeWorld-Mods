#include "test_support.hpp"
int main(int argc,char** argv){
    using namespace fixture;check(argc>=2,"DLL supplied");const std::string mode=argc>2?argv[2]:"normal";
    if(mode=="relocated_low"||mode=="bad_handler")imageDelta=0xffec0000u;
    if(mode=="relocated_high")imageDelta=0x240000u;
    failMenu=mode=="menu";failEvent=mode=="event";failHook=mode=="hook";image(mode=="unsupported");
    if(mode=="bad_handler")put(loot::deathFunction+imageDelta+6,0x6f207cu+imageDelta);
    auto a=api();if(mode=="storage")a.storage.put=nullptr;
    const auto dll=LoadLibraryA(argv[1]);check(dll!=nullptr,"load actual built DLL");
    auto init=reinterpret_cast<CubeModInitFn>(GetProcAddress(dll,"CubeMod_Init"));auto stop=reinterpret_cast<CubeModShutdownFn>(GetProcAddress(dll,"CubeMod_Shutdown"));check(init&&stop,"exports");
    auto info=init(&a);check(info&&std::strcmp(info->id,"local.cubeworld.boss-draw")==0&&!info->deps,"unique ID with no other Mod dependencies");
    check(hooks.empty(),"no raw hook before all mods initialized");
    if(mode=="conflict")put(loot::deathFunction,std::array<unsigned char,10>{0xe9,0,0,0,0,0,0,0,0,0});
    if(readyFn){CubeEventArgs e{};e.structSize=sizeof(e);e.event=CUBE_EVENT_READY;readyFn(&e);}
    if(mode=="normal"||mode=="relocated_low"||mode=="relocated_high"){check(hooks.size()==1&&frameFn&&menuCount==2,"observer plus independent menu/HUD");CubeHookCall c{};c.argCount=2;c.argi[0]=123;hooks.begin()->second(&c);check(!c.cancel&&!c.overrideReturn&&c.argi[0]==123,"observer never cancels or mutates original loot call");}
    else check(hooks.empty(),"failed or conflicting initialization never enables death observer");
    stop();check(hooks.empty()&&menuCount==closedMenus,"shutdown releases only owned hooks and menus");FreeLibrary(dll);
    std::printf("PASS %u actual DLL checks (%s); no native game call.\n",assertions,mode.c_str());
}

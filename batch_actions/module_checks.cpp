#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "cube_sdk.h"
#include "batch.hpp"
#include <map>
#include <vector>
#include <cstdio>
#include <cstdlib>

namespace {
std::map<unsigned,std::vector<unsigned char>> memory;
std::map<unsigned,CubeHookFn> handlers;
CubeEventFn readyFn=nullptr;unsigned removed=0,subscriptions=0;bool failHook=false;
bool overlayFailure=false,eventFailure=false;unsigned menus=0,closedMenus=0;
void check(bool v,const char* text){if(!v){std::printf("FAIL %s\n",text);std::exit(1);}}
template<class T>void put(unsigned a,const T& v){auto& b=memory[a];b.resize(sizeof(v));std::memcpy(b.data(),&v,sizeof(v));}
int32_t CUBE_CALL read(const CubeApi*,uint32_t a,void* out,uint32_t n){for(const auto& s:memory)if(a>=s.first&&std::uint64_t(a-s.first)+n<=s.second.size()){std::memcpy(out,s.second.data()+a-s.first,n);return 1;}return 0;}
unsigned CUBE_CALL rebase(const CubeApi*,unsigned a){return a;}
void CUBE_CALL log(const CubeApi*,CubeLogLevel,const char* v){std::printf("DLL: %s\n",v);}
unsigned CUBE_CALL subscribe(const CubeApi*,CubeEvent e,CubeEventFn fn,void*){if(e==CUBE_EVENT_FRAME&&eventFailure)return 0;if(e==CUBE_EVENT_READY)readyFn=fn;return ++subscriptions;}
int32_t CUBE_CALL unsubscribe(const CubeApi*,unsigned){return 1;}
int32_t CUBE_CALL write(const CubeApi*,unsigned,const void*,unsigned){return 0;}
int32_t CUBE_CALL value(const CubeApi*,unsigned){return 2;}
unsigned CUBE_CALL menu(const CubeApi*,CubeOverlayDrawFn,void*,unsigned key,int open){check(key==0&&!open,"no hidden keyboard shortcut needed");if(overlayFailure)return 0;++menus;return 1;}
int32_t CUBE_CALL closeMenu(const CubeApi*,unsigned){++closedMenus;return 1;}
int32_t CUBE_CALL onRaw(const CubeApi*,unsigned a,CubeCallConv cc,int n,CubeHookFn fn,void*) {
    check(cc==CUBE_CC_THISCALL,"native thiscall ABI");check(n==(a==batch::buyFunction?1:0),"native arity");
    if(failHook&&a==batch::craftFunction)return 0;handlers[a]=fn;return 1;
}
int32_t CUBE_CALL remove(const CubeApi*,unsigned a){removed+=static_cast<unsigned>(handlers.erase(a));return 1;}
}
int main(int argc,char** argv){
    check(argc>=2,"DLL argument");const bool unsupported=argc>2&&std::strcmp(argv[2],"unsupported")==0;
    failHook=argc>2&&std::strcmp(argv[2],"partial")==0;
    const bool conflict=argc>2&&std::strcmp(argv[2],"conflict")==0;
    overlayFailure=argc>2&&std::strcmp(argv[2],"overlay")==0;
    eventFailure=argc>2&&std::strcmp(argv[2],"events")==0;
    CubeApi api{};api.abiVersion=CUBE_ABI_VERSION;api.structSize=sizeof(api);api.environment=CUBE_ENV_CLIENT;
    api.mem.read=&read;api.mem.rebase=&rebase;api.log.write=&log;api.events.subscribe=&subscribe;
    api.mem.write=&write;api.items.value=&value;api.events.unsubscribe=&unsubscribe;
    api.overlay.registerMenu=&menu;api.overlay.unregisterMenu=&closeMenu;
    api.hooks.onRaw=&onRaw;api.hooks.removeRaw=&remove;
    const unsigned base=reinterpret_cast<unsigned>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=0x80;put(base,dos);
    IMAGE_NT_HEADERS32 nt{};nt.Signature=IMAGE_NT_SIGNATURE;nt.FileHeader.Machine=IMAGE_FILE_MACHINE_I386;
    nt.FileHeader.TimeDateStamp=unsupported?0:0x51ea955e;nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt.OptionalHeader.SizeOfImage=0x3bc000;nt.OptionalHeader.AddressOfEntryPoint=0x28e1e0;put(base+0x80,nt);
    put(0x47b611,std::array<unsigned char,6>{0x81,0xec,0xd0,3,0,0});
    put(0x4709d1,std::array<unsigned char,6>{0x81,0xec,0x6c,2,0,0});
    put(0x47b1b0,std::array<unsigned char,6>{0x56,0x8b,0xf1,0x80,0xbe,0xf0});
    put(batch::buyFunction,std::array<unsigned char,6>{0x55,0x8b,0xec,0x6a,0xff,0x68});
    put(batch::craftFunction,std::array<unsigned char,6>{0x55,0x8b,0xec,0x6a,0xff,0x68});
    HMODULE dll=LoadLibraryA(argv[1]);check(dll!=nullptr,"load actual built DLL");
    auto init=reinterpret_cast<CubeModInitFn>(GetProcAddress(dll,"CubeMod_Init"));
    auto stop=reinterpret_cast<CubeModShutdownFn>(GetProcAddress(dll,"CubeMod_Shutdown"));check(init&&stop,"exports");
    CubeModInfo* info=init(&api);check(info&&std::strcmp(info->id,"local.cubeworld.batch-actions")==0,"unique independent ID");
    check(!info->deps,"no other Mod dependency");check((info->capabilities&CUBE_CAP_WRITES)!=0,"intercept cancellation permission");
    check(handlers.empty(),"does not hook before all Mod init checks finish");
    if(unsupported||overlayFailure||eventFailure)check(!subscriptions&&handlers.empty(),"initialization failure never installs hooks");
    else{
        check(readyFn!=nullptr,"registered READY callback");
        if(conflict)put(batch::craftFunction,std::array<unsigned char,6>{0xe9,0,0,0,0,0x90});
        CubeEventArgs e{};e.structSize=sizeof(e);e.event=CUBE_EVENT_READY;readyFn(&e);
        if(conflict)check(handlers.empty(),"occupied craft entry rejected before either hook");
        else if(failHook)check(handlers.empty()&&removed==1,"partial hook failure removes only its buy handler");
        else{
            check(handlers.size()==2,"purchase and craft handlers installed after READY");
            CubeHookCall c{};c.structSize=sizeof(c);c.hook=CUBE_HOOK_RAW;c.self=0;c.argCount=1;c.argi[0]=0;
            handlers.at(batch::buyFunction)(&c);check(!c.cancel,"unrelated click without valid shop unchanged");
        }
    }
    stop();check(handlers.empty(),"shutdown releases own handlers");check(menus==closedMenus,"own menu released exactly once");FreeLibrary(dll);
    std::printf("Batch DLL initialization checks passed. Native detours/gameplay not executed.\n");
}

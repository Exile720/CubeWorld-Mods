#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "cube_sdk.h"
#include "loader/core/owner_name.h"
#include "light.hpp"
#include <map>
#include <vector>
#include <cstring>
#include <cstdio>
#include <limits>

// Actual DLL with synthetic SDK memory. Native Direct3D/game functions are never run.
namespace {
unsigned checks=0,writes=0,removed=0,sessionReads=0,memoryReads=0;bool world=true;
constexpr unsigned gc=0x01000000,shader=0x04000000,args=0x05000000;
std::map<unsigned,std::vector<unsigned char>> memory;
modloader::ModContext context{};CubeHookFn handler=nullptr;CubeCallConv convention{};int count=0;
void check(bool ok,const char* text){++checks;if(!ok){std::printf("FAIL %s\n",text);std::exit(1);}}
template<class T>void put(unsigned a,const T& v){auto& b=memory[a];b.resize(sizeof(v));std::memcpy(b.data(),&v,sizeof(v));}
int32_t CUBE_CALL read(const CubeApi*,uint32_t a,void* out,uint32_t n){++memoryReads;for(const auto& e:memory)if(a>=e.first&&std::uint64_t(a)-e.first+n<=e.second.size()){std::memcpy(out,e.second.data()+(a-e.first),n);return 1;}return 0;}
int32_t CUBE_CALL readable(const CubeApi* api,uint32_t a,uint32_t n){std::vector<unsigned char>b(n);return read(api,a,b.data(),n);}
uint32_t CUBE_CALL rebase(const CubeApi*,uint32_t a){return a;}
int32_t CUBE_CALL write(const CubeApi*,uint32_t,const void*,uint32_t){++writes;return 0;}
int32_t CUBE_CALL session(const CubeApi*,CubeSession* out){++sessionReads;*out={};out->gameState=world?CUBE_STATE_IN_WORLD:CUBE_STATE_TITLE;return 1;}
void CUBE_CALL log(const CubeApi*,CubeLogLevel,const char* text){std::printf("DLL: %s\n",text);}
int32_t CUBE_CALL onRaw(const CubeApi*,uint32_t a,CubeCallConv cc,int32_t n,CubeHookFn fn,void*){check(a==maplight::lightFunction,"only the verified lighting function is hooked");handler=fn;convention=cc;count=n;return 1;}
int32_t CUBE_CALL removeRaw(const CubeApi*,uint32_t a){check(a==maplight::lightFunction,"shutdown removes only its own lighting hook");++removed;return 1;}
CubeHookCall call(){CubeHookCall c{};c.structSize=sizeof(c);c.hook=CUBE_HOOK_RAW;c.address=maplight::lightFunction;c.self=shader;c.argCount=4;for(int i=0;i<4;++i){c.argi[i]=args+i*0x100;std::memcpy(c.argf+i,c.argi+i,4);}return c;}
void unchanged(CubeHookCall c,const char* text){const auto previous=c;handler(&c);check(!std::memcmp(&c,&previous,sizeof(c)),text);}
template<class T>bool ownValue(unsigned a,T& out){MEMORY_BASIC_INFORMATION info{};if(!VirtualQuery(reinterpret_cast<void*>(a),&info,sizeof(info))||info.State!=MEM_COMMIT||(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;if(std::uint64_t(a)+sizeof(out)>reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize)return false;std::memcpy(&out,reinterpret_cast<void*>(a),sizeof(out));return true;}
unsigned marshalled(const CubeHookCall& c,int i){unsigned f=0;std::memcpy(&f,c.argf+i,4);const unsigned original=args+i*0x100;return f!=original?f:static_cast<unsigned>(c.argi[i]);}
}
int wmain(int argc,wchar_t** argv){
    if(argc<2||argc>3)return 2;
    context.api.abiVersion=CUBE_ABI_VERSION;context.api.structSize=sizeof(CubeApi);context.api.environment=CUBE_ENV_CLIENT;
    context.api.mem={&read,&readable,&rebase,&write};context.api.session.get=&session;context.api.log.write=&log;context.api.hooks.onRaw=&onRaw;context.api.hooks.removeRaw=&removeRaw;
    const unsigned base=reinterpret_cast<unsigned>(GetModuleHandleW(nullptr));IMAGE_DOS_HEADER dos{};dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=0x80;put(base,dos);
    IMAGE_NT_HEADERS32 nt{};nt.Signature=IMAGE_NT_SIGNATURE;nt.FileHeader.Machine=IMAGE_FILE_MACHINE_I386;nt.FileHeader.TimeDateStamp=0x51ea955e;nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC;nt.OptionalHeader.SizeOfImage=0x3bc000;nt.OptionalHeader.AddressOfEntryPoint=0x28e1e0;put(base+0x80,nt);
    put(maplight::lightFunction,std::array<unsigned char,9>{0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x1c});put(0x448274,std::array<unsigned char,3>{0xc2,0x10,0});put(0x4ad931,std::array<unsigned char,6>{0x8b,0x8f,0x80,0x05,0x80,0});put(0x4ad957,std::array<unsigned char,5>{0xe8,0x14,0xa8,0xf9,0xff});
    put(maplight::controllerGlobal,gc);put(gc+maplight::shaderOffset,shader);put(gc+maplight::mapOpenOffset,std::uint8_t{0});put(gc+0x800440,82800000);
    const std::array<float,4> dark{.01f,.02f,.03f,1};put(args,dark);put(args+0x100,dark);put(args+0x200,std::array<float,3>{.5f,0,-1});put(args+0x300,dark);
    if(argc==3)put(maplight::lightFunction,std::array<unsigned char,9>{0xe9,0,0,0,0,0,0,0,0});
    const auto before=memory;
    HMODULE module=LoadLibraryW(argv[1]);check(module!=nullptr,"actual DLL loads only into the fixture process");
    auto init=reinterpret_cast<CubeModInitFn>(GetProcAddress(module,"CubeMod_Init"));auto shutdown=reinterpret_cast<CubeModShutdownFn>(GetProcAddress(module,"CubeMod_Shutdown"));check(init&&shutdown,"actual DLL exports init and shutdown");
    const auto* info=init(&context.api);check(info&&std::strcmp(info->id,"local.cubeworld.map_light")==0,"actual manifest has its independent registered ID");context.capabilities=info->capabilities;
    check(!info->deps,"lighting has no hard dependency on map, Explorer or potion Mods");
    check(info->capabilities==(CUBE_CAP_RAW_MEM|CUBE_CAP_RAW_HOOKS|CUBE_CAP_WRITES)&&modloader::ownerMayWrite(&context.api),"actual capability gate permits transient argument changes");
    if(argc==3){check(!handler,"an existing detour or unknown signature refuses initialization");shutdown();check(writes==0&&memory==before,"refused build makes no memory writes");std::printf("PASS %u unsupported-build checks\n",checks);return 0;}
    check(handler&&convention==CUBE_CC_THISCALL&&count==4,"native hook has verified thiscall/four-pointer ABI");
    const auto beforeClosedSessions=sessionReads,beforeClosedReads=memoryReads;
    unchanged(call(),"closed M map preserves night-world rendering arguments");
    check(sessionReads==beforeClosedSessions&&memoryReads-beforeClosedReads==2,"closed map uses two bounded reads and never queries full session/entity counts");
    const auto stressBefore=memoryReads;auto stress=call();const auto stressOriginal=stress;
    for(int i=0;i<1000;++i)handler(&stress);
    check(sessionReads==beforeClosedSessions&&memoryReads-stressBefore==2000&&!std::memcmp(&stress,&stressOriginal,sizeof(stress)),"1000 ordinary-render callbacks perform no full session query or argument changes");
    put(gc+maplight::mapOpenOffset,std::uint8_t{1});const auto activeBefore=memory;auto c=call();handler(&c);
    maplight::Palette p{};check(ownValue(marshalled(c,0),p.diffuse)&&ownValue(marshalled(c,1),p.ambient)&&ownValue(marshalled(c,2),p.direction)&&ownValue(marshalled(c,3),p.fill),"rewritten pointers refer to readable DLL-owned stable values");
    check(std::memcmp(&p,&maplight::daylight,sizeof(p))==0&&maplight::finite(p),"full M map uses the native landmark palette and a valid fixed direction");
    check(!c.cancel&&!c.overrideReturn,"native light upload remains responsible for all GPU calls");
    check(memory==activeBefore&&writes==0,"original color buffers, game clock and every synthetic game byte remain unchanged");
    for(int timeMs : {6*3600000,12*3600000,20*3600000-1}) {
        put(gc+maplight::timeOffset,timeMs);
        // Bright inputs are deliberate: original daytime color/direction and pointers must pass through exactly.
        put(args,std::array<float,4>{1.099f,1.098f,.897f,1});
        const auto daylightBefore=memory;
        const auto daylightSessions=sessionReads;
        unchanged(call(),"daytime including dawn/noon/before-night preserves all native lighting arguments");
        check(memory==daylightBefore&&writes==0,"daytime pass-through preserves game time and source color bytes");
        check(sessionReads==daylightSessions,"daytime map rendering also skips full session/entity-count queries");
    }
    put(args,dark);
    for(int timeMs : {0,6*3600000-1,20*3600000,maplight::dayMs-1}) {
        put(gc+maplight::timeOffset,timeMs);auto boundary=call();handler(&boundary);
        maplight::Palette actual{};
        check(ownValue(marshalled(boundary,0),actual.diffuse)&&ownValue(marshalled(boundary,1),actual.ambient)&&ownValue(marshalled(boundary,2),actual.direction)&&ownValue(marshalled(boundary,3),actual.fill)&&std::memcmp(&actual,&maplight::daylight,sizeof(actual))==0,"night boundaries and midnight retain the supplement palette");
    }
    for(int timeMs : {-1,maplight::dayMs,std::numeric_limits<int>::max()}) {
        put(gc+maplight::timeOffset,timeMs);unchanged(call(),"invalid clock values preserve native lighting");
    }
    memory.erase(gc+maplight::timeOffset);unchanged(call(),"unreadable clock preserves native lighting");
    put(gc+maplight::timeOffset,23*3600000);auto beforeDawn=call();handler(&beforeDawn);
    check(marshalled(beforeDawn,0)!=args,"night supplement can resume after invalid clock reads");
    put(gc+maplight::timeOffset,6*3600000);unchanged(call(),"crossing into daytime while M stays open immediately restores native arguments");
    put(gc+maplight::timeOffset,23*3600000);
    const auto beforeOtherSessions=sessionReads;
    auto other=call();other.self+=4;unchanged(other,"other shader objects keep native lighting");
    check(sessionReads==beforeOtherSessions,"unrelated shader skips the full session query");
    other=call();other.argCount=3;unchanged(other,"wrong argument layout is refused");
    other=call();other.argi[0]=0;unchanged(other,"unreadable color pointer is refused");
    other=call();other.cancel=1;unchanged(other,"prior hook cancellation is respected");
    put(args,std::array<float,4>{std::numeric_limits<float>::quiet_NaN(),1,1,1});unchanged(call(),"invalid native floats are refused without replacing arguments");put(args,dark);
    put(args+0x200,std::array<float,3>{0,0,0});unchanged(call(),"zero native light direction is refused");put(args+0x200,std::array<float,3>{.5f,0,-1});
    world=false;unchanged(call(),"title or loading state cannot enable map lighting");world=true;
    put(gc+maplight::mapOpenOffset,std::uint8_t{2});unchanged(call(),"unknown map flag fails closed");
    put(gc+maplight::mapOpenOffset,std::uint8_t{0});unchanged(call(),"closing map immediately preserves original night arguments");
    shutdown();check(removed==1,"shutdown unregisters exactly one owned hook");
    put(gc+maplight::mapOpenOffset,std::uint8_t{1});unchanged(call(),"captured callback after shutdown cannot apply lighting");
    check(writes==0,"no SDK direct memory write used in any branch");
    std::printf("PASS %u map-light actual DLL checks; synthetic SDK only, no game/GPU run\n",checks);
}

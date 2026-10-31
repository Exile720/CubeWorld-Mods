#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "cube_mod.hpp"
#include "light.hpp"
#include <atomic>

namespace {
std::atomic<bool> enabled{false},reported{false};
bool installed=false;
bool read(unsigned address,void* out,unsigned size) {
    const auto api=cube::mod().raw();return address&&api&&api->mem.read(api,address,out,size)!=0;
}
void lighting(cube::HookCall& call) {
    if(!enabled.load(std::memory_order_acquire)||call.cancelled())return;
    auto& m=cube::mod();unsigned gc=0;
    // This function also runs during ordinary world rendering. Gate on the map
    // before session.get(), which counts resident entities to report playerCount.
    if(!read(m.rebase(maplight::controllerGlobal),&gc,4)||
       !maplight::mapContext(&read,gc,call.self())) {reported.store(false);return;}
    int timeMs=0;
    if(!read(gc+maplight::timeOffset,&timeMs,sizeof(timeMs))||!maplight::night(timeMs)) {
        if(reported.exchange(false))m.log.info("map light: native map lighting restored; night supplement inactive");
        return;
    }
    if(!m.session().isInWorld())return;
    if(call.argCount()!=4)return;
    std::array<unsigned,4> addresses{};for(int i=0;i<4;++i)addresses[i]=static_cast<unsigned>(call.argInt(i));
    maplight::Palette native{};if(!maplight::palette(&read,addresses,native))return;
    // Redirect arguments to immutable DLL-owned values. No time, world, save or native input writes.
    const auto& p=maplight::daylight;
    call.setArgInt(0,reinterpret_cast<int>(p.diffuse.data()));
    call.setArgInt(1,reinterpret_cast<int>(p.ambient.data()));
    call.setArgInt(2,reinterpret_cast<int>(p.direction.data()));
    call.setArgInt(3,reinterpret_cast<int>(p.fill.data()));
    if(!reported.exchange(true))m.log.info("map light: night supplement active at %02d:%02d; original diffuse %.3f %.3f %.3f, ambient %.3f %.3f %.3f; clock untouched",timeMs/3600000,(timeMs/60000)%60,native.diffuse[0],native.diffuse[1],native.diffuse[2],native.ambient[0],native.ambient[1],native.ambient[2]);
}
bool supported() {
    auto& m=cube::mod();const unsigned base=reinterpret_cast<unsigned>(GetModuleHandleW(nullptr));
    const auto dos=m.read<IMAGE_DOS_HEADER>(base);
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0x40||dos.e_lfanew>0x1000)return false;
    const auto nt=m.read<IMAGE_NT_HEADERS32>(base+dos.e_lfanew);
    if(nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||
       nt.FileHeader.TimeDateStamp!=0x51ea955e||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC||
       nt.OptionalHeader.SizeOfImage!=0x3bc000||nt.OptionalHeader.AddressOfEntryPoint!=0x28e1e0)return false;
    const std::array<unsigned char,9> prefix{0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x1c};
    const std::array<unsigned char,3> cleanup{0xc2,0x10,0};
    const std::array<unsigned char,6> shaderLoad{0x8b,0x8f,0x80,0x05,0x80,0};
    const std::array<unsigned char,5> caller{0xe8,0x14,0xa8,0xf9,0xff};
    return m.read<std::array<unsigned char,9>>(m.rebase(maplight::lightFunction))==prefix&&
        m.read<std::array<unsigned char,3>>(m.rebase(0x448274))==cleanup&&
        m.read<std::array<unsigned char,6>>(m.rebase(0x4ad931))==shaderLoad&&
        m.read<std::array<unsigned char,5>>(m.rebase(0x4ad957))==caller;
}
void stop() {
    enabled.store(false,std::memory_order_release);
    if(installed){cube::mod().eventHook.removeRaw(cube::mod().rebase(maplight::lightFunction));installed=false;}
}
}
CUBE_MOD("Map Light","0.1.1","CubeWorld Local Mods") {
    mod.setId("local.cubeworld.map_light");
    // Writes permits argument redirection through the loader's capability gate.
    // The module makes no direct memory writes or world/time setter calls.
    mod.setCapabilities(cube::Capability::RawMem|cube::Capability::RawHooks|cube::Capability::Writes);
    if(!supported()){mod.log.error("map light disabled: unsupported Alpha build or existing render hook");return;}
    installed=mod.eventHook.raw(mod.rebase(maplight::lightFunction),cube::CallConv::Thiscall,4,&lighting);
    if(!installed){mod.log.error("map light disabled: lighting hook unavailable");return;}
    enabled.store(true,std::memory_order_release);
    mod.log.info("map light 0.1.1 ready: night supplement on full M map only; daytime native lighting; clock and saves unchanged");
}
CUBE_MOD_API void CUBE_CALL CubeMod_Shutdown(){stop();}

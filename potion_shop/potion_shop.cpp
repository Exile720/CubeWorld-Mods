#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "cube_mod.hpp"
#include "stock.hpp"
#include <atomic>

namespace {
constexpr unsigned gcGlobal=0x76b1c8, copyFunction=0x44ad30, priceFunction=0x4c76e0;
constexpr unsigned temporaryStock=0x800c0c;
potions::StockSource stockSource;
potions::Offer activeOffer;
std::atomic<bool> enabled{false};
std::atomic<DWORD> gameThread{0};
bool copyInstalled=false,priceInstalled=false;

bool read(unsigned address,void* output,unsigned size) {
    auto api=cube::mod().raw();
    return address && api && api->mem.read(api,address,output,size)!=0;
}
unsigned controller() {
    auto& mod=cube::mod();return mod.read<unsigned>(mod.rebase(gcGlobal));
}
void stockCopy(cube::HookCall& call) {
    if(!enabled.load(std::memory_order_acquire) || call.cancelled()) return;
    const unsigned gc=controller();unsigned selected=0;
    if(!gc || call.self()!=gc+temporaryStock || !potions::itemShopContext(&read,gc,selected) ||
       static_cast<unsigned>(call.argInt(0))!=selected+0x11dc) return;
    // Scratch must stay intact until the loader's raw-hook wrapper runs the real copy.
    // Only this game thread can replace the source. Never change a merchant's inventory.
    DWORD expected=0;const DWORD current=GetCurrentThreadId();
    gameThread.compare_exchange_strong(expected,current,std::memory_order_acq_rel);
    if(gameThread.load(std::memory_order_acquire)!=current) return;
    const auto previous=activeOffer;activeOffer={};potions::Offer next;
    if(!potions::shopOffer(&read,gc,selected,next) || !next.level) return;
    const auto prepared=stockSource.prepare(&read,selected+0x11dc,next.level);
    if(prepared!=potions::Prepare::Added && prepared!=potions::Prepare::Present) return;
    if(prepared==potions::Prepare::Added) {
        // Native assignment copies the whole stock, including the previous-tier potion.
        call.setArgInt(0,static_cast<int>(reinterpret_cast<unsigned>(&stockSource.header)));
    }
    activeOffer=next;
    if(previous.player!=next.player || previous.level!=next.level)
        cube::mod().log.info("potion shop: craft level %d; shop LifePotion level %d; %d copper",
                             next.craftLevel,next.level,next.copper);
}
void itemPrice(cube::HookCall& call) {
    if(!enabled.load(std::memory_order_acquire) || call.cancelled()) return;
    if(gameThread.load(std::memory_order_acquire)!=GetCurrentThreadId()) return;
    const int price=potions::purchasePrice(&read,controller(),call.self(),activeOffer);
    if(price<0) return;
    // Both affordability and money deduction use this exact temporary-stock Item pointer.
    // Player inventory / crafted items / resale retain the original native value function.
    call.cancel();call.setReturnInt(price);
}
bool supportedGame() {
    auto& mod=cube::mod();
    const unsigned base=reinterpret_cast<unsigned>(GetModuleHandleW(nullptr));
    const auto dos=mod.read<IMAGE_DOS_HEADER>(base);
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<0x40 || dos.e_lfanew>0x1000) return false;
    const auto nt=mod.read<IMAGE_NT_HEADERS32>(base+dos.e_lfanew);
    if(nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
       nt.FileHeader.TimeDateStamp!=0x51ea955e || nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
       nt.OptionalHeader.SizeOfImage!=0x3bc000 || nt.OptionalHeader.AddressOfEntryPoint!=0x28e1e0)
        return false;
    // Only compare instructions before absolute relocated operands; refuse existing detours.
    const std::array<std::uint8_t,6> copyBytes={0x55,0x8b,0xec,0x6a,0xff,0x68};
    const std::array<std::uint8_t,8> priceBytes={0x55,0x8b,0xec,0x83,0xec,0x08,0xf3,0x0f};
    return mod.read<std::array<std::uint8_t,6>>(mod.rebase(copyFunction))==copyBytes &&
           mod.read<std::array<std::uint8_t,8>>(mod.rebase(priceFunction))==priceBytes;
}
void stop() {
    enabled.store(false,std::memory_order_release);
    auto& mod=cube::mod();
    // Generic raw hooks disable and drain their original calls before releasing trampolines.
    if(copyInstalled) {mod.eventHook.removeRaw(mod.rebase(copyFunction));copyInstalled=false;}
    if(priceInstalled) {mod.eventHook.removeRaw(mod.rebase(priceFunction));priceInstalled=false;}
}
}

CUBE_MOD("Potion Shop","0.2.0","CubeWorld Local Mods") {
    mod.setId("local.cubeworld.potion-shop");
    // RawHooks installs the handlers; Writes also permits their argument/return changes.
    // Without Writes the loader silently restores both the stock source and the price.
    mod.setCapabilities(cube::Capability::RawMem|cube::Capability::RawHooks|cube::Capability::Writes);
    if(!supportedGame()) {mod.log.error("potion shop disabled: unsupported Alpha executable or conflicting hook");return;}
    priceInstalled=mod.eventHook.raw(mod.rebase(priceFunction),cube::CallConv::Thiscall,0,&itemPrice);
    if(priceInstalled)
        copyInstalled=mod.eventHook.raw(mod.rebase(copyFunction),cube::CallConv::Thiscall,1,&stockCopy);
    if(!copyInstalled || !priceInstalled) {stop();mod.log.error("potion shop disabled: hook installation failed");return;}
    enabled.store(true,std::memory_order_release);
    mod.log.info("potion shop 0.2.0 ready: one recipe tier below crafting; adaptive copper price; single player; resale unchanged");
}

CUBE_MOD_API void CUBE_CALL CubeMod_Shutdown() {stop();}

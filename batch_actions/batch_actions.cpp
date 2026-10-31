#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "cube_mod.hpp"
#include "batch.hpp"
#include "panel.hpp"
#include "explore_font.hpp"
#include <atomic>
#include <cmath>

namespace {
std::atomic<bool> enabled{false};
bool buyInstalled=false,craftInstalled=false,menuInstalled=false;
thread_local bool replaying=false;
unsigned frameToken=0;
std::atomic<DWORD> actionThread{0};
exploreui::TextFont font;
struct Pending {
    bool active=false,purchase=true,show=false,confirm=false;
    unsigned gc=0;batch::Buy buy;batch::Craft craft;
    std::array<std::int64_t,3> point{};
} pending;
batchui::View view;
bool read(unsigned address,void* out,unsigned bytes) {
    const auto api=cube::mod().raw();if(!api||!address||bytes>0x100000)return false;
    if(api->mem.read(api,address,out,bytes))return true;
    if(bytes<=4096-(address&4095))return false;
    auto target=static_cast<unsigned char*>(out);
    while(bytes) {const unsigned n=(std::min)(bytes,4096-(address&4095));
        if(!api->mem.read(api,address,target,n))return false;address+=n;target+=n;bytes-=n;}
    return true;
}
unsigned controller() {unsigned gc=0;read(cube::mod().rebase(0x76b1c8),&gc,4);return gc;}
bool systemFocused() {
    DWORD process=0;const HWND window=GetForegroundWindow();
    return window&&GetWindowThreadProcessId(window,&process)&&process==GetCurrentProcessId();
}
bool (*focused)()=&systemFocused;
struct Replay {Replay(){replaying=true;}~Replay(){replaying=false;}};
void nativeBuy(unsigned gc) {using Fn=void(__thiscall*)(void*,int);reinterpret_cast<Fn>(cube::mod().rebase(batch::buyFunction))(reinterpret_cast<void*>(gc),2);}
void nativeCraft(unsigned gc) {using Fn=void(__thiscall*)(void*);reinterpret_cast<Fn>(cube::mod().rebase(batch::craftFunction))(reinterpret_cast<void*>(gc));}
bool equipmentHovered(unsigned gc) {
    using Fn=unsigned(__thiscall*)(void*);
    const unsigned address=reinterpret_cast<Fn>(cube::mod().rebase(0x47b1b0))(reinterpret_cast<void*>(gc));
    unsigned char type=0;return address&&(!read(address,&type,1)||type!=0);
}
void buy(cube::HookCall& call) {
    if(!enabled.load()||replaying||call.cancelled()||call.argInt(0)<0||call.argInt(0)>2)return;
    const unsigned gc=controller();batch::Buy origin;
    if(call.self()!=gc||!batch::buying(&read,gc,origin,false)||equipmentHovered(gc))return;
    const auto api=cube::mod().raw();const int price=api->items.value(api,origin.stock);
    if(price<1)return;
    const DWORD current=GetCurrentThreadId();DWORD expected=0;actionThread.compare_exchange_strong(expected,current);
    if(actionThread.load()!=current)return;
    if(pending.active){if(pending.purchase)call.cancel();return;}
    Pending next;next.active=next.show=true;next.gc=gc;next.buy=origin;
    if(!read(origin.player+0x10,next.point.data(),sizeof(next.point)))return;
    call.cancel();call.setReturnInt(0);
    pending=next;view={};view.price=price;
    cube::mod().log.info("batch supplies: quantity dialog requested for empty flasks; owned=%d, price=%d",origin.owned,price);
}
void craft(cube::HookCall& call) {
    if(!enabled.load()||replaying||call.cancelled())return;
    const unsigned gc=controller();batch::Craft origin;
    if(call.self()!=gc||!batch::crafting(&read,gc,origin,false))return;
    const DWORD current=GetCurrentThreadId();DWORD expected=0;actionThread.compare_exchange_strong(expected,current);
    if(actionThread.load()!=current)return;
    if(pending.active){if(!pending.purchase&&batch::sameContext(pending.craft,origin))call.cancel();return;}
    Pending next;next.active=next.show=true;next.purchase=false;next.gc=gc;next.craft=origin;
    if(!read(origin.player+0x10,next.point.data(),sizeof(next.point)))return;
    call.cancel();call.setReturnInt(0);
    pending=next;view={};view.purchase=false;
    cube::mod().log.info("batch supplies: quantity dialog requested for craft type=%u subtype=%u level=%d",origin.output.bytes[0],origin.output.bytes[1],batch::field<short>(origin.output,0x10));
}
bool write(unsigned address,const void* value,unsigned bytes) {
    const auto api=cube::mod().raw();return api&&api->mem.write&&api->mem.write(api,address,value,bytes)!=0;
}
// Only temporary UI selection/input fields are written; the game owns coins and inventory.
// Input blocking may move native hover to its mouse-look anchor, so bind the captured shop cell
// for the duration of one native middle-button purchase (same guarded native purchase branch).
class PurchaseUi {
    unsigned gc_=0,bag_=0,shop_=0,player_=0;
    std::array<int,2> bagHover_{},shopHover_{};
    unsigned char character_=0,middle_=0;unsigned changed_=0;
public:
    bool ok=false;
    PurchaseUi(unsigned gc,const batch::Buy& pin):gc_(gc),shop_(pin.widget),player_(pin.player) {
        unsigned char skills=1,map=1;
        if(!read(gc+0x800954,&bag_,4)||!bag_||!read(bag_+0x184,bagHover_.data(),8)||
           !read(shop_+0x184,shopHover_.data(),8)||!read(gc+0x8008f0,&character_,1)||
           !read(gc+0x11b,&middle_,1)||!read(gc+0x8008f1,&skills,1)||skills||
           !read(gc+0x8006e4,&map,1)||map)return;
        const std::array<int,2> noHover{-1,-1},target{pin.row,pin.col};const unsigned char closed=0;
        if(!write(bag_+0x184,noHover.data(),8))return;changed_=1;
        if(!write(shop_+0x184,target.data(),8))return;changed_=2;
        if(!write(gc+0x8008f0,&closed,1))return;changed_=3;ok=true;
    }
    ~PurchaseUi() {
        if(!changed_)return;
        unsigned player=0,bag=0,shop=0;
        bool restored=controller()==gc_&&read(gc_+0x8006d0,&player,4)&&player==player_&&
            read(gc_+0x800954,&bag,4)&&bag==bag_&&read(gc_+0x80095c,&shop,4)&&shop==shop_;
        if(restored) {
            if(changed_>=3){const bool a=write(gc_+0x8008f0,&character_,1),b=write(gc_+0x11b,&middle_,1);restored=a&&b;}
            if(changed_>=2)restored=write(shop_+0x184,shopHover_.data(),8)&&restored;
            restored=write(bag_+0x184,bagHover_.data(),8)&&restored;
        }
        if(!restored){enabled.store(false);cube::mod().log.error("batch supplies stopped: temporary purchase UI could not be restored");}
    }
};
void closeDialog() {pending={};view={};cube::mod().menu().setOpen(false);}
bool stationary() {
    std::array<std::int64_t,3> now{};const unsigned player=pending.purchase?pending.buy.player:pending.craft.player;
    if(!read(player+0x10,now.data(),sizeof(now)))return false;
    for(unsigned i=0;i<3;++i)if(std::abs(static_cast<long double>(now[i])-pending.point[i])>65536)return false;
    return true;
}
bool refreshView() {
    const unsigned gc=pending.gc;
    if(!pending.active||controller()!=gc||!stationary())return false;
    if(pending.purchase) {
        batch::Buy now;
        if(!batch::buying(&read,gc,now,false,&pending.buy)||!batch::sameContext(pending.buy,now))return false;
        const auto api=cube::mod().raw();const int price=api->items.value(api,now.stock);
        if(price<1||price!=view.price)return false;
        view.title="空瓶";view.owned=now.owned;view.coins=now.coins;view.quantity.fit(batch::maximum(now,price));
    } else {
        batch::Craft now;
        if(!batch::crafting(&read,gc,now,false)||!batch::sameContext(pending.craft,now))return false;
        view.owned=now.owned;view.quantity.fit(batch::maximum(now));
        const bool water=now.output.bytes[0]==11;
        view.title=water?"水瓶":"生命药水 · 等级 "+std::to_string(batch::field<short>(now.output,0x10));
        view.materials=water?"材料：空瓶（持有 "+std::to_string(now.available[0])+" 个）":
            "材料：心之花 "+std::to_string(now.available[0])+" / 水瓶 "+std::to_string(now.available[1])+"（每份各 1 个）";
    }
    return true;
}
void CUBE_CALL frame(CubeEventArgs*) {
    if(!enabled.load()||!pending.active)return;
    if(actionThread.load()!=GetCurrentThreadId()) {
        enabled.store(false);cube::mod().log.error("batch supplies stopped: render and native action thread differ");return;
    }
    if(!focused()||!refreshView()){closeDialog();return;}
    if(pending.show){pending.show=false;cube::mod().menu().setOpen(true);}
    if(!pending.confirm)return;
    pending.confirm=false;view.working=false;
    const unsigned wanted=static_cast<unsigned>(view.quantity.value);unsigned done=0;
    const auto valid=[&]{return enabled.load()&&focused()&&controller()==pending.gc&&stationary();};
    Replay guard;
    if(pending.purchase) {
        done=batch::buyBatch(&read,pending.gc,valid,[&]{PurchaseUi ui(pending.gc,pending.buy);if(ui.ok)nativeBuy(pending.gc);},view.price,wanted,&pending.buy);
    } else done=batch::craftBatch(&read,pending.gc,valid,[&]{nativeCraft(pending.gc);},wanted);
    cube::mod().log.info("batch supplies: confirmed %u, completed %u (%s)",wanted,done,pending.purchase?"purchase":"craft");
    view.notice=done?"已完成 "+std::to_string(done)+" 个。":"没有完成操作，已停止；请检查余额、材料与日志。";
    if(done<wanted&&done)view.notice+="剩余部分已停止。";
    if(!refreshView()){closeDialog();return;}
}
void draw() {
    if(!enabled.load()||!pending.active){cube::mod().menu().setOpen(false);return;}
    exploreui::ScopedTextFont ownFont(font.ensure(ImGui::GetFont()));
    const auto action=batchui::draw(view);
    if(action.close)closeDialog();
    else if(action.confirm){pending.confirm=true;view.working=true;}
}
template<std::size_t N>bool signature(unsigned address,const std::array<unsigned char,N>& expected) {
    std::array<unsigned char,N> actual{};return read(cube::mod().rebase(address),actual.data(),N)&&actual==expected;
}
bool supported() {
    const unsigned base=reinterpret_cast<unsigned>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
    if(!read(base,&dos,sizeof(dos))||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0x40||dos.e_lfanew>0x1000||
       !read(base+dos.e_lfanew,&nt,sizeof(nt))||nt.Signature!=IMAGE_NT_SIGNATURE||
       nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||nt.FileHeader.TimeDateStamp!=0x51ea955e||
       nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC||nt.OptionalHeader.SizeOfImage!=0x3bc000||
       nt.OptionalHeader.AddressOfEntryPoint!=0x28e1e0)return false;
    return signature(0x47b611,std::array<unsigned char,6>{0x81,0xec,0xd0,3,0,0})&&
        signature(0x4709d1,std::array<unsigned char,6>{0x81,0xec,0x6c,2,0,0})&&
        signature(0x47b1b0,std::array<unsigned char,6>{0x56,0x8b,0xf1,0x80,0xbe,0xf0});
}
bool buyEntrySafe() {
    if(signature(batch::buyFunction,std::array<unsigned char,6>{0x55,0x8b,0xec,0x6a,0xff,0x68}))return true;
    std::array<unsigned char,5> jump{};const unsigned address=cube::mod().rebase(batch::buyFunction);
    if(!read(address,jump.data(),5)||jump[0]!=0xe9)return false;
    int relative=0;std::memcpy(&relative,jump.data()+1,4);HMODULE owner=nullptr;
    const unsigned target=address+5+relative;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(target),&owner)&&owner==GetModuleHandleW(L"cube_mod.dll");
}
void stop() {
    enabled.store(false);
    auto& mod=cube::mod();
    if(craftInstalled){mod.eventHook.removeRaw(mod.rebase(batch::craftFunction));craftInstalled=false;}
    if(buyInstalled){mod.eventHook.removeRaw(mod.rebase(batch::buyFunction));buyInstalled=false;}
    const auto api=mod.raw();if(frameToken&&api&&api->events.unsubscribe){api->events.unsubscribe(api,frameToken);frameToken=0;}
    if(menuInstalled&&api&&api->overlay.unregisterMenu){api->overlay.unregisterMenu(api,mod.menu().handle());menuInstalled=false;}
}
void ready() {
    // All mods have now initialized: InventorySort may validate/install its own shared buy hook first.
    if(buyInstalled||craftInstalled||!supported()||!buyEntrySafe()||
       !signature(batch::craftFunction,std::array<unsigned char,6>{0x55,0x8b,0xec,0x6a,0xff,0x68})) {
        cube::mod().log.error("batch supplies disabled: unsupported build or conflicting function hook");return;
    }
    auto& mod=cube::mod();
    buyInstalled=mod.eventHook.raw(mod.rebase(batch::buyFunction),cube::CallConv::Thiscall,1,&buy);
    if(buyInstalled)craftInstalled=mod.eventHook.raw(mod.rebase(batch::craftFunction),cube::CallConv::Thiscall,0,&craft);
    if(!craftInstalled){stop();mod.log.error("batch supplies disabled: hooks unavailable");return;}
    enabled.store(true);mod.log.info("batch supplies 0.1.2 ready: visible quantity dialog, A/D +/-10, explicit confirmation, original carry limit");
}
}
CUBE_MOD("Batch Supplies","0.1.2","CubeWorld Local Mods") {
    mod.setId("local.cubeworld.batch-actions");
    mod.setCapabilities(cube::Capability::RawMem|cube::Capability::RawHooks|cube::Capability::Writes|cube::Capability::Overlay);
    mod.setPriority(100);
    if(!supported()){mod.log.error("batch supplies disabled: unsupported Alpha executable");return;}
    const auto api=mod.raw();
    if(!api->events.subscribe||!api->events.unsubscribe||!api->mem.write||!api->items.value||!api->overlay.registerMenu){mod.log.error("batch supplies disabled: required menu/native APIs missing");return;}
    mod.menu().setToggleKey(0).setOpen(false).onDraw(&draw);
    if(!mod.menu().handle()){mod.log.error("batch supplies disabled: quantity menu registration failed");return;}
    menuInstalled=true;
    frameToken=api->events.subscribe(api,CUBE_EVENT_FRAME,&frame,nullptr);
    if(!frameToken){stop();mod.log.error("batch supplies disabled: frame subscription failed");return;}
    mod.eventListener.on(cube::Event::Ready,&ready);
}
CUBE_MOD_API void CUBE_CALL CubeMod_Shutdown(){stop();}

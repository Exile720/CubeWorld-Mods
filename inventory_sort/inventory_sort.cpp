#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "cube_mod.hpp"
#include "grouping.hpp"
#include "sections_ui.hpp"
#include "../region_explorer/explore_font.hpp"
#include <atomic>
#include <cstdio>
#include <mutex>

namespace {
using Draw=void(__thiscall*)(void*);
using Select=void(__thiscall*)(void*,int);
Draw originalDraw=nullptr;
Draw originalScroll=nullptr;
Select originalSelect=nullptr;
std::atomic<bool> enabled{false};
std::array<std::atomic<bool>,inventory::maxPages> pageReported{};
std::array<std::atomic<bool>,2> popupReported{};
std::array<bool,5> installed{};
thread_local inventory::View view;
thread_local inventory::Layout layout;
thread_local bool drawing=false;
thread_local bool scrolling=false;
thread_local inventory::View scrollView;
thread_local inventoryui::Frame nativeFrame;
std::mutex frameMutex;
inventoryui::Frame overlayFrame;
ULONGLONG frameTime=0;
exploreui::TextFont textFont;
unsigned menuHandle=0;
bool read(unsigned a,void* out,unsigned n) {
    const auto api=cube::mod().raw();
    if(!a||!out||!n||!api||!api->mem.read||std::uint64_t(a)+n>0x100000000ULL)return false;
    if(api->mem.read(api,a,out,n))return true;
    // The loader requires each read to stay in one VirtualQuery region. A valid
    // native vector can cross adjacent readable regions, so retry at page edges.
    if(n<=0x1000-(a&0xfff))return false;
    auto* bytes=static_cast<unsigned char*>(out);
    while(n) {
        const unsigned part=std::min(n,0x1000-(a&0xfff));
        if(!api->mem.read(api,a,bytes,part))return false;
        a+=part;bytes+=part;n-=part;
    }
    return true;
}
template<class T>bool write(unsigned a,const T& value) {
    const auto api=cube::mod().raw();return a&&api&&api->mem.write(api,a,&value,sizeof(value))!=0;
}
unsigned controller(){unsigned gc=0;read(cube::mod().rebase(inventory::controllerGlobal),&gc,4);return gc;}
void clearHover(unsigned widget) {
    const inventory::Slot none{};write(widget+inventory::hoverOffset,none);write(widget+inventory::selectionOffset,none);
}
bool prepare(unsigned self,int columns) {
    try {return view.prepare(&read,controller(),self,cube::mod().rebase(inventory::creatureVtable),columns,&layout);}
    catch(...) {view.valid=false;return false;}
}
void clearFrame() {std::lock_guard<std::mutex> lock(frameMutex);overlayFrame.valid=false;}
bool geometry(unsigned self) {
    using Position=void(__thiscall*)(void*,float*);
    using Size=float(__thiscall*)(void*);
    float pos[2]{};
    auto& m=cube::mod();
    __try {
        reinterpret_cast<Position>(m.rebase(inventory::positionFunction))(reinterpret_cast<void*>(self),pos);
        nativeFrame.width=reinterpret_cast<Size>(m.rebase(inventory::widthFunction))(reinterpret_cast<void*>(self));
        nativeFrame.height=reinterpret_cast<Size>(m.rebase(inventory::heightFunction))(reinterpret_cast<void*>(self));
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    nativeFrame.x=pos[0];nativeFrame.y=pos[1];
    return read(self+0x1a8,&nativeFrame.cellW,4)&&read(self+0x1ac,&nativeFrame.cellH,4)&&inventoryui::geometry(nativeFrame);
}
bool nodeVisible(unsigned panel,bool& shown) {
    unsigned state=0,array=0,value=0;int index=-1;
    if(!panel||!read(panel+0x3c,&state,4)||
       !read(state+0x68,&index,4)||index<0||index>=256||!read(state+0x94,&array,4)||
       !read(array+index*4,&value,4))return false;
    shown=value!=0;return true;
}
bool visible(unsigned gc) {
    unsigned panel=0;bool shown=false;
    return read(gc+0x8008bc,&panel,4)&&nodeVisible(panel,shown)&&shown;
}
bool popupBounds(unsigned node,inventoryui::Rect& r) {
    unsigned shape=0,vtable=0;
    std::array<float,4> local{};std::array<float,16> matrix{};
    if(!read(node+0x34,&shape,4)||!shape||!read(shape,&vtable,4)||
       vtable!=cube::mod().rebase(inventory::popupShapeVtable)||
       !read(shape+inventory::popupBoundsOffset,local.data(),sizeof(local))||
       !read(node+0x48,matrix.data(),sizeof(matrix)))return false;
    // SmoothMeshShape caches LOCAL bounds. The node's current matrix moves/scales
    // those bounds into the same screen coordinates used by the native bag.
    return inventoryui::screenBounds(local,matrix,r);
}
bool popupOcclusions(inventoryui::Frame& f) {
    f.occlusions.clear();
    for(std::size_t i=0;i<inventory::popupNodeOffsets.size();++i) {
        const unsigned offset=inventory::popupNodeOffsets[i];
        unsigned node=0;bool shown=false;
        if(!read(f.gc+offset,&node,4))return false;
        if(!node)continue;
        if(!nodeVisible(node,shown))return false;
        if(!shown)continue;
        inventoryui::Rect r{};
        // Never cover a visible native popup when its bounds cannot be verified.
        if(!popupBounds(node,r))return false;
        f.occlusions.push_back(r);
        if(!popupReported[i].exchange(true)) {
            char message[192]{};
            std::snprintf(message,sizeof(message),"native popup mask %s: screen=(%.2f,%.2f,%.2f,%.2f), bag=(%.2f,%.2f)",
                          i?"equipped":"item",r.x,r.y,r.w,r.h,f.x,f.y);
            cube::mod().log.info(message);
        }
    }
    return true;
}
void drawSections() {
    if(!enabled.load())return;
    inventoryui::Frame f;
    {std::lock_guard<std::mutex> lock(frameMutex);if(!overlayFrame.valid||GetTickCount64()-frameTime>200)return;f=overlayFrame;}
    if(controller()!=f.gc||!visible(f.gc))return;
    int page=-1;if(!read(f.widget+inventory::pageOffset,&page,4)||page!=f.page||!popupOcclusions(f))return;
    exploreui::ScopedTextFont fontScope(textFont.ensure(ImGui::GetFont()));
    auto* d=ImGui::GetForegroundDrawList();const auto flags=d->Flags;
    d->Flags&=~ImDrawListFlags_AntiAliasedFill;
    inventoryui::draw(d,f);
    POINT p{};const auto window=GetForegroundWindow();DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(pid==GetCurrentProcessId()&&GetCursorPos(&p)&&ScreenToClient(window,&p)&&!inventoryui::occluded(f,static_cast<float>(p.x),static_cast<float>(p.y))) {
        for(const auto& s:f.sections)if(inventoryui::headerVisible(f,s)) {
            const auto r=inventoryui::headerRect(f,s);
            if(p.x>=r.x&&p.x<r.x+r.w&&p.y>=r.y&&p.y<r.y+r.h) {
                navui::handCursor(d,ImGui::GetIO().DisplaySize,std::clamp(f.cellH/75.0f,.7f,1.4f),
                                 {static_cast<double>(p.x),static_cast<double>(p.y)},true);break;
            }
        }
    }
    d->Flags=flags;
}
// The native renderer also reads coins at items+0x128/+0x12c. prefix contains
// those original bytes as well as the shadow vector header; only the UI pointer changes.
bool beginDraw(unsigned self) {
    unsigned own=0;const unsigned gc=controller();
    // Other native InventoryWidgets (shop/recipes) must not invalidate the player's map.
    if(!gc||!read(gc+inventory::widgetOffset,&own,4)||own!=self)return false;
    if(!geometry(self)){view.valid=false;clearFrame();return false;}
    if(!prepare(self,nativeFrame.columns)){clearFrame();return false;}
    const unsigned shadow=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(view.prefix.data()));
    if(!write(self+inventory::itemsOffset,shadow)){view.valid=false;clearFrame();return false;}
    drawing=true;return true;
}
void endDraw(unsigned self) {
    inventory::Slot hovered{};
    read(self+inventory::hoverOffset,&hovered,sizeof(hovered));
    const auto native=view.native(hovered);
    // Restore the actual inventory before any native mouse/transaction handler runs.
    const bool restored=write(self+inventory::itemsOffset,view.origin);
    const bool current=restored&&view.current(&read,controller(),cube::mod().rebase(inventory::creatureVtable));
    const auto safe=current?native:inventory::Slot{};
    view.displayedHover=current?hovered:inventory::Slot{};
    write(self+inventory::hoverOffset,safe);
    drawing=false;
    if(restored) {
        unsigned scroll=0;int row=-1;
        if(read(self+0x198,&scroll,4)&&read(scroll+view.page*4,&row,4)&&row>=0&&row<=4096) {
            try {
                nativeFrame.widget=self;nativeFrame.gc=controller();nativeFrame.page=view.page;
                nativeFrame.firstRow=row;nativeFrame.sections=view.sections;nativeFrame.valid=true;
                std::lock_guard<std::mutex> lock(frameMutex);overlayFrame=nativeFrame;frameTime=GetTickCount64();
            } catch(...) {clearFrame();}
        } else clearFrame();
    }
    if(!restored){enabled.store(false);cube::mod().log.error("inventory grouping stopped: UI pointer restoration failed");}
    else if(current&&!pageReported[view.page].exchange(true)) {
        char message[192]{};
        std::snprintf(message,sizeof(message),"inventory sections active: page=%d, native cells=%u, sections=%u, columns=%d; native slots restored after drawing",
                      view.page,static_cast<unsigned>(view.source.size()),static_cast<unsigned>(view.sections.size()),view.columns);
        cube::mod().log.info(message);
    }
}
void __fastcall draw(void* object,void*) {
    const auto original=originalDraw;
    if(!original)return;
    const unsigned self=reinterpret_cast<unsigned>(object);
    if(!enabled.load()||drawing||!beginDraw(self)){original(object);return;}
    // A native exception must still restore the widget; SEH cleanup owns no C++ objects.
    __try {original(object);}
    __finally {endDraw(self);}
}
bool beginScroll(unsigned self) {
    unsigned own=0;const unsigned gc=controller();
    if(!gc||!read(gc+inventory::widgetOffset,&own,4)||own!=self||!geometry(self))return false;
    try {if(!scrollView.prepare(&read,gc,self,cube::mod().rebase(inventory::creatureVtable),nativeFrame.columns,&layout))return false;}
    catch(...) {return false;}
    const unsigned shadow=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(scrollView.prefix.data()));
    if(!write(self+inventory::itemsOffset,shadow))return false;
    scrolling=true;return true;
}
void endScroll(unsigned self) {
    if(!write(self+inventory::itemsOffset,scrollView.origin)){enabled.store(false);cube::mod().log.error("inventory grouping stopped: scroll UI pointer restoration failed");}
    scrolling=false;
}
void __fastcall scroll(void* object,void*) {
    const auto original=originalScroll;if(!original)return;
    const unsigned self=reinterpret_cast<unsigned>(object);
    if(!enabled.load()||drawing||scrolling||!beginScroll(self)){original(object);return;}
    // Native item transactions also recalculate scrolling outside rendering. Use a
    // separate temporary view: updating the last-drawn click map here would make stale clicks valid.
    __try {original(object);}
    __finally {endScroll(self);}
}
void staleGuard(cube::HookCall& call) {
    if(!enabled.load()||drawing||!view.valid)return;
    const unsigned gc=controller();
    if(call.self()!=gc)return;
    if(!view.current(&read,gc,cube::mod().rebase(inventory::creatureVtable))) {
        unsigned own=0;
        // Never write a stale/freed widget on a character change.
        if(read(gc+inventory::widgetOffset,&own,4)&&own==view.widget)clearHover(own);
    }
}
bool beginSelect(unsigned self,inventory::Slot& hover,inventory::Slot& selected) {
    if(!view.valid||self!=view.widget)return false;
    const unsigned gc=controller();
    if(!view.current(&read,gc,cube::mod().rebase(inventory::creatureVtable))) {clearHover(self);return false;}
    if(!read(self+inventory::hoverOffset,&hover,sizeof(hover))||
       !read(self+inventory::selectionOffset,&selected,sizeof(selected)))return false;
    const auto h=view.display(hover),s=view.display(selected);
    if(!write(self+inventory::hoverOffset,h))return false;
    if(!write(self+inventory::selectionOffset,s)){write(self+inventory::hoverOffset,hover);return false;}
    return true;
}
void endSelect(unsigned self,const inventory::Slot& hover) {
    inventory::Slot selected{};read(self+inventory::selectionOffset,&selected,sizeof(selected));
    const auto native=view.native(selected);
    write(self+inventory::selectionOffset,native);write(self+inventory::hoverOffset,hover);
}
void __fastcall select(void* object,void*,int flag) {
    const auto original=originalSelect;
    if(!original)return;
    const unsigned self=reinterpret_cast<unsigned>(object);
    inventory::Slot hover{},selected{};
    if(!enabled.load()||drawing||!beginSelect(self,hover,selected)){original(object,flag);return;}
    __try {original(object,flag);}
    __finally {endSelect(self,hover);}
}
template<std::size_t N>bool signature(unsigned address,const std::array<unsigned char,N>& expected) {
    std::array<unsigned char,N> bytes{};
    return read(cube::mod().rebase(address),bytes.data(),N)&&bytes==expected;
}
bool supported() {
    const unsigned base=reinterpret_cast<unsigned>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
    if(!read(base,&dos,sizeof(dos))||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0x40||dos.e_lfanew>0x1000||
       !read(base+dos.e_lfanew,&nt,sizeof(nt))||nt.Signature!=IMAGE_NT_SIGNATURE||
       nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||nt.FileHeader.TimeDateStamp!=0x51ea955e||
       nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC||nt.OptionalHeader.SizeOfImage!=0x3bc000||
       nt.OptionalHeader.AddressOfEntryPoint!=0x28e1e0)return false;
    return signature(inventory::drawFunction,std::array<unsigned char,6>{0x55,0x8b,0xec,0x6a,0xff,0x68})&&
        signature(0x4c2090,std::array<unsigned char,7>{0x83,0xbb,0x60,0x01,0,0,0})&&
        signature(0x4c20ab,std::array<unsigned char,10>{0xc7,0x83,0x84,0x01,0,0,0xff,0xff,0xff,0xff})&&
        signature(0x4c597a,std::array<unsigned char,1>{0xc3})&&
        signature(inventory::selectFunction,std::array<unsigned char,6>{0x55,0x8b,0xec,0x56,0x8b,0xf1})&&
        signature(inventory::scrollFunction,std::array<unsigned char,9>{0x55,0x8b,0xec,0x83,0xec,0x0c,0x56,0x8b,0xf1})&&
        signature(0x4c679c,std::array<unsigned char,5>{0x5e,0x5d,0xc2,4,0})&&
        signature(inventory::leftFunction,std::array<unsigned char,6>{0x55,0x8b,0xec,0x6a,0xff,0x68})&&
        signature(0x47b611,std::array<unsigned char,6>{0x81,0xec,0xd0,0x03,0,0})&&
        signature(inventory::rightFunction,std::array<unsigned char,6>{0x55,0x8b,0xec,0x6a,0xff,0x68})&&
        signature(0x47dde1,std::array<unsigned char,3>{0x83,0xec,0x3c})&&
        signature(inventory::positionFunction,std::array<unsigned char,6>{0x55,0x8b,0xec,0x83,0xec,0x44})&&
        signature(inventory::widthFunction,std::array<unsigned char,6>{0x55,0x8b,0xec,0x83,0xec,0x0c})&&
        signature(inventory::heightFunction,std::array<unsigned char,6>{0x55,0x8b,0xec,0x83,0xec,0x0c});
}
void stop() {
    enabled.store(false);
    clearFrame();
    const auto api=cube::mod().raw();
    if(menuHandle&&api&&api->overlay.unregisterMenu){api->overlay.unregisterMenu(api,menuHandle);menuHandle=0;}
    const std::array<unsigned,5> addresses{inventory::drawFunction,inventory::selectFunction,inventory::leftFunction,inventory::rightFunction,inventory::scrollFunction};
    for(int i=4;i>=0;--i)if(installed[i]){cube::mod().eventHook.removeRaw(cube::mod().rebase(addresses[i]));installed[i]=false;}
    // Supported management is with the game exited. No runtime hot unloading is exposed.
}
}
CUBE_MOD("Inventory Grouping","0.1.7","CubeWorld Local Mods") {
    mod.setId("local.cubeworld.inventory_sort");
    mod.setCapabilities(cube::Capability::RawMem|cube::Capability::RawHooks|cube::Capability::Writes|cube::Capability::Overlay);
    if(!supported()){mod.log.error("inventory grouping disabled: unsupported Alpha build or conflicting inventory hook");return;}
    installed[0]=mod.eventHook.rawDetour(mod.rebase(inventory::drawFunction),reinterpret_cast<void*>(&draw),reinterpret_cast<void**>(&originalDraw));
    if(installed[0])installed[1]=mod.eventHook.rawDetour(mod.rebase(inventory::selectFunction),reinterpret_cast<void*>(&select),reinterpret_cast<void**>(&originalSelect));
    if(installed[1])installed[2]=mod.eventHook.raw(mod.rebase(inventory::leftFunction),cube::CallConv::Thiscall,1,&staleGuard);
    if(installed[2])installed[3]=mod.eventHook.raw(mod.rebase(inventory::rightFunction),cube::CallConv::Thiscall,1,&staleGuard);
    if(installed[3])installed[4]=mod.eventHook.rawDetour(mod.rebase(inventory::scrollFunction),reinterpret_cast<void*>(&scroll),reinterpret_cast<void**>(&originalScroll));
    if(!installed[4]){stop();mod.log.error("inventory grouping disabled: inventory hooks unavailable");return;}
    if(!mod.raw()->overlay.registerMenu){stop();mod.log.error("inventory grouping disabled: category heading overlay unavailable");return;}
    mod.menu().setToggleKey(0).setOpen(true).onDraw(&drawSections).setPassthrough(true);
    menuHandle=mod.menu().handle();
    if(!menuHandle){stop();mod.log.error("inventory grouping disabled: category heading overlay registration failed");return;}
    enabled.store(true);
    mod.log.info("inventory grouping 0.1.7 ready: sold and held slots retain their category positions; visible empty cells accept native drops; popup masks and guarded region reads retained");
}
CUBE_MOD_API void CUBE_CALL CubeMod_Shutdown(){stop();}

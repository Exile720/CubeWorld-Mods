#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "cube_sdk.h"
#include "grouping.hpp"
#include "imgui.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace {
unsigned checks=0,draws=0,selects=0,removed=0,menus=0,unregistered=0;
unsigned gc=0x10000000;
constexpr unsigned player=0x20000000,widget=0x30000000,rows=0x40000000,cells=0x50000000,other=0x60000000;
constexpr unsigned popupNode=0x14000000,comparisonNode=0x15000000,popupVisible=0x14002000,comparisonVisible=0x15002000;
constexpr unsigned popupShape=0x16000000,comparisonShape=0x17000000;
std::map<unsigned,std::vector<unsigned char>> memory;
std::map<unsigned,void*> detours;
std::map<unsigned,CubeHookFn> hooks;
std::vector<std::pair<unsigned,unsigned>> writes;
const CubeApi* sdk=nullptr;
int hoverDisplay=0,selectedDisplay=-1;
bool shadow=false,partial=false,throwDraw=false,overlayUnavailable=false;
float paneWidth=650,paneHeight=620,paneX=20,paneY=20;
std::array<float,4> popupArea{400,240,360,370},comparisonArea{60,380,240,230};
unsigned boundsReads=0,failRead=0;
unsigned splitBoundary=0;
unsigned failWrite=0;
CubeOverlayDrawFn overlayDraw=nullptr;
void* overlayUser=nullptr;
std::vector<inventory::Cell> drawn;
void check(bool ok,const char* text){++checks;if(!ok){std::printf("FAIL %s\n",text);std::exit(1);}}
template<class T>void put(unsigned a,const T& v){auto& b=memory[a];b.resize(sizeof(v));std::memcpy(b.data(),&v,sizeof(v));}
template<class T>void array(unsigned a,const std::vector<T>& v){auto& b=memory[a];b.resize(v.size()*sizeof(T));if(!b.empty())std::memcpy(b.data(),v.data(),b.size());}
bool processRead(unsigned a,void* out,unsigned n){
    MEMORY_BASIC_INFORMATION info{};
    if(!a||!VirtualQuery(reinterpret_cast<void*>(a),&info,sizeof(info))||info.State!=MEM_COMMIT||
       (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))||std::uint64_t(a)+n>reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize)return false;
    std::memcpy(out,reinterpret_cast<void*>(a),n);return true;
}
int32_t CUBE_CALL read(const CubeApi*,uint32_t a,void* out,uint32_t n){
    if(a==failRead)return 0;
    if(splitBoundary&&a<splitBoundary&&std::uint64_t(a)+n>splitBoundary)return 0;
    if(a==popupShape+inventory::popupBoundsOffset||a==comparisonShape+inventory::popupBoundsOffset)++boundsReads;
    for(const auto& e:memory)if(a>=e.first&&std::uint64_t(a)-e.first+n<=e.second.size()){std::memcpy(out,e.second.data()+(a-e.first),n);return 1;}
    return processRead(a,out,n)?1:0;
}
bool rd(unsigned a,void* out,unsigned n){return read(sdk,a,out,n)!=0;}
template<class T>T get(unsigned a){T v{};check(rd(a,&v,sizeof(v)),"fixture memory is readable");return v;}
int32_t CUBE_CALL write(const CubeApi*,uint32_t a,const void* from,uint32_t n){
    writes.emplace_back(a,n);
    if(a==failWrite){failWrite=0;return 0;}
    for(auto& e:memory)if(a>=e.first&&std::uint64_t(a)-e.first+n<=e.second.size()){std::memcpy(e.second.data()+(a-e.first),from,n);return 1;}
    return 0;
}
void __fastcall position(void*,void*,float* out){out[0]=paneX;out[1]=paneY;}
float __fastcall width(void*,void*){return paneWidth;}
float __fastcall height(void*,void*){return paneHeight;}
void popupGeometry(unsigned node,unsigned shape,const std::array<float,4>& area,float dx=480,float dy=300,float scale=1) {
    std::array<float,16> matrix{};matrix[0]=matrix[5]=scale;matrix[10]=matrix[15]=1;matrix[12]=dx;matrix[13]=dy;
    put(node+0x34,shape);put(shape,inventory::popupShapeVtable);put(node+0x48,matrix);
    put(shape+inventory::popupBoundsOffset,std::array<float,4>{(area[0]-dx)/scale,(area[1]-dy)/scale,(area[0]+area[2]-dx)/scale,(area[1]+area[3]-dy)/scale});
}
uint32_t CUBE_CALL rebase(const CubeApi*,uint32_t a){
    if(a==inventory::positionFunction)return reinterpret_cast<unsigned>(&position);
    if(a==inventory::widthFunction)return reinterpret_cast<unsigned>(&width);
    if(a==inventory::heightFunction)return reinterpret_cast<unsigned>(&height);
    return a;
}
int32_t CUBE_CALL readable(const CubeApi* a,uint32_t p,uint32_t n){std::vector<unsigned char>b(n);return read(a,p,b.data(),n);}
void CUBE_CALL log(const CubeApi*,CubeLogLevel,const char* text){std::printf("DLL: %s\n",text);}
inventory::Cell item(unsigned type,unsigned subtype,unsigned id,int count=1){inventory::Cell c{};c.count=count;c.item[0]=static_cast<unsigned char>(type);c.item[1]=static_cast<unsigned char>(subtype);std::memcpy(c.item.data()+4,&id,4);c.item[0x18]=0x7f;c.item[0x117]=0xa5;return c;}
unsigned identity(const inventory::Cell& c){unsigned id=0;std::memcpy(&id,c.item.data()+4,4);return id;}
void __fastcall originalScroll(void* object,void*);
void __fastcall originalDraw(void* object,void*){
    ++draws;
    const unsigned self=reinterpret_cast<unsigned>(object);
    const unsigned origin=get<unsigned>(self+inventory::itemsOffset);
    shadow=origin!=player+inventory::inventoryOffset;
    if(self!=widget||!shadow)return;
    reinterpret_cast<void(__fastcall*)(void*,void*)>(detours[inventory::scrollFunction])(object,nullptr);
    const auto outer=get<inventory::Vector>(origin);
    const int page=get<int>(self+inventory::pageOffset);
    const auto row=get<inventory::Vector>(outer.begin+page*12);
    drawn.resize((row.end-row.begin)/sizeof(inventory::Cell));
    if(!drawn.empty())check(rd(row.begin,drawn.data(),static_cast<unsigned>(drawn.size()*sizeof(inventory::Cell))),"original renderer can read the complete view");
    check(get<int>(origin+0x128)==123456&&get<int>(origin+0x12c)==73,"native money display remains readable and exact");
    put(self+inventory::hoverOffset,inventory::Slot{page,hoverDisplay});
    if(throwDraw)throw 7;
}
void __fastcall originalSelect(void* object,void*,int flag){
    ++selects;const unsigned self=reinterpret_cast<unsigned>(object);
    if(flag){const auto hover=get<inventory::Slot>(self+inventory::hoverOffset);if(hover.index<0)return;put(self+inventory::selectionOffset,hover);}
    selectedDisplay=get<inventory::Slot>(self+inventory::selectionOffset).index;
}
void __fastcall originalScroll(void* object,void*) {
    const unsigned self=reinterpret_cast<unsigned>(object),origin=get<unsigned>(self+inventory::itemsOffset);
    const auto outer=get<inventory::Vector>(origin);const int page=get<int>(self+inventory::pageOffset);
    const auto row=get<inventory::Vector>(outer.begin+page*12);
    const int cols=static_cast<int>((paneWidth-10)/(get<int>(self+0x1a8)+5)),visibleRows=static_cast<int>((paneHeight-40)/(get<int>(self+0x1ac)+5)),n=(row.end-row.begin)/sizeof(inventory::Cell);
    const int maximum=n?std::max(1,((n-1)/cols)-visibleRows+2):0;
    put(self+0x1a4,maximum);const int before=get<int>(0x70000000+page*4);
    if(maximum==0)put(0x70000000+page*4,0);else if(before>=maximum)put(0x70000000+page*4,maximum-1);
}
int32_t CUBE_CALL detour(const CubeApi*,uint32_t a,void* fn,void** trampoline){
    if(partial&&a==inventory::selectFunction)return 0;
    detours[a]=fn;
    *trampoline=a==inventory::drawFunction?reinterpret_cast<void*>(&originalDraw):a==inventory::scrollFunction?reinterpret_cast<void*>(&originalScroll):reinterpret_cast<void*>(&originalSelect);
    return 1;
}
int32_t CUBE_CALL raw(const CubeApi*,uint32_t a,CubeCallConv cc,int32_t n,CubeHookFn fn,void*){
    check((a==inventory::leftFunction||a==inventory::rightFunction)&&cc==CUBE_CC_THISCALL&&n==1,"input guards use the verified ABI");hooks[a]=fn;return 1;
}
int32_t CUBE_CALL remove(const CubeApi*,uint32_t a){++removed;check(detours.count(a)||hooks.count(a),"shutdown only removes owned hooks");return 1;}
uint32_t CUBE_CALL menu(const CubeApi*,CubeOverlayDrawFn fn,void* user,uint32_t key,int32_t open){check(fn&&key==0&&open==1,"category labels register as an always-visible display callback");if(overlayUnavailable)return 0;overlayDraw=fn;overlayUser=user;++menus;return 1;}
int32_t CUBE_CALL unmenu(const CubeApi*,uint32_t handle){check(handle==1,"shutdown removes only its own heading overlay");++unregistered;return 1;}
int32_t CUBE_CALL passthrough(const CubeApi*,uint32_t handle,int32_t value){check(handle==1&&value==1,"category labels never freeze movement or replace native input");return 1;}
void* CUBE_CALL context(const CubeApi*){return ImGui::GetCurrentContext();}
void CUBE_CALL allocators(const CubeApi*,void** alloc,void** free,void** user){ImGuiMemAllocFunc a=nullptr;ImGuiMemFreeFunc f=nullptr;ImGui::GetAllocatorFunctions(&a,&f,user);*alloc=reinterpret_cast<void*>(a);*free=reinterpret_cast<void*>(f);}
void draw(){reinterpret_cast<void(__fastcall*)(void*,void*)>(detours[inventory::drawFunction])(reinterpret_cast<void*>(widget),nullptr);}
void select(int flag){reinterpret_cast<void(__fastcall*)(void*,void*,int)>(detours[inventory::selectFunction])(reinterpret_cast<void*>(widget),nullptr,flag);}
void input(unsigned address){CubeHookCall c{};c.structSize=sizeof(c);c.hook=CUBE_HOOK_RAW;c.address=address;c.self=gc;c.argCount=1;const auto before=c;hooks[address](&c);check(!std::memcmp(&before,&c,sizeof(c)),"native input is never cancelled or given a synthetic transaction");}
template<class T>void nativeStore(unsigned a,const T& value) {
    for(auto& e:memory)if(a>=e.first&&std::uint64_t(a)-e.first+sizeof(value)<=e.second.size()){std::memcpy(e.second.data()+a-e.first,&value,sizeof(value));return;}
    check(false,"native transaction fixture writes only its own known memory");
}
bool leftClick() {
    input(inventory::leftFunction);const auto target=get<inventory::Slot>(widget+inventory::hoverOffset);
    if(target.page<0||target.index<0)return false;
    const auto root=get<inventory::Vector>(player+inventory::inventoryOffset);
    if(static_cast<unsigned>(target.page)>=(root.end-root.begin)/sizeof(inventory::Vector))return false;
    const auto row=get<inventory::Vector>(root.begin+target.page*sizeof(inventory::Vector));
    if(static_cast<unsigned>(target.index)>=(row.end-row.begin)/sizeof(inventory::Cell))return false;
    const unsigned a=row.begin+target.index*sizeof(inventory::Cell),cursor=player+inventory::inventoryOffset+12;
    const auto held=get<inventory::Cell>(cursor),item=get<inventory::Cell>(a);
    if(held.occupied()&&inventory::nativePage(held.item[0])!=get<int>(widget+inventory::pageOffset))return false;
    nativeStore(a,held);nativeStore(cursor,item);return true;
}
void restorePrefix(){std::array<unsigned char,inventory::prefixSize> p{};const inventory::Vector v{rows,rows+36,rows+36};std::memcpy(p.data(),&v,12);int coins=123456,platinum=73;std::memcpy(p.data()+0x128,&coins,4);std::memcpy(p.data()+0x12c,&platinum,4);put(player+inventory::inventoryOffset,p);}
void setup(const std::vector<inventory::Cell>& items){
    const unsigned previous=gc;gc+=0x1000; // each setup is a fresh native UI session
    for(unsigned offset:{0x8008bcu,0x8008e4u,0x8008ecu})if(memory.count(previous+offset))put(gc+offset,get<unsigned>(previous+offset));
    put(inventory::controllerGlobal,gc);put(gc+inventory::playerOffset,player);put(gc+inventory::widgetOffset,widget);put(player,inventory::creatureVtable);
    put(widget+inventory::controllerOffset,gc);put(widget+inventory::itemsOffset,player+inventory::inventoryOffset);put(widget+inventory::modeOffset,0);put(widget+inventory::pageOffset,0);
    put(widget+inventory::hoverOffset,inventory::Slot{});put(widget+inventory::selectionOffset,inventory::Slot{});
    put(widget+0x1a8,75);put(widget+0x1ac,75);put(widget+0x198,0x70000000u);put(0x70000000,std::array<int,3>{0,0,0});
    restorePrefix();array(rows,std::vector<inventory::Vector>{{cells,cells+static_cast<unsigned>(items.size()*sizeof(inventory::Cell)),cells+static_cast<unsigned>(items.size()*sizeof(inventory::Cell))},{0,0,0},{0,0,0}});array(cells,items);
}
}
int wmain(int argc,wchar_t** argv){
    if(argc<2)return 2;
    CubeApi api{};sdk=&api;api.abiVersion=CUBE_ABI_VERSION;api.structSize=sizeof(api);api.environment=CUBE_ENV_CLIENT;
    api.mem={&read,&readable,&rebase,&write};api.log.write=&log;api.hooks.installRawDetour=&detour;api.hooks.onRaw=&raw;api.hooks.removeRaw=&remove;
    api.overlay.registerMenu=&menu;api.overlay.unregisterMenu=&unmenu;api.overlay.setPassthrough=&passthrough;api.overlay.context=&context;api.overlay.allocFuncs=&allocators;
    const unsigned base=reinterpret_cast<unsigned>(GetModuleHandleW(nullptr));IMAGE_DOS_HEADER dos{};dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=0x80;put(base,dos);
    IMAGE_NT_HEADERS32 nt{};nt.Signature=IMAGE_NT_SIGNATURE;nt.FileHeader.Machine=IMAGE_FILE_MACHINE_I386;nt.FileHeader.TimeDateStamp=0x51ea955e;nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC;nt.OptionalHeader.SizeOfImage=0x3bc000;nt.OptionalHeader.AddressOfEntryPoint=0x28e1e0;put(base+0x80,nt);
    for(unsigned a:{inventory::drawFunction,inventory::leftFunction,inventory::rightFunction})put(a,std::array<unsigned char,6>{0x55,0x8b,0xec,0x6a,0xff,0x68});
    put(0x4c2090,std::array<unsigned char,7>{0x83,0xbb,0x60,1,0,0,0});put(0x4c20ab,std::array<unsigned char,10>{0xc7,0x83,0x84,1,0,0,0xff,0xff,0xff,0xff});put(0x4c597a,std::uint8_t{0xc3});
    put(inventory::selectFunction,std::array<unsigned char,6>{0x55,0x8b,0xec,0x56,0x8b,0xf1});put(0x4c679c,std::array<unsigned char,5>{0x5e,0x5d,0xc2,4,0});
    put(inventory::scrollFunction,std::array<unsigned char,9>{0x55,0x8b,0xec,0x83,0xec,0x0c,0x56,0x8b,0xf1});
    put(0x47b611,std::array<unsigned char,6>{0x81,0xec,0xd0,3,0,0});put(0x47dde1,std::array<unsigned char,3>{0x83,0xec,0x3c});
    put(rebase(&api,inventory::positionFunction),std::array<unsigned char,6>{0x55,0x8b,0xec,0x83,0xec,0x44});
    for(unsigned a:{inventory::widthFunction,inventory::heightFunction})put(rebase(&api,a),std::array<unsigned char,6>{0x55,0x8b,0xec,0x83,0xec,0x0c});
    std::vector<inventory::Cell> source={item(5,0,501),{},item(4,0,401),item(3,2,302),item(4,0,402),item(3,1,301),{},item(2,0,201),item(11,2,1102,9)};
    setup(source);const auto originals=memory;
    const std::wstring mode=argc>2?argv[2]:L"";
    partial=mode==L"partial";overlayUnavailable=mode==L"overlay";
    if(mode==L"unsupported")put(inventory::drawFunction,std::array<unsigned char,6>{0xe9,0,0,0,0,0});
    HMODULE module=LoadLibraryW(argv[1]);check(module!=nullptr,"actual independent DLL loads into fixture only");
    auto init=reinterpret_cast<CubeModInitFn>(GetProcAddress(module,"CubeMod_Init"));auto shutdown=reinterpret_cast<CubeModShutdownFn>(GetProcAddress(module,"CubeMod_Shutdown"));check(init&&shutdown,"actual DLL exports are complete");
    const auto info=init(&api);check(info&&std::string(info->id)=="local.cubeworld.inventory_sort"&&!info->deps,"manifest retains unique ID and no other Mod dependency");
    check(std::string(info->version)=="0.1.7","existing module is upgraded without a duplicate entry");
    check(info->capabilities==(CUBE_CAP_RAW_MEM|CUBE_CAP_RAW_HOOKS|CUBE_CAP_WRITES|CUBE_CAP_OVERLAY),"manifest declares native UI and label overlay capabilities");
    if(mode==L"unsupported"){check(detours.empty()&&hooks.empty()&&writes.empty()&&!menus,"unknown build or existing detour is refused before writes/hooks");shutdown();std::printf("PASS %u unsupported checks\n",checks);return 0;}
    if(partial){check(removed==1&&hooks.empty()&&writes.empty()&&!menus,"partial hook installation rolls back its earlier detour");shutdown();check(removed==1,"rollback cleanup is idempotent");std::printf("PASS %u partial checks\n",checks);return 0;}
    if(overlayUnavailable){check(removed==5&&writes.empty()&&!menus,"missing category labels rolls back every hook instead of showing blank separator rows");shutdown();check(removed==5&&!unregistered,"failed overlay cleanup is idempotent");std::printf("PASS %u overlay-unavailable checks\n",checks);return 0;}
    check(detours.size()==3&&hooks.size()==2&&menus==1,"native handlers and one independent label overlay are registered");
    hoverDisplay=40;draw();check(shadow,"native renderer consumes the DLL-owned section view");
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.DisplaySize={1280,800};io.DeltaTime=1.0f/60;io.IniFilename=nullptr;io.LogFilename=nullptr;
    unsigned char* atlas=nullptr;int aw=0,ah=0;io.Fonts->GetTexDataAsRGBA32(&atlas,&aw,&ah);
    put(gc+0x8008bc,0x11000000u);put(0x1100003c,0x12000000u);put(0x12000068,0);put(0x12000094,0x13000000u);put(0x13000000,1u);
    put(gc+inventory::popupNodeOffsets[0],popupNode);put(gc+inventory::popupNodeOffsets[1],comparisonNode);
    put(popupNode+0x3c,popupNode+0x1000);put(popupNode+0x1068,0);put(popupNode+0x1094,popupVisible);put(popupVisible,0u);
    put(comparisonNode+0x3c,comparisonNode+0x1000);put(comparisonNode+0x1068,0);put(comparisonNode+0x1094,comparisonVisible);put(comparisonVisible,0u);
    popupGeometry(popupNode,popupShape,popupArea);popupGeometry(comparisonNode,comparisonShape,comparisonArea,100,200);
    ImGui::NewFrame();overlayDraw(overlayUser);ImGui::Render();check(ImGui::GetDrawData()->TotalVtxCount>0,"actual DLL category overlay draws only after the native view has a valid visible frame");
    check(boundsReads==0,"hidden native popups require no geometry reads or game function calls");
    const auto overlay=[&](){ImGui::NewFrame();overlayDraw(overlayUser);ImGui::Render();return ImGui::GetDrawData()->TotalVtxCount;};
    const auto avoids=[&](const std::array<float,4>& r){
        const auto* data=ImGui::GetDrawData();
        for(int i=0;i<data->CmdListsCount;++i)for(const auto& command:data->CmdLists[i]->CmdBuffer)if(command.ElemCount) {
            const auto clip=command.ClipRect;
            if(clip.x<clip.z&&clip.y<clip.w&&clip.x<r[0]+r[2]+3&&r[0]-3<clip.z&&clip.y<r[1]+r[3]+3&&r[1]-3<clip.w)return false;
        }
        return true;
    };
    put(popupVisible,1u);check(overlay()>0&&avoids(popupArea),"every actual overlay draw command avoids the native item detail and its border while other headings remain visible");
    put(comparisonVisible,1u);check(overlay()>0&&avoids(popupArea)&&avoids(comparisonArea),"item detail and equipped comparison can overlap different headings without either being overpainted");
    popupArea={25,55,650,570};popupGeometry(popupNode,popupShape,popupArea);put(comparisonVisible,0u);check(overlay()==0,"a popup covering the entire bag suppresses every heading pixel without moving item cells");
    popupArea={400,240,360,370};popupGeometry(popupNode,popupShape,popupArea,250,100,1.5f);check(overlay()>0&&avoids(popupArea),"scaled and translated native popup bounds avoid their actual screen rectangle");
    failRead=popupShape+inventory::popupBoundsOffset;check(overlay()==0,"unreadable popup geometry hides headings safely for that frame");failRead=0;
    put(popupShape,0u);check(overlay()==0,"unknown native shape refuses guessed field interpretation");popupGeometry(popupNode,popupShape,popupArea);
    std::array<float,16> invalidMatrix{};put(popupNode+0x48,invalidMatrix);check(overlay()==0,"invalid homogeneous transform cannot place a mask over unrelated screen coordinates");popupGeometry(popupNode,popupShape,popupArea);
    // Captured in the real 0.1.3 failure: local bounds and screen bounds are far apart.
    // Place that recorded popup over visible headings by moving the fixture bag only.
    put(popupShape+inventory::popupBoundsOffset,std::array<float,4>{45.1754074f,-119.824219f,353.175873f,188.175797f});
    std::array<float,16> capturedMatrix{};capturedMatrix[0]=capturedMatrix[5]=capturedMatrix[10]=capturedMatrix[15]=1;capturedMatrix[12]=793.824707f;capturedMatrix[13]=471.824280f;
    put(popupNode+0x48,capturedMatrix);
    const std::array<float,4> capturedScreen{839,352,308.001f,308.001f};
    paneX=531;paneY=136;draw();
    auto untranslated=capturedMatrix;untranslated[12]=untranslated[13]=0;put(popupNode+0x48,untranslated);
    check(overlay()>0&&!avoids(capturedScreen),"captured local-only coordinates reproduce the original overpaint instead of vacuously passing outside the bag");
    put(popupNode+0x48,capturedMatrix);
    check(overlay()>0&&avoids(capturedScreen),"actual recorded popup local bounds are transformed before clipping, not used as screen coordinates");
    paneX=20;paneY=20;draw();popupGeometry(popupNode,popupShape,popupArea);
    put(popupVisible,0u);check(overlay()>0,"moving off the item restores normal category headings on the next overlay frame");
    put(0x13000000,0u);ImGui::NewFrame();overlayDraw(overlayUser);ImGui::Render();check(ImGui::GetDrawData()->TotalVtxCount==0,"closing the native bag immediately hides every category label");put(0x13000000,1u);
    check(drawn.size()==80,"five populated categories use aligned title and content rows without old empty capacity");
    check(identity(drawn[8])==401&&identity(drawn[9])==402&&identity(drawn[24])==501&&identity(drawn[40])==302&&identity(drawn[41])==301&&identity(drawn[56])==201&&identity(drawn[72])==1102,"chest, gloves, weapons, formulas and materials occupy separate stable sections");
    check(get<unsigned>(widget+inventory::itemsOffset)==player+inventory::inventoryOffset,"actual inventory pointer is restored after drawing");
    check(get<inventory::Slot>(widget+inventory::hoverOffset).index==3,"weapon display row maps to the actual native slot");
    check(memory.at(cells)==originals.at(cells)&&memory.at(rows)==originals.at(rows)&&memory.at(player+inventory::inventoryOffset)==originals.at(player+inventory::inventoryOffset),"every original item byte, stack, header and coin value remains unchanged");
    input(inventory::rightFunction);select(1);
    check(selectedDisplay==40&&get<inventory::Slot>(widget+inventory::selectionOffset).index==3,"highlight follows its section while selected item identity stays native");
    select(0);check(selectedDisplay==40,"subsequent highlight keeps the same visual position");
    for(int decoration:{0,7,10,15,16,23,25,31,32,39,42,47}){hoverDisplay=decoration;draw();input(inventory::rightFunction);check(get<inventory::Slot>(widget+inventory::hoverOffset).index==-1,"titles and row alignment padding can never equip, sell or receive an item");}
    hoverDisplay=40;draw();source[3]={};array(cells,source);
    reinterpret_cast<void(__fastcall*)(void*,void*)>(detours[inventory::scrollFunction])(reinterpret_cast<void*>(widget),nullptr);
    check(get<int>(widget+0x1a4)==4&&get<unsigned>(widget+inventory::itemsOffset)==player+inventory::inventoryOffset,"native transaction scrolling uses grouped row count and always restores the real inventory pointer");
    input(inventory::leftFunction);
    check(get<inventory::Slot>(widget+inventory::hoverOffset).index==-1&&get<inventory::Slot>(widget+inventory::selectionOffset).index==-1,"inventory changes block stale click locations before another action");
    draw();check(!drawn[40].occupied()&&identity(drawn[41])==301&&get<inventory::Slot>(widget+inventory::hoverOffset).index==-1,"selling leaves its slot empty and never pulls the following weapon under the pointer");
    source[5]={};array(cells,source);input(inventory::rightFunction);draw();check(drawn.size()==80&&!drawn[40].occupied()&&!drawn[41].occupied()&&identity(drawn[56])==201,"an emptied category retains its area so later categories stay in their previous positions");
    source[1]=item(4,0,403);array(cells,source);draw();check(identity(drawn[8])==403&&identity(drawn[9])==401&&identity(drawn[10])==402,"new chest pickup joins the chest section with stable peer order");
    auto& prefix=memory[player+inventory::inventoryOffset];auto held=item(3,0,3000);std::memcpy(prefix.data()+12,&held,sizeof(held));draw();
    check(drawn.size()==80,"held item uses existing category vacancies without appending a distant return-only section");
    hoverDisplay=40;draw();check(get<inventory::Slot>(widget+inventory::hoverOffset).index==3,"vacated weapon position maps to its actual free native slot");
    hoverDisplay=11;draw();select(1);check(get<inventory::Slot>(widget+inventory::hoverOffset).index==3&&get<inventory::Slot>(widget+inventory::selectionOffset).index==3&&selectedDisplay==11,"visible alignment cells accept a held item and selection stays under the actual mouse position");
    held=item(4,0,4000);std::memcpy(prefix.data()+12,&held,sizeof(held));input(inventory::leftFunction);check(get<inventory::Slot>(widget+inventory::hoverOffset).index==-1,"changing the held item at the same stack count invalidates stale return mappings");
    held={};std::memcpy(prefix.data()+12,&held,sizeof(held));input(inventory::leftFunction);check(get<inventory::Slot>(widget+inventory::hoverOffset).index==-1,"cursor changes invalidate a stale drop target");
    hoverDisplay=8;draw();throwDraw=true;try{draw();check(false,"native exception propagates");}catch(int value){check(value==7,"native exception propagates after cleanup");}throwDraw=false;
    check(get<unsigned>(widget+inventory::itemsOffset)==player+inventory::inventoryOffset,"native exceptions always restore the actual inventory pointer");
    put(other+inventory::itemsOffset,player+inventory::inventoryOffset);const auto beforeOther=writes.size();reinterpret_cast<void(__fastcall*)(void*,void*)>(detours[inventory::drawFunction])(reinterpret_cast<void*>(other),nullptr);check(writes.size()==beforeOther,"merchant and recipe widgets are pure pass-through");
    draw();input(inventory::rightFunction);check(get<inventory::Slot>(widget+inventory::hoverOffset).index==1,"other widget rendering does not invalidate the player section map");
    failWrite=widget+inventory::itemsOffset;draw();check(!shadow&&get<unsigned>(widget+inventory::itemsOffset)==player+inventory::inventoryOffset,"failed UI-pointer change preserves native drawing");
    paneWidth=0;draw();check(!shadow,"unavailable layout preserves native UI instead of inventing columns");paneWidth=650;draw();
    put(widget+inventory::pageOffset,1);input(inventory::leftFunction);check(get<inventory::Slot>(widget+inventory::hoverOffset).index==-1,"tab switch invalidates the old hover");draw();check(drawn.empty(),"empty page has no fake item cells or empty category blocks");
    put(widget+inventory::pageOffset,0);draw();put(gc+inventory::playerOffset,player+0x10000);input(inventory::rightFunction);check(get<inventory::Slot>(widget+inventory::hoverOffset).index==-1,"character changes clear stale native indices");put(gc+inventory::playerOffset,player);
    for(auto malformed:std::vector<inventory::Vector>{{cells,cells-4,cells},{cells,cells+1,cells+1},{cells,cells,cells+257*sizeof(inventory::Cell)}}){put(rows,malformed);draw();check(!shadow,"invalid inventory bounds preserve native display");}
    for(unsigned n:{0u,1u,50u,128u,256u}) {
        std::vector<inventory::Cell> bag(n);const std::array<unsigned,9> types{3,4,5,6,7,8,9,2,11};
        for(unsigned i=0;i<n;++i)if(i%7)bag[i]=item(types[(i*13)%types.size()],i%3,9000+i,static_cast<int>(i%5+1));
        setup(bag);inventory::View probe;check(probe.prepare(&rd,gc,widget,inventory::creatureVtable,8),"bounded real-size bag can be sectioned");
        bool lossless=true,bijection=true;unsigned occupied=0,mapped=0;
        for(unsigned i=0;i<n;++i){const auto display=probe.display({0,static_cast<int>(i)});if(bag[i].occupied()){++occupied;bijection&=display.index>=0&&probe.native(display).index==static_cast<int>(i);if(display.index>=0)lossless&=!std::memcmp(&probe.cells[display.index],&bag[i],sizeof(inventory::Cell));}else bijection&=display.index==-1;}
        for(unsigned i=0;i<probe.cells.size();++i)if(probe.native({0,static_cast<int>(i)}).index>=0)++mapped;
        check(lossless&&bijection&&mapped==occupied,"all real items survive exactly once; synthetic spacing cannot become an inventory slot");
    }
    std::vector<inventory::Cell> adaptiveBag;inventory::View adaptive;
    for(unsigned n:{8u,9u,10u,11u}) {
        adaptiveBag.clear();for(unsigned i=0;i<n;++i)adaptiveBag.push_back(item(4,0,4000+i));adaptiveBag.push_back(item(5,0,5000));setup(adaptiveBag);
        const unsigned bodyRows=(n+7)/8;draw();
        check(adaptive.prepare(&rd,gc,widget,inventory::creatureVtable,8)&&adaptive.sections[0].count==static_cast<int>(n)&&adaptive.sections[0].bodyRows==static_cast<int>(bodyRows)&&adaptive.sections[1].header==static_cast<int>((bodyRows+1)*8),"category grows only when the original row capacity is exceeded; ten and eleven items both use two native eight-column rows");
        check(drawn.size()==(bodyRows+3)*8&&identity(drawn[8])==4000&&identity(drawn[(bodyRows+2)*8])==5000,"actual DLL derives row wrapping from the original native dimensions");
    }
    paneWidth=490;draw();check(drawn.size()==30&&identity(drawn[6])==4000&&identity(drawn[24])==5000,"changing native pane width immediately preserves its six-column layout without changing cell size");paneWidth=650;
    // Exact 104-cell equipment page captured from the failing native session.
    wchar_t executable[MAX_PATH]{};check(GetModuleFileNameW(nullptr,executable,MAX_PATH)!=0,"fixture executable path is available");
    const auto capture=std::filesystem::path(executable).parent_path().parent_path().parent_path()/L"research/evidence/inventory-equipment-0.1.4-cells.bin";
    std::vector<inventory::Cell> recorded(104);
    std::ifstream file(capture,std::ios::binary);file.read(reinterpret_cast<char*>(recorded.data()),recorded.size()*sizeof(inventory::Cell));
    check(file.gcount()==static_cast<std::streamsize>(recorded.size()*sizeof(inventory::Cell))&&file.peek()==std::char_traits<char>::eof(),"exact recorded equipment cells load without truncation or extra data");
    setup(recorded);paneWidth=400;paneHeight=285;put(widget+0x1a8,40);put(widget+0x1ac,40);
    constexpr unsigned splitCells=0x51000aa8;const unsigned recordedBytes=static_cast<unsigned>(recorded.size()*sizeof(inventory::Cell));
    array(splitCells,recorded);array(rows,std::vector<inventory::Vector>{{splitCells,splitCells+recordedBytes,splitCells+recordedBytes},{0,0,0},{0,0,0}});
    splitBoundary=0x51007000;std::vector<inventory::Cell> rejected(recorded.size());
    check(!rd(splitCells,rejected.data(),recordedBytes),"loader single-region policy reproduces the original whole-page read failure at the captured offset");
    hoverDisplay=8;draw();check(shadow&&drawn.size()==192&&overlay()>0,"actual DLL restores seven equipment categories with the recorded native eight-column geometry across readable regions");
    std::vector<int> expectedNative(drawn.size(),-1);unsigned display=0;
    for(unsigned type:{4u,5u,6u,7u,3u,8u,9u}) {
        for(unsigned i=0;i<8;++i)check(!drawn[display+i].occupied(),"recorded equipment category retains one noninteractive heading row");
        display+=8;
        for(unsigned i=0;i<recorded.size();++i)if(recorded[i].item[0]==type) {
            check(display<drawn.size()&&!std::memcmp(&drawn[display],&recorded[i],sizeof(inventory::Cell)),"recorded equipment appears once in its category with complete bytes and native relative order preserved");
            expectedNative[display++]=static_cast<int>(i);
        }
        display=(display+7)/8*8;
    }
    check(display==drawn.size(),"all seven captured categories use only native row wrapping");
    check(get<inventory::Slot>(widget+inventory::hoverOffset).index==expectedNative[8],"hovered first chest armor maps back to the captured real item");
    hoverDisplay=static_cast<int>(std::find(expectedNative.begin(),expectedNative.end(),103)-expectedNative.begin());
    check(hoverDisplay<static_cast<int>(drawn.size()),"captured final native item has one display location");
    draw();select(1);input(inventory::rightFunction);
    check(get<inventory::Slot>(widget+inventory::hoverOffset).index==103&&get<inventory::Slot>(widget+inventory::selectionOffset).index==103&&selectedDisplay==hoverDisplay,"item past the region boundary keeps correct hover, selection and guarded native input mapping");
    auto changed=recorded;changed.back().count++;array(splitCells,changed);input(inventory::rightFunction);
    check(get<inventory::Slot>(widget+inventory::hoverOffset).index==-1,"stale-item detection still catches changes beyond the region boundary");array(splitCells,recorded);
    failRead=splitBoundary;draw();check(!shadow&&overlay()==0,"unreadable tail still preserves native display and hides stale category headings");failRead=0;
    draw();check(shadow&&get<inventory::Slot>(widget+inventory::hoverOffset).index==103,"readable tail restores categories and their native item mapping");
    std::vector<inventory::Cell> unchanged(recorded.size());splitBoundary=0;check(rd(splitCells,unchanged.data(),recordedBytes)&&!std::memcmp(unchanged.data(),recorded.data(),recordedBytes),"equipment records remain byte-identical after drawing, selection and read failure");
    const auto loadCapture=[&](const wchar_t* name,unsigned count) {
        std::vector<inventory::Cell> result(count);std::ifstream input(capture.parent_path()/name,std::ios::binary);
        const auto bytes=static_cast<std::streamsize>(count*sizeof(inventory::Cell));input.read(reinterpret_cast<char*>(result.data()),bytes);
        check(input.gcount()==bytes&&input.peek()==std::char_traits<char>::eof(),"bounded native return capture loads exactly");return result;
    };
    const auto beforeReturn=loadCapture(L"inventory-return-0.1.5-before-cells.bin",105);
    const auto heldReturn=loadCapture(L"inventory-return-0.1.5-held-cells.bin",105);
    const auto cursorReturn=loadCapture(L"inventory-return-0.1.5-cursor.bin",1)[0];
    setup(beforeReturn);paneWidth=400;paneHeight=285;put(widget+0x1a8,40);put(widget+0x1ac,40);draw();
    const auto beforePositions=drawn;const auto nativePick=beforeReturn[57];
    const auto picked=std::find_if(drawn.begin(),drawn.end(),[&](const auto& c){return !std::memcmp(&c,&nativePick,sizeof(c));});
    check(picked!=drawn.end(),"the actually picked native weapon has a display location");hoverDisplay=static_cast<int>(picked-drawn.begin());
    draw();check(leftClick(),"native-style pickup uses the actual DLL's restored real hover location");draw();
    check(drawn.size()==beforePositions.size()&&!drawn[hoverDisplay].occupied()&&get<inventory::Slot>(widget+inventory::hoverOffset).index==57,"pickup keeps the vacated position visible without moving the other equipment or categories");
    check(leftClick(),"native-style return can use the original visible vacancy");draw();
    check(get<inventory::Cell>(player+inventory::inventoryOffset+12).count==0&&memory.at(cells).size()==beforeReturn.size()*sizeof(inventory::Cell)&&!std::memcmp(memory.at(cells).data(),beforeReturn.data(),memory.at(cells).size()),"pickup and return restore every captured item byte exactly without adding or losing an item");
    array(cells,heldReturn);nativeStore(player+inventory::inventoryOffset+12,cursorReturn);draw();
    check(drawn.size()==beforePositions.size()&&get<inventory::Slot>(widget+inventory::hoverOffset).index==57,"actual captured held state preserves the same category vacancy");
    hoverDisplay=15;draw();select(1);
    check(get<inventory::Slot>(widget+inventory::hoverOffset).index==57&&selectedDisplay==15,"an on-screen empty alignment cell returns to the correct real vacancy without jumping the highlight elsewhere");
    check(leftClick()&&get<inventory::Cell>(player+inventory::inventoryOffset+12).count==0&&get<inventory::Cell>(cells+57*sizeof(inventory::Cell)).item==cursorReturn.item,"the actual captured cursor item returns through a visible empty cell to its real inventory slot");draw();
    const auto salePositions=drawn;const auto front=drawn[8];unsigned sold=0;
    while(sold<beforeReturn.size()&&std::memcmp(&front,&beforeReturn[sold],sizeof(front)))++sold;
    check(sold<beforeReturn.size(),"first displayed chest armor has a captured native identity");nativeStore(cells+sold*sizeof(inventory::Cell),inventory::Cell{});
    reinterpret_cast<void(__fastcall*)(void*,void*)>(detours[inventory::scrollFunction])(reinterpret_cast<void*>(widget),nullptr);
    hoverDisplay=8;draw();check(drawn.size()==salePositions.size()&&!drawn[8].occupied()&&get<inventory::Slot>(widget+inventory::hoverOffset).index==-1,"selling the first armor keeps its vacancy and cannot select the next item under the same pointer");
    for(unsigned i=0;i<drawn.size();++i)if(i!=8)check(!std::memcmp(&drawn[i],&salePositions[i],sizeof(inventory::Cell)),"every other captured equipment item and category remains at its previous display position after sale");
    const auto afterSale=drawn;put(widget+inventory::pageOffset,1);draw();put(widget+inventory::pageOffset,0);draw();
    check(drawn.size()==afterSale.size()&&!std::memcmp(drawn.data(),afterSale.data(),drawn.size()*sizeof(inventory::Cell)),"switching away and back keeps sold vacancies and all other positions in the same session");
    // An equipment-slot removal has no previous inventory cell. It still gets a
    // return target within its category, including when the native page is empty.
    setup(std::vector<inventory::Cell>(3));put(widget+0x1a8,40);put(widget+0x1ac,40);nativeStore(player+inventory::inventoryOffset+12,item(4,0,4400));hoverDisplay=8;draw();
    check(drawn.size()==16&&get<inventory::Slot>(widget+inventory::hoverOffset).index==0&&leftClick(),"equipment removed from a worn slot can return to an otherwise empty native inventory page");draw();
    check(get<inventory::Cell>(player+inventory::inventoryOffset+12).count==0&&identity(get<inventory::Cell>(cells))==4400,"returning worn equipment consumes the held item once and preserves its identity");
    nativeStore(player+inventory::inventoryOffset+12,item(1,0,1100,3));hoverDisplay=9;draw();
    check(get<inventory::Slot>(widget+inventory::hoverOffset).index==-1&&!leftClick(),"held items cannot be placed into the wrong native tab");
    nativeStore(player+inventory::inventoryOffset+12,inventory::Cell{});
    paneWidth=650;paneHeight=620;
    setup(source);hoverDisplay=8;draw();
    for(const auto& w:writes)check((w.first==widget+inventory::itemsOffset&&w.second==4)||(w.first==widget+inventory::hoverOffset&&w.second==8)||(w.first==widget+inventory::selectionOffset&&w.second==8),"every Mod write remains limited to its three native UI fields");
    shutdown();check(removed==5&&unregistered==1,"shutdown removes owned hooks and category labels");
    const auto stoppedWrites=writes.size();draw();check(!shadow&&writes.size()==stoppedWrites,"disabled module passes original native inventory through");
    ImGui::DestroyContext();
    std::printf("PASS %u actual-DLL section checks; renderer/input are fixtures, no real game transaction executed\n",checks);
}

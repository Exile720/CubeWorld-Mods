#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "cube_sdk.h"
#include "explore.hpp"
#include "nav_bridge.hpp"
#include "imgui.h"
#include <map>
#include <cstdio>
#include <cstdlib>

// Actual DLL callbacks, synthetic SDK and memory, no game process or input simulation.
namespace {
struct Module {
    CubeApi api{};HMODULE dll=nullptr;const CubeModInfo* info=nullptr;
    CubeOverlayDrawFn draw=nullptr;void* drawUser=nullptr;CubeMessageFn message=nullptr;void* messageUser=nullptr;
    std::string scope;std::map<std::string,std::string> files;int pass=0;unsigned toggle=999;bool visible=false;
    bool failSave=false,failRemove=false;unsigned puts=0;
};
Module navigation,explorer;unsigned checks=0,writes=0,hooks=0;bool inWorld=true;
constexpr unsigned gc=0x01000000,player=0x03000000,chunk=0x05000000;
const nav::Point position{8391168,8391168,80};
std::map<unsigned,std::vector<unsigned char>> memory;std::vector<std::string> logs;
unsigned messageAddress=0,messageSize=0;
void check(bool value,const char* text){++checks;if(!value){std::printf("FAIL: %s\n",text);std::exit(1);}}
Module& owner(const CubeApi* api){return api==&navigation.api?navigation:explorer;}
template<class T>void put(unsigned address,const T& value){auto& bytes=memory[address];bytes.resize(sizeof(value));std::memcpy(bytes.data(),&value,sizeof(value));}
int32_t CUBE_CALL read(const CubeApi*,uint32_t address,void* out,uint32_t size){
    for(const auto& entry:memory)if(address>=entry.first&&std::uint64_t(address)-entry.first+size<=entry.second.size()){
        std::memcpy(out,entry.second.data()+(address-entry.first),size);return 1;
    }
    if(messageAddress&&address>=messageAddress&&std::uint64_t(address)-messageAddress+size<=messageSize){std::memcpy(out,reinterpret_cast<void*>(address),size);return 1;}
    return 0;
}
int32_t CUBE_CALL readable(const CubeApi* api,uint32_t address,uint32_t size){std::vector<unsigned char> bytes(size);return read(api,address,bytes.data(),size);}
uint32_t CUBE_CALL rebase(const CubeApi*,uint32_t address){return address;}
int32_t CUBE_CALL write(const CubeApi*,uint32_t,const void*,uint32_t){++writes;return 0;}
int32_t CUBE_CALL rawHook(const CubeApi*,uint32_t,CubeCallConv,int32_t,CubeHookFn,void*){++hooks;return 0;}
void CUBE_CALL log(const CubeApi*,CubeLogLevel,const char* message){logs.emplace_back(message);}
int32_t CUBE_CALL getPlayer(const CubeApi*,CubePlayer* out){*out={};out->address=player;out->hasPosition=1;return 1;}
int32_t CUBE_CALL getWorld(const CubeApi*,CubeWorld* out){*out={};out->hasSeed=1;out->seed=123;return 1;}
int32_t CUBE_CALL getSession(const CubeApi*,CubeSession* out){*out={};out->gameState=inWorld?CUBE_STATE_IN_WORLD:CUBE_STATE_TITLE;return 1;}
int32_t CUBE_CALL setScope(const CubeApi* api,const char* scope){owner(api).scope=scope;return 1;}
int32_t CUBE_CALL has(const CubeApi* api,const char* key){auto& m=owner(api);return m.files.count(m.scope+'/'+key)?1:0;}
int32_t CUBE_CALL get(const CubeApi* api,const char* key,void* out,int32_t size){auto& m=owner(api);const auto found=m.files.find(m.scope+'/'+key);if(found==m.files.end())return 0;if(!out)return static_cast<int>(found->second.size());const auto count=std::min(size,static_cast<int>(found->second.size()));std::memcpy(out,found->second.data(),count);return count;}
int32_t CUBE_CALL save(const CubeApi* api,const char* key,const void* data,int32_t size){auto& m=owner(api);if(m.failSave)return 0;++m.puts;m.files[m.scope+'/'+key]=std::string(static_cast<const char*>(data),size);return 1;}
int32_t CUBE_CALL remove(const CubeApi* api,const char* key){auto& m=owner(api);return !m.failRemove&&m.files.erase(m.scope+'/'+key)?1:0;}
uint32_t CUBE_CALL onMessage(const CubeApi* api,CubeMessageFn fn,void* user){auto& m=owner(api);m.message=fn;m.messageUser=user;return 1;}
int32_t CUBE_CALL send(const CubeApi* api,const char* target,uint32_t id,void* payload,uint32_t size){
    Module* destination=std::strcmp(target,navbridge::provider)==0?&navigation:std::strcmp(target,navbridge::consumer)==0?&explorer:nullptr;
    if(!destination||!destination->message||!destination->info)return -1;
    CubeMessageArgs args{};args.senderId=owner(api).info->id;args.msgId=id;args.payload=payload;args.payloadSize=size;args.result=-1;
    messageAddress=reinterpret_cast<unsigned>(payload);messageSize=size;
    destination->message(&destination->api,&args,destination->messageUser);messageAddress=messageSize=0;return args.result;
}
uint32_t CUBE_CALL menu(const CubeApi* api,CubeOverlayDrawFn fn,void* user,uint32_t key,int32_t visible){auto& m=owner(api);m.draw=fn;m.drawUser=user;m.toggle=key;m.visible=visible!=0;return 1;}
int32_t CUBE_CALL pass(const CubeApi* api,uint32_t,int32_t value){owner(api).pass=value;return 1;}
void* CUBE_CALL context(const CubeApi*){return ImGui::GetCurrentContext();}
void CUBE_CALL alloc(const CubeApi*,void** a,void** f,void** u){ImGuiMemAllocFunc af;ImGuiMemFreeFunc ff;ImGui::GetAllocatorFunctions(&af,&ff,u);*a=reinterpret_cast<void*>(af);*f=reinterpret_cast<void*>(ff);}
void initialize(Module& m,const wchar_t* path,const char* id){
    m.api.abiVersion=CUBE_ABI_VERSION;m.api.structSize=sizeof(CubeApi);m.api.environment=CUBE_ENV_CLIENT;
    m.api.mem={&read,&readable,&rebase,&write};m.api.log.write=&log;m.api.hooks.onRaw=&rawHook;
    m.api.player.get=&getPlayer;m.api.world.get=&getWorld;m.api.session.get=&getSession;
    m.api.storage={&setScope,&get,&save,&remove,&has};m.api.services.onMessage=&onMessage;m.api.services.sendMessage=&send;
    m.api.overlay.registerMenu=&menu;m.api.overlay.setPassthrough=&pass;m.api.overlay.context=&context;m.api.overlay.allocFuncs=&alloc;
    m.dll=LoadLibraryW(path);check(m.dll!=nullptr,"test loads actual module into fixture process");
    auto init=reinterpret_cast<CubeModInitFn>(GetProcAddress(m.dll,"CubeMod_Init"));check(init!=nullptr,"actual module exports initializer");
    m.info=init(&m.api);check(m.info&&std::strcmp(m.info->id,id)==0,"actual manifest has the registered unique ID");
    check(m.info->capabilities==(CUBE_CAP_OVERLAY|CUBE_CAP_RAW_MEM),"actual module has overlay/read capability only");
    check(!m.info->deps,"no hard dependency on another Mod");
    check(m.draw&&m.message&&m.toggle==0&&m.pass==1&&m.visible,"actual SDK registers passive always-on overlay and optional messages");
}
void frame(){ImGui::NewFrame();if(navigation.draw)navigation.draw(navigation.drawUser);if(explorer.draw)explorer.draw(explorer.drawUser);ImGui::Render();}
}
int wmain(int argc,wchar_t** argv){
    if(argc<3||argc>4)return 2;static_assert(sizeof(void*)==4,"Win32 required");
    const bool fontFallback=argc==4&&std::wcscmp(argv[3],L"font-fallback")==0;
    put(0x76b1c8,gc);put(player+0x10,std::array<std::int64_t,3>{std::int64_t(position.x*65536),std::int64_t(position.y*65536),std::int64_t(position.z*65536)});
    put(player+0x190,13);
    std::array<unsigned char,32> world{};std::memcpy(world.data(),"fixture",7);unsigned length=7,capacity=15;std::memcpy(world.data()+16,&length,4);std::memcpy(world.data()+20,&capacity,4);put(gc+0x2e4+0x94,world);
    put(gc+0x8006e4,std::uint8_t{1});put(gc+0x1ac,0.0f);put(gc+0x11c,1280);put(gc+0x120,800);put(gc+0x1000e4c,std::array<float,3>{0,0,0});
    const std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};put(gc+0x800d44+4,identity);put(gc+0x800d44+0x44,identity);
    put(gc+0x800d44+0xb0+(512*1024+512)*4,chunk);
    std::array<unsigned char,64*explore::recordSize> records{};
    for(unsigned i=9;i<=10;++i){auto* p=records.data()+i*explore::recordSize;const std::int64_t x=std::int64_t(position.x*65536),y=std::int64_t((position.y+(i-9)*2048)*65536);const unsigned cat=i==9?1:3,mission=i==9?0:52;std::memcpy(p,&x,8);std::memcpy(p+8,&y,8);std::memcpy(p+0x18,&cat,4);std::memcpy(p+0x34,&mission,4);}
    {auto* p=records.data()+11*explore::recordSize;const std::int64_t x=std::int64_t(position.x*65536),y=std::int64_t((position.y+4096)*65536);const unsigned cat=14,subtype=5;std::memcpy(p,&x,8);std::memcpy(p+8,&y,8);std::memcpy(p+0x18,&cat,4);std::memcpy(p+0x1c,&subtype,4);p[0x41]=2;}
    put(chunk+explore::chunkPlaces,records);
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={1280,800};io.DeltaTime=1.f/60;io.Fonts->AddFontDefault();
    if(fontFallback)io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;
    else {unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);}
    auto* baseFont=io.Fonts->Fonts[0];auto* baseData=baseFont->Sources[0]->FontData;
    if(argc==4&&!fontFallback) {
        const bool soloExplorer=std::wcscmp(argv[3],L"explorer-only")==0;
        check(soloExplorer||std::wcscmp(argv[3],L"navigation-only")==0,"recognized standalone fixture mode");
        if(soloExplorer) {
            const auto scope=nav::worldScope(123,"fixture");
            explorer.files[scope+"/target"]=explore::encodeTarget({position.x+500,position.y+200,80},"test town");
            initialize(explorer,argv[2],navbridge::consumer);
        } else initialize(navigation,argv[1],navbridge::provider);
        const auto before=memory;frame();frame();
        check(ImGui::GetDrawData()->TotalVtxCount>0,"actual standalone module draws its own map UI without its peer");
        put(gc+0x8006e4,std::uint8_t{0});frame();
        check(ImGui::GetDrawData()->TotalVtxCount>0,"standalone module keeps its own navigation HUD after map closes");
        inWorld=false;frame();check(ImGui::GetDrawData()->TotalVtxCount==0,"standalone UI hides on title screen");
        memory[gc+0x8006e4]=before.at(gc+0x8006e4);
        check(memory==before&&writes==0&&hooks==0,"standalone execution preserves synthetic game memory and hooks");
        ImGui::DestroyContext();std::printf("PASS %u actual DLL standalone checks\n",checks);return 0;
    }
    initialize(navigation,argv[1],navbridge::provider);initialize(explorer,argv[2],navbridge::consumer);
    const auto before=memory;inWorld=false;frame();navbridge::Sidebar sidebar;
    check(send(&navigation.api,navbridge::consumer,navbridge::sidebar,&sidebar,sizeof(sidebar))==0,"title screen exposes no exploration panel");
    inWorld=true;frame();frame();
    if(fontFallback) {
        check(io.Fonts->Fonts.Size==3,"map and explorer create one private fallback font each");
        check(baseFont->Sources.Size==1&&baseFont->Sources[0]->FontData==baseData,"fallback does not merge into or replace the shared default font");
        for(int i=1;i<io.Fonts->Fonts.Size;++i){auto* ownFont=io.Fonts->Fonts[i];
            check(ownFont->Sources.Size==2&&ownFont->Sources[0]->FontData==baseData&&!ownFont->Sources[0]->FontDataOwnedByAtlas,"each private font preserves the native source without taking ownership");
            check(ownFont->Sources[0]->OversampleH==2&&ownFont->Sources[0]->PixelSnapH&&ownFont->Sources[0]->RasterizerMultiply==1.f,"native font clarity uses pixel snapping and normal ink density");
            check(!baseFont->IsGlyphInFont(0x82b6)&&ownFont->IsGlyphInFont(0x82b6),"private fallback supplies missing glyphs while shared font stays unchanged");}
        check(ImGui::GetFont()==baseFont&&io.FontDefault==nullptr,"font scope restores the current and default font for other Mods");
        check(std::any_of(logs.begin(),logs.end(),[](const std::string& s){return s.find("missing-glyph fallback ready")!=std::string::npos;}),"actual DLL reports fallback readiness after initialization");
    } else check(io.Fonts->Fonts.Size==1,"locked static atlas is left unchanged when dynamic textures are unavailable");
    check(send(&navigation.api,navbridge::consumer,navbridge::sidebar,&sidebar,sizeof(sidebar))==1&&sidebar.h>0&&sidebar.x+sidebar.w<1280,"actual Explorer draws and reserves a bounded map panel");
    check(std::any_of(logs.begin(),logs.end(),[](const std::string& s){return s.find("1 resident chunks, 3 places, challenges=2 done=1 retained=1")!=std::string::npos;}),"actual DLL includes completed pyramid with cleared task ID, alongside city and current task through SDK");
    check(memory==before&&writes==0&&hooks==0,"both actual DLLs leave game/discovery bytes and hooks untouched");
    navbridge::Request request;request.point={position.x+500,position.y+200,80};const auto scope=nav::worldScope(123,"fixture");std::snprintf(request.scope,sizeof(request.scope),"%s",scope.c_str());std::snprintf(request.label,sizeof(request.label),"%s","test town");
    check(send(&explorer.api,navbridge::provider,navbridge::select,&request,sizeof(request))==1,"actual navigation accepts optional Explorer selection");
    const auto key=scope+"/destination";nav::Point decoded;
    check(nav::decode(navigation.files[key],decoded)&&decoded.x==request.point.x,"selection persists in navigation's own scope with full precision");
    check(explorer.files.empty(),"navigation request cannot write the other Mod's storage");
    check(send(&explorer.api,navbridge::provider,navbridge::status,&request,sizeof(request))==1,"selected target is shared instead of drawing a second arrow");
    const auto original=navigation.files;auto other=request;other.point.x+=100;
    check(send(&explorer.api,navbridge::provider,navbridge::clear,&other,sizeof(other))==1&&navigation.files==original,"cancel does not erase a newer different manual target");
    std::snprintf(other.scope,sizeof(other.scope),"%s","wrong world");
    check(send(&explorer.api,navbridge::provider,navbridge::select,&other,sizeof(other))==0&&navigation.files==original,"actual bridge refuses wrong-world payload without data changes");
    other=request;other.version=2;check(send(&explorer.api,navbridge::provider,navbridge::select,&other,sizeof(other))==0,"actual bridge refuses unknown protocol");
    check(send(&explorer.api,navbridge::provider,navbridge::select,&request,sizeof(request)-1)!=1,"actual bridge refuses truncated payload");
    navigation.failSave=true;other=request;other.point.x+=10;
    check(send(&explorer.api,navbridge::provider,navbridge::select,&other,sizeof(other))==0&&navigation.files==original,"storage failure preserves prior navigation target");navigation.failSave=false;
    navigation.failRemove=true;
    check(send(&explorer.api,navbridge::provider,navbridge::clear,&request,sizeof(request))==0&&navigation.files==original,"failed cancellation is reported and preserves target");navigation.failRemove=false;
    check(send(&explorer.api,navbridge::provider,navbridge::clear,&request,sizeof(request))==1&&!navigation.files.count(key),"successful cancellation removes only matching target");
    memory.erase(player+0x10);frame();
    check(send(&navigation.api,navbridge::consumer,navbridge::sidebar,&sidebar,sizeof(sidebar))==0,"unreadable full-precision player position suppresses stale Explorer UI");
    put(gc+0x8006e4,std::uint8_t{0});frame();
    check(writes==0&&hooks==0,"actual module failure paths still perform no game writes or gameplay hooks");
    ImGui::DestroyContext();std::printf("PASS %u actual DLL/SDK integration checks (synthetic memory, no game run)\n",checks);
}

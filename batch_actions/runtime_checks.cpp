#include "batch_actions.cpp"
#include <map>
#include <vector>
#include <cstdio>
#include <cstdlib>

namespace test {
unsigned checks=0,nativeCalls=0,sharedGuardCalls=0;
unsigned failWrite=0;
CubeApi api{};
std::map<unsigned,std::vector<unsigned char>> memory;
std::map<unsigned,CubeHookFn> hooks;
std::map<CubeEvent,CubeEventFn> events;
std::vector<unsigned> uiWrites;
CubeOverlayDrawFn overlayDraw=nullptr;void* overlayUser=nullptr;bool visible=false;
constexpr unsigned gc=0x1000000,player=0x3000000,rows=0x4000000,cells=0x4100000;
constexpr unsigned merchant=0x5000000,shopWidget=0x5100000,shopRows=0x5200000,shopCells=0x5300000;
constexpr unsigned recipeWidget=0x5400000,recipeInputs=0x5500000,bagWidget=0x5600000;
void check(bool v,const char* name){++checks;if(!v){std::printf("FAIL %s\n",name);std::exit(1);}}
template<class T>void put(unsigned a,const T& v){for(auto& s:memory)if(a>=s.first&&std::uint64_t(a-s.first)+sizeof(v)<=s.second.size()){std::memcpy(s.second.data()+a-s.first,&v,sizeof(v));return;}auto& s=memory[a];s.resize(sizeof(v));std::memcpy(s.data(),&v,sizeof(v));}
int32_t CUBE_CALL rd(const CubeApi*,unsigned a,void* out,unsigned n){for(const auto& s:memory)if(a>=s.first&&std::uint64_t(a-s.first)+n<=s.second.size()){std::memcpy(out,s.second.data()+a-s.first,n);return 1;}return 0;}
template<class T>T get(unsigned a){T value{};check(rd(&api,a,&value,sizeof(value))!=0,"fixture readable");return value;}
int32_t CUBE_CALL wr(const CubeApi*,unsigned a,const void* value,unsigned n){uiWrites.push_back(a);if(a==failWrite){failWrite=0;return 0;}for(auto& s:memory)if(a>=s.first&&std::uint64_t(a-s.first)+n<=s.second.size()){std::memcpy(s.second.data()+a-s.first,value,n);return 1;}return 0;}
CubeHookCall dispatch(unsigned address,int button=-1){CubeHookCall c{};c.structSize=sizeof(c);c.hook=CUBE_HOOK_RAW;c.address=address;c.self=gc;c.argCount=button<0?0:1;c.argi[0]=button;hooks.at(address)(&c);return c;}
void cell(unsigned index,int count,const batch::Item& item){put(cells+index*batch::cellSize,batch::Cell{count,item});}
void change(unsigned index,int delta){auto v=get<batch::Cell>(cells+index*batch::cellSize);v.count+=delta;put(cells+index*batch::cellSize,v);}
void __fastcall gameBuy(void*,void*,int button){
    ++sharedGuardCalls;auto c=dispatch(reinterpret_cast<unsigned>(&gameBuy),button);if(c.cancel)return;
    check(button==2,"programmatic purchase uses native middle-button branch");
    check(get<std::array<int,2>>(bagWidget+0x184)==std::array<int,2>{-1,-1},"no player item can be sold");
    check(get<std::array<int,2>>(shopWidget+0x184)==std::array<int,2>{0,0},"captured shop cell is bound");
    check(get<unsigned char>(gc+0x8008f0)==0,"equipment cannot be sold");
    ++nativeCalls;change(0,1);put(player+0x1304,get<int>(player+0x1304)-2);put(gc+0x11b,static_cast<unsigned char>(1));
}
void __fastcall gameCraft(void*,void*){auto c=dispatch(reinterpret_cast<unsigned>(&gameCraft));if(c.cancel)return;++nativeCalls;change(3,1);if(get<batch::Item>(gc+0x1000e7c).bytes[0]==11)change(0,-1);else{change(1,-1);change(2,-1);}}
unsigned __fastcall equipment(void*,void*){return 0;}
unsigned CUBE_CALL rebase(const CubeApi*,unsigned a){if(a==batch::buyFunction)return reinterpret_cast<unsigned>(&gameBuy);if(a==batch::craftFunction)return reinterpret_cast<unsigned>(&gameCraft);if(a==0x47b1b0)return reinterpret_cast<unsigned>(&equipment);return a;}
void CUBE_CALL log(const CubeApi*,CubeLogLevel,const char* text){std::printf("production: %s\n",text);}
unsigned CUBE_CALL subscribe(const CubeApi*,CubeEvent e,CubeEventFn fn,void*){events[e]=fn;return static_cast<unsigned>(e)+1;}
int32_t CUBE_CALL unsubscribe(const CubeApi*,unsigned token){events.erase(static_cast<CubeEvent>(token-1));return 1;}
int32_t CUBE_CALL onRaw(const CubeApi*,unsigned a,CubeCallConv,int,CubeHookFn fn,void*){hooks[a]=fn;return 1;}
int32_t CUBE_CALL remove(const CubeApi*,unsigned a){hooks.erase(a);return 1;}
int32_t CUBE_CALL price(const CubeApi*,unsigned){return 2;}
unsigned CUBE_CALL menu(const CubeApi*,CubeOverlayDrawFn fn,void* user,unsigned,int){overlayDraw=fn;overlayUser=user;return 1;}
int32_t CUBE_CALL unregister(const CubeApi*,unsigned){overlayDraw=nullptr;return 1;}
int32_t CUBE_CALL setVisible(const CubeApi*,unsigned,int v){visible=v!=0;return 1;}
int32_t CUBE_CALL isVisible(const CubeApi*,unsigned){return visible?1:0;}
void* CUBE_CALL context(const CubeApi*){return ImGui::GetCurrentContext();}
void CUBE_CALL allocators(const CubeApi*,void** alloc,void** free,void** user){ImGuiMemAllocFunc a;ImGuiMemFreeFunc f;ImGui::GetAllocatorFunctions(&a,&f,user);*alloc=reinterpret_cast<void*>(a);*free=reinterpret_cast<void*>(f);}
void show(unsigned offset,unsigned at,bool on){put(gc+offset,at);put(at+0x3c,at+0x1000);put(at+0x1068,0);put(at+0x1094,at+0x2000);put(at+0x2000,int(on));}
void fixture(bool water=false,int bottles=0,int flowers=30,int waterCount=30,int potions=0){
    if(::pending.active)::closeDialog();memory.clear();nativeCalls=sharedGuardCalls=0;uiWrites.clear();
    put(0x76b1c8,gc);put(gc+0x8006d0,player);put(gc+0x8009b0,static_cast<unsigned char>(0));put(player+0x11e8,0);
    put(player+0x1304,100);put(player+0x10,std::array<std::int64_t,3>{100000,200000,300000});
    put(player+0x11dc,batch::Vector{rows,rows+12,rows+12});put(rows,batch::Vector{cells,cells+4*batch::cellSize,cells+4*batch::cellSize});
    cell(0,bottles,batch::item(11,12,1,24));cell(1,flowers,batch::item(11,22));cell(2,waterCount,batch::item(11,26));cell(3,potions,water?batch::item(11,26):batch::item(1,1,37));
    if(water)cell(2,0,batch::item(11,26));
    show(0x8008c4,0x5900000,true);show(0x8008bc,0x5a00000,true);show(0x800ad4,0x5800000,true);
    put(gc+0x8008d8,merchant);put(merchant+0x140,static_cast<unsigned char>(0x80));put(gc+0x80095c,shopWidget);put(gc+0x800954,bagWidget);
    put(shopWidget+0x1b4,0);put(shopWidget+0x184,std::array<int,2>{0,0});put(bagWidget+0x184,std::array<int,2>{-1,-1});
    put(gc+0x800c0c,batch::Vector{shopRows,shopRows+12,shopRows+12});put(shopRows,batch::Vector{shopCells,shopCells+batch::cellSize,shopCells+batch::cellSize});put(shopCells,batch::Cell{1,batch::item(11,12,1,24)});
    put(gc+0x800964,recipeWidget);put(gc+0x1000e78,-1.f);const auto output=water?batch::item(11,26):batch::item(1,1,37);put(gc+0x1000e7c,output);put(recipeWidget+0x16c,output);
    const unsigned n=water?1:2;put(gc+0x1000f98,batch::Vector{recipeInputs,recipeInputs+n*batch::cellSize,recipeInputs+n*batch::cellSize});
    if(water)put(recipeInputs,batch::Ingredient{batch::item(11,12,1,24),1});else put(recipeInputs,std::array<batch::Ingredient,2>{batch::Ingredient{batch::item(11,22),1},batch::Ingredient{batch::item(11,26),1}});
    put(gc+0x8008f0,static_cast<unsigned char>(1));put(gc+0x8008f1,static_cast<unsigned char>(0));put(gc+0x8006e4,static_cast<unsigned char>(0));put(gc+0x11b,static_cast<unsigned char>(0));
}
void init(){
    api.abiVersion=CUBE_ABI_VERSION;api.structSize=sizeof(api);api.environment=CUBE_ENV_CLIENT;
    api.mem.read=&rd;api.mem.write=&wr;api.mem.rebase=&rebase;api.log.write=&log;api.items.value=&price;
    api.events.subscribe=&subscribe;api.events.unsubscribe=&unsubscribe;api.hooks.onRaw=&onRaw;api.hooks.removeRaw=&remove;
    api.overlay.registerMenu=&menu;api.overlay.unregisterMenu=&unregister;api.overlay.setVisible=&setVisible;api.overlay.isVisible=&isVisible;api.overlay.context=&context;api.overlay.allocFuncs=&allocators;
    const unsigned image=reinterpret_cast<unsigned>(GetModuleHandleW(nullptr));IMAGE_DOS_HEADER dos{};dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=0x80;put(image,dos);
    IMAGE_NT_HEADERS32 nt{};nt.Signature=IMAGE_NT_SIGNATURE;nt.FileHeader.Machine=IMAGE_FILE_MACHINE_I386;nt.FileHeader.TimeDateStamp=0x51ea955e;nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC;nt.OptionalHeader.SizeOfImage=0x3bc000;nt.OptionalHeader.AddressOfEntryPoint=0x28e1e0;put(image+0x80,nt);
    put(0x47b611,std::array<unsigned char,6>{0x81,0xec,0xd0,3,0,0});put(0x4709d1,std::array<unsigned char,6>{0x81,0xec,0x6c,2,0,0});
    const std::array<unsigned char,6> entry{0x55,0x8b,0xec,0x6a,0xff,0x68};put(reinterpret_cast<unsigned>(&gameBuy),entry);put(reinterpret_cast<unsigned>(&gameCraft),entry);put(reinterpret_cast<unsigned>(&equipment),std::array<unsigned char,6>{0x56,0x8b,0xf1,0x80,0xbe,0xf0});
    auto* info=CubeMod_Init(&api);check(info&&std::strcmp(info->version,"0.1.2")==0,"production entry version");CubeEventArgs ready{};ready.event=CUBE_EVENT_READY;events.at(CUBE_EVENT_READY)(&ready);check(::enabled.load(),"production initialized");::focused=[] {return true;};
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={900,600};io.DeltaTime=1.f/60;
    check(io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc",18,nullptr,io.Fonts->GetGlyphRangesChineseFull())!=nullptr,"Chinese preview font");unsigned char* pixels=nullptr;int w=0,h=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
}
void tick(){CubeEventArgs e{};e.event=CUBE_EVENT_FRAME;events.at(CUBE_EVENT_FRAME)(&e);}
void uiFrame(){ImGui::NewFrame();if(visible&&overlayDraw)overlayDraw(overlayUser);ImGui::Render();}
void openBuy(){auto c=dispatch(reinterpret_cast<unsigned>(&gameBuy),0);check(c.cancel&&::pending.active,"ordinary bottle click opens choice before purchase");tick();check(visible,"quantity dialog visible");}
void openCraft(){auto c=dispatch(reinterpret_cast<unsigned>(&gameCraft));check(c.cancel&&::pending.active,"ordinary craft completion waits for confirmation");tick();}
void confirm(){::pending.confirm=true;tick();}
}
int main(){using namespace test;
    init();fixture();openBuy();check(nativeCalls==0,"opening dialog never spends money");
    uiFrame();ImGui::GetIO().AddKeyEvent(ImGuiKey_D,true);uiFrame();check(::view.quantity.value==20,"D increases displayed quantity by ten");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_D,false);uiFrame();ImGui::GetIO().AddKeyEvent(ImGuiKey_A,true);uiFrame();check(::view.quantity.value==10,"A decreases displayed quantity by ten");ImGui::GetIO().AddKeyEvent(ImGuiKey_A,false);uiFrame();
    put(shopWidget+0x184,std::array<int,2>{-1,-1});put(bagWidget+0x184,std::array<int,2>{0,1});
    confirm();check(nativeCalls==10&&sharedGuardCalls==10,"confirmation re-enters shared native purchase handler ten times");
    check(get<int>(player+0x1304)==80&&get<batch::Cell>(cells).count==10,"native coins and bottle result exact");
    check(get<std::array<int,2>>(shopWidget+0x184)==std::array<int,2>{-1,-1}&&get<std::array<int,2>>(bagWidget+0x184)==std::array<int,2>{0,1},"native hover restored even after overlay changes it");
    check(get<unsigned char>(gc+0x8008f0)==1&&get<unsigned char>(gc+0x11b)==0,"equipment visibility and input state restored");
    for(unsigned a:uiWrites)check(a==bagWidget+0x184||a==shopWidget+0x184||a==gc+0x8008f0||a==gc+0x11b,"Mod writes only temporary UI fields");
    fixture(false,50,7,0,15);openBuy();check(::view.owned==50&&::view.quantity.maximum==0&&nativeCalls==0,"live situation: 50 empty bottles clearly shown as full");uiFrame();::closeDialog();check(nativeCalls==0,"cancel full dialog has no native calls");
    fixture(false,0,30,30);openBuy();::view.quantity.value=40;confirm();check(nativeCalls==40&&get<int>(player+0x1304)==20,"chosen forty is honored, original unit price retained");
    fixture(false,49);openBuy();check(::view.quantity.maximum==1,"remaining carry capacity shown");confirm();check(nativeCalls==1,"carry boundary exact");
    fixture(false,0);put(player+0x1304,7);openBuy();check(::view.quantity.maximum==3,"affordable quantity shown");confirm();check(nativeCalls==3&&get<int>(player+0x1304)==1,"partial affordable quantity uses native price");
    fixture();openBuy();put(bagWidget+0x184,std::array<int,2>{0,1});put(shopWidget+0x184,std::array<int,2>{-1,-1});failWrite=shopWidget+0x184;confirm();
    check(nativeCalls==0&&get<int>(player+0x1304)==100,"failed UI binding never spends money");
    check(get<std::array<int,2>>(bagWidget+0x184)==std::array<int,2>{0,1}&&::enabled.load(),"partial UI binding restores earlier field");
    fixture(true,50,7,0);openCraft();check(nativeCalls==0&&::view.quantity.maximum==50,"water quantity waits without consuming empty bottles");::view.quantity.value=20;confirm();check(nativeCalls==20&&get<batch::Cell>(cells).count==30&&get<batch::Cell>(cells+3*batch::cellSize).count==20,"twenty water consume twenty bottles");
    fixture(false,30,7,10,15);openCraft();check(::view.quantity.maximum==7&&::view.title.find("37")!=std::string::npos,"level 37 and limiting seven flowers displayed");confirm();check(nativeCalls==7&&get<batch::Cell>(cells+batch::cellSize).count==0&&get<batch::Cell>(cells+2*batch::cellSize).count==3,"life batch consumes both native ingredients exactly");
    fixture(false,30,7,0,15);openCraft();check(::view.quantity.maximum==0&&nativeCalls==0,"missing water is visible and never crafted free");::closeDialog();
    fixture(true,20);openCraft();::closeDialog();tick();check(nativeCalls==0,"cancelling craft has no output or consumption");
    fixture();openBuy();put(player+0x10,std::array<std::int64_t,3>{10000000,200000,300000});tick();check(!visible&&!::pending.active&&nativeCalls==0,"movement invalidates pending operation");
    fixture();openBuy();::focused=[] {return false;};tick();check(!visible&&nativeCalls==0,"focus loss closes choice without buying");::focused=[] {return true;};
    fixture();put(gc+0x8009b0,static_cast<unsigned char>(1));auto c=dispatch(reinterpret_cast<unsigned>(&gameBuy),0);check(!c.cancel&&!::pending.active,"network game retains original behavior");
    CubeMod_Shutdown();ImGui::DestroyContext();check(hooks.empty()&&!overlayDraw,"production shutdown releases own registrations");
    std::printf("%u runtime assertions passed; real panel, key events and production callbacks, synthetic native transactions.\n",checks);
}

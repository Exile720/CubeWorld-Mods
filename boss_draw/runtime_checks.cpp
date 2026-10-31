#include "boss_draw.cpp"
#include "test_support.hpp"
#include <thread>
namespace fx=fixture;
static std::vector<loot::Item> inventory;static unsigned grants=0;static bool grantFails=false;
static bool focus(){return true;}
static void publish(){
    fx::put(fx::pc+0x11dc,loot::Vector{fx::rows,fx::rows+12,fx::rows+12});
    const unsigned end=fx::cells+unsigned(inventory.size())*0x11c;fx::put(fx::rows,loot::Vector{fx::cells,end,end});
    for(unsigned i=0;i<inventory.size();++i){fx::put(fx::cells+i*0x11c,1);fx::put(fx::cells+i*0x11c+4,inventory[i]);}
}
static bool grant(unsigned fn,unsigned player,const loot::Item* item){
    fx::check(fn==loot::addFunction&&player==fx::pc,"native grant receives audited function and current character");++grants;
    loot::State disk;fx::check(loot::decode(fx::blobs.at(fx::storageKey("ledger")),disk)&&disk.tickets.front().stage==loot::Stage::Claiming,"durable intent precedes native call");
    if(grantFails)return false;inventory.push_back(*item);publish();return true;
}
// Original 0x489024 -> 0x60CD9B -> 0x59622E passes the embedded World,
// not the owning GameController, as ECX. Keep this independent of production.
static void kill(unsigned receiver=fx::gc+0x2e4){CubeHookCall c{};c.hook=CUBE_HOOK_RAW;c.self=receiver;c.argCount=2;c.argi[0]=fx::enemy;cube::HookCall call(&c,cube::mod().raw());death(call);fx::check(!c.cancel&&!c.overrideReturn,"death hook preserves original result");}
static void workerKill(unsigned count=1){
    DWORD workerId=0;std::thread worker([&]{workerId=GetCurrentThreadId();for(unsigned n=0;n<count;++n)kill();});worker.join();
    fx::check(workerId!=nativeThread.load(),"real death worker differs from the render FRAME thread");
}
static void tick(){nextContextCheck=0;frame(nullptr);}
static void fresh(){
    {std::lock_guard<std::mutex> guard(killMutex);kills.clear();}
    fx::context();fx::blobs.clear();inventory.clear();grants=0;grantFails=false;scope.clear();nativeThread.store(0);firstDeathThread.store(0);nextRespawn=0;fx::failStore=false;enabled.store(true);tick();
}
int main(){
    fx::image();auto a=fx::api();auto info=CubeMod_Init(&a);fx::check(info!=nullptr,"init production code");CubeEventArgs event{};event.structSize=sizeof(event);event.event=CUBE_EVENT_READY;fx::readyFn(&event);
    focused=&focus;give=&grant;fresh();fx::check(dataOk&&!scope.empty(),"valid single player scope loaded");
    kill(fx::gc);tick();fx::check(ledger.tickets.empty()&&grants==0,"controller receiver is rejected; only the original embedded World is valid");
    kill(fx::gc+0x800d44);tick();fx::check(ledger.tickets.empty()&&grants==0,"generation-world receiver cannot substitute for the live embedded World");
    const auto beforeWorker=fx::blobs;workerKill(2);
    fx::check(ledger.tickets.empty()&&fx::blobs==beforeWorker&&grants==0,"death worker only queues snapshots without storage, reveal or native grant");
    {std::lock_guard<std::mutex> guard(killMutex);fx::check(kills.size()==1,"duplicate worker callbacks occupy one bounded queue slot");}
    tick();fx::check(ledger.tickets.size()==1&&ledger.tickets.front().stage==loot::Stage::Sealed&&grants==0,"render FRAME credits one sealed card from another thread without granting equipment");
    workerKill();tick();fx::check(ledger.tickets.size()==1&&grants==0,"later callback for the same dead entity cannot credit twice");
    fresh();workerKill();fx::world.seed=43;tick();
    fx::check(ledger.tickets.empty()&&grants==0,"queued death from another world cannot credit the current scope");
    fx::world.seed=42;tick();fx::check(ledger.tickets.empty(),"discarded foreign-scope snapshot cannot leak back after a world switch");
    fresh();fx::world.seed=43;workerKill();tick();
    fx::check(ledger.tickets.size()==1&&ledger.tickets.front().stage==loot::Stage::Sealed&&grants==0,"scope loading preserves an already queued snapshot for that same new world");
    fresh();
    kill();kill();tick();fx::check(ledger.tickets.size()==1,"duplicate death callback gives one ticket");
    fx::check(ledger.tickets.front().pool.size()==1,"captures exactly the real nonempty loot slots");
    doReveal=true;tick();const auto result=ledger.tickets.front().reward;fx::check(ledger.tickets.front().stage==loot::Stage::Revealed,"explicit draw reveals card");
    const auto key=scope;load(key);doReveal=true;tick();fx::check(loot::equal(ledger.tickets.front().reward,result),"reload keeps exact revealed item");
    doClaim=true;tick();fx::check(grants==1&&inventory.size()==1&&ledger.tickets.empty(),"one click grants exactly one and consumes ticket");doClaim=true;tick();fx::check(grants==1,"double click cannot grant twice");
    fx::put(fx::enemy+0x16c,100.f);nextRespawn=0;tick();fx::put(fx::enemy+0x16c,0.f);kill();tick();fx::check(ledger.tickets.size()==1,"same ID after confirmed revival earns another ticket");
    fresh();fx::put(fx::node+0x18,0.f);kill();tick();fx::check(ledger.tickets.empty(),"nearby unparticipated kill earns nothing");
    fx::put(fx::node+0x18,12.f);fx::put(fx::enemy+0x7e,static_cast<unsigned short>(0));kill();tick();fx::check(ledger.tickets.empty(),"ordinary enemy earns nothing");
    fx::put(fx::enemy+0x7e,static_cast<unsigned short>(0x200));fx::session.networkMode=CUBE_NET_MULTIPLAYER;kill();tick();fx::check(ledger.tickets.empty(),"network session never awards");
    fresh();kill();tick();doReveal=true;tick();grantFails=true;doClaim=true;tick();fx::check(grants==1&&ledger.tickets.front().stage==loot::Stage::Claiming,"ambiguous native add remains journaled");
    load(scope);doClaim=true;tick();fx::check(grants==1,"restart after ambiguous add cannot retry or duplicate");
    fresh();kill();tick();doReveal=true;tick();fx::failStore=true;doClaim=true;tick();fx::check(!dataOk&&grants==0,"failed claim persistence never calls native add");
    fx::failStore=false;fx::blobs[fx::storageKey("ledger")][0]^=1;load(scope);fx::check(!dataOk,"corrupt ledger locks instead of resetting");
    fresh();kill();tick();const auto first=scope;fx::world.seed=43;tick();fx::check(scope!=first&&ledger.tickets.empty(),"different worlds cannot share tickets");fx::world.seed=42;tick();fx::check(scope==first&&ledger.tickets.size()==1,"original world's rewards remain");
    fx::put(fx::gc+0x800a0c,1);tick();fx::check(scope!=first&&ledger.tickets.empty(),"same-name same-class character in another save slot cannot share rewards");fx::put(fx::gc+0x800a0c,0);tick();fx::check(scope==first&&ledger.tickets.size()==1,"original character's rewards remain");
    fx::player.classForm=2;tick();fx::check(scope!=first&&ledger.tickets.empty(),"class swap keeps class pity and pending rewards separate");fx::player.classForm=3;tick();
    fx::ui.anyOpen=1;doReveal=true;tick();fx::check(ledger.tickets.front().stage==loot::Stage::Sealed,"native menu blocks reveal without consuming ticket");fx::ui.anyOpen=0;
    fx::put(fx::pc+0x11e8,1u);doReveal=true;tick();fx::check(ledger.tickets.front().stage==loot::Stage::Sealed,"cursor-held item blocks reveal without consuming ticket");fx::put(fx::pc+0x11e8,0u);
    const auto preserved=fx::blobs;fx::failScope=true;load("unavailable_scope");fx::check(!dataOk&&fx::blobs==preserved,"failed scope selection cannot access or overwrite another character's rewards");fx::failScope=false;load(first);
    nextContextCheck=GetTickCount64()+60000;fx::sessionReads=0;
    for(unsigned idle=0;idle<1000;++idle)frame(nullptr);
    fx::check(fx::sessionReads==0,"idle frames do not repeatedly walk entity/session state");
    // Exercise the actual panel against a real ImGui context, including result and error states.
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.DisplaySize={800,600};io.DeltaTime=1.f/60;io.IniFilename=io.LogFilename=nullptr;unsigned char* atlas=nullptr;int w=0,h=0;io.Fonts->GetTexDataAsRGBA32(&atlas,&w,&h);
    for(unsigned mode=0;mode<4;++mode){lootui::View v{&ledger,"测试提示",true,false,-10};if(mode==1)loot::reveal(ledger);if(mode==2)ledger.tickets.front().stage=loot::Stage::Claiming;if(mode==3)ledger.tickets.clear();ImGui::NewFrame();lootui::draw(v);ImGui::Render();fx::check(ImGui::GetDrawData()->TotalVtxCount>0,"production panel renders valid geometry");}
    loot::State animated;loot::Ticket card;card.cl=4;card.bossLevel=45;card.rank=4;loot::credit(animated,card);loot::reveal(animated);const auto savedAnimation=loot::encode(animated);
    for(double age:{0.,.25,.51,.8,1.1,2.1,4.1,6.1,8.1}){ImGui::NewFrame();lootui::View v{&animated,"",true,false,ImGui::GetTime()-age};lootui::draw(v);ImGui::Render();
        const auto* data=ImGui::GetDrawData();bool finite=true;for(int list=0;list<data->CmdListsCount;++list)for(const auto& vertex:data->CmdLists[list]->VtxBuffer)finite&=std::isfinite(vertex.pos.x)&&std::isfinite(vertex.pos.y);
        fx::check(finite&&data->TotalVtxCount>0,"card flip phases produce finite render geometry");}
    for(const auto viewport:{ImVec2{520,500},ImVec2{1280,720}}){io.DisplaySize=viewport;ImGui::NewFrame();lootui::View v{&animated,"请在单人世界站稳、关闭原生菜单并放下手中物品后操作。",false,false,-10};lootui::draw(v);ImGui::Render();
        fx::check(ImGui::GetDrawData()->TotalVtxCount>0,"redesigned panel renders in small and large viewports");}
    fx::check(loot::encode(animated)==savedAnimation,"animation never mutates saved reward, pity or tickets");
    ImGui::DestroyContext();stop();fx::check(fx::hooks.empty()&&fx::closedMenus==fx::menuCount,"production shutdown cleanup");
    std::printf("PASS %u production callback, persistence, grant and UI assertions. Native add simulated.\n",fx::assertions);
}

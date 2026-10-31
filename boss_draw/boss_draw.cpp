#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "cube_mod.hpp"
#include "loot.hpp"
#include "panel.hpp"
#include "font.hpp"
#include <atomic>
#include <mutex>
#include <map>
#include <set>

namespace {
std::atomic<bool> enabled{false};std::atomic<DWORD> nativeThread{0};
std::atomic<DWORD> firstDeathThread{0};
struct PendingDeath {std::string scope;loot::Ticket ticket;unsigned entity=0;};
std::mutex killMutex;std::vector<PendingDeath> kills;
// Only FRAME owns the ledger, current scope and credited-death deduplication.
std::map<std::uint64_t,unsigned> dead;
loot::State ledger;std::string scope,notice;bool dataOk=false,hooked=false;
unsigned frameToken=0,menuHandle=0,hudHandle=0;bool doReveal=false,doClaim=false;double revealedAt=-10;
std::uint64_t animSerial=0;
cube::Menu* hud=nullptr;bossui::TextFont font;ULONGLONG nextRespawn=0;
ULONGLONG nextContextCheck=0;
bool read(unsigned address,void* out,unsigned bytes){
    const auto api=cube::mod().raw();if(!api||address<0x10000||std::uint64_t(address)+bytes>0x100000000ull||bytes>loot::maxBytes)return false;
    auto p=static_cast<unsigned char*>(out);while(bytes){unsigned n=(std::min)(bytes,4096-(address&4095));if(!api->mem.read(api,address,p,n))return false;address+=n;p+=n;bytes-=n;}return true;
}
template<class T>bool get(unsigned a,T& v){return read(a,&v,sizeof(v));}
template<std::size_t N>bool signature(unsigned a,const std::array<unsigned char,N>& expected){std::array<unsigned char,N> v{};return read(cube::mod().rebase(a),v.data(),unsigned(N))&&v==expected;}
const char* supportFailure="not checked";
bool supported(){
    const unsigned base=reinterpret_cast<unsigned>(GetModuleHandleW(nullptr));IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
    supportFailure="PE header/build mismatch";
    if(!get(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0x40||dos.e_lfanew>0x1000||!get(base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||
       nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||nt.FileHeader.TimeDateStamp!=0x51ea955e||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC||
       nt.OptionalHeader.SizeOfImage!=0x3bc000||nt.OptionalHeader.AddressOfEntryPoint!=0x28e1e0)return false;
    // The SEH-handler immediate is an IMAGE_REL_BASED_HIGHLOW relocation, not a
    // fixed instruction byte. Check its exact rebased value; never skip it.
    auto deathBytes=std::array<unsigned char,10>{0x55,0x8b,0xec,0x6a,0xff,0x68,0,0,0,0};
    const unsigned handler=cube::mod().rebase(0x6f207b);std::memcpy(deathBytes.data()+6,&handler,4);
    supportFailure="death entry or relocated SEH handler mismatch";
    if(!signature(loot::deathFunction,deathBytes))return false;
    supportFailure="native inventory-add entry mismatch";
    if(!signature(loot::addFunction,std::array<unsigned char,9>{0x55,0x8b,0xec,0x81,0xec,0x54,2,0,0}))return false;
    supportFailure="death call-site mismatch";
    if(!signature(0x59622e,std::array<unsigned char,5>{0xe8,0x4d,0x4f,0,0}))return false;
    supportFailure="verified";return true;
}
bool systemFocused(){DWORD pid=0;const auto w=GetForegroundWindow();return w&&GetWindowThreadProcessId(w,&pid)&&pid==GetCurrentProcessId();}
bool (*focused)()=&systemFocused;
std::string worldName(unsigned gc){
    const unsigned p=gc+0x378;unsigned n=0,cap=0,address=0;
    if(!get(p+16,n)||!get(p+20,cap)||!n||n>128||cap<n||cap>1024)return {};
    address=p;if(cap>=16&&!get(p,address))return {};std::string name(n,'\0');char end=1;
    if(!read(address,name.data(),n)||!get(address+n,end)||end)return {};return name;
}
bool context(CubePlayer& p,CubeSession& s,std::string& key){
    const auto api=cube::mod().raw();p={};s={};CubeWorld w{};unsigned live=0;int character=-1;
    if(!api->player.get(api,&p)||!api->session.get(api,&s)||!api->world.get(api,&w)||s.gameState!=CUBE_STATE_IN_WORLD||
        !s.hasNetwork||s.networkMode!=CUBE_NET_SINGLEPLAYER||s.connected||s.playerCount!=1||!p.address||!p.hasName||!p.name[0]||
        !std::memchr(p.name,0,sizeof(p.name))||p.classForm<1||p.classForm>4||!w.hasSeed||s.address!=w.address||
        !get(s.address+0x8006d0,live)||live!=p.address||!get(s.address+0x800a0c,character)||character<0||character>10000)return false;
    const auto name=worldName(s.address);if(name.empty())return false;
    key="world_"+std::to_string(w.seed)+"_"+std::to_string(loot::hash(name.data(),name.size()))+"_char_"+std::to_string(character)+"_"+
        std::to_string(loot::hash(p.name,std::strlen(p.name)))+"_class_"+std::to_string(p.classForm);return true;
}
void lockData(const char* why){dataOk=false;notice=why;cube::mod().log.error("boss draw locked: %s",why);}
bool persist(const loot::State& next){
    if(!dataOk)return false;const auto text=loot::encode(next);loot::State check;
    if(!loot::decode(text,check)){lockData("抽奖记录校验失败，已停止发奖。");return false;}
    auto storage=cube::mod().storage();
    if(storage.has("ledger")){
        const auto before=storage.getString("ledger");loot::State old;
        if(!loot::decode(before,old)||!storage.putString("ledger_backup",before)||storage.getString("ledger_backup")!=before){lockData("抽奖备份失败，已停止发奖，原记录保留。");return false;}
    }
    if(!storage.putString("ledger",text)||storage.getString("ledger")!=text){lockData("抽奖保存失败，已停止发奖；请保留数据与日志。");return false;}
    ledger=next;return true;
}
void load(const std::string& key){
    scope=key;ledger={};notice.clear();dataOk=true;doReveal=doClaim=false;revealedAt=-10;animSerial=0;
    dead.clear();
    auto storage=cube::mod().storage();const auto api=cube::mod().raw();
    if(!api->storage.setScope(api,scope.c_str())){lockData("无法确认角色抽奖数据范围，已停止发奖。");return;}
    if(storage.has("ledger")){
        const int n=storage.size("ledger");if(n<40||n>loot::maxBytes||!loot::decode(storage.getString("ledger"),ledger))lockData("抽奖记录损坏，已锁定；不会自动回退重发，请保留数据与日志。");
    }else{
        FILETIME time{};GetSystemTimeAsFileTime(&time);ledger.rng=(std::uint64_t(time.dwHighDateTime)<<32)|time.dwLowDateTime;ledger.rng^=GetTickCount64()^GetCurrentProcessId();
    }
    if(dataOk&&!ledger.tickets.empty()&&ledger.tickets.front().stage==loot::Stage::Claiming)
        notice="上次领奖在中途停止，已锁定这张卡以防重复发放；请先核对背包并保留日志。";
    cube::mod().log.info("boss draw: loaded scope=%s pending=%u valid=%d",scope.c_str(),unsigned(ledger.tickets.size()),dataOk?1:0);
}
const char* snapshotFailure(unsigned entity,std::uint64_t playerId){
    float hp=1;int level=0;unsigned char rank=0;
    if(!get(entity+0x16c,hp)||!std::isfinite(hp))return "unreadable health";
    if(hp>0)return "health is still positive at death entry";
    if(!loot::contributed(&read,entity,playerId))return "no confirmed positive local damage contribution";
    if(!get(entity+0x190,level)||level<1||level>32767)return "unsupported boss level";
    if(!get(entity+0x1a8,rank)||rank>4)return "unsupported boss rank";
    for(unsigned off:loot::slots){loot::Item item;if(!read(entity+off,item.b.data(),unsigned(item.b.size())))return "unreadable worn equipment";
        if(item.b[0]&&!loot::valid(item))return "unsupported worn equipment";}
    return "snapshot unreadable or changed during observation";
}
void death(cube::HookCall& call){
    if(!enabled.load()||call.cancelled()||call.argCount()!=2)return;
    const unsigned entity=unsigned(call.argInt(0));unsigned actual=0;unsigned char category=0;unsigned short flags=0;
    if(!get(entity,actual)||actual!=cube::mod().rebase(0x6fd8cc)||!get(entity+0x60,category)||category!=1||
       !get(entity+0x7e,flags)||(flags&0x200)==0||(flags&0x800))return;
    DWORD expected=0;if(firstDeathThread.compare_exchange_strong(expected,GetCurrentThreadId()))
        cube::mod().log.info("boss draw: first boss death thread=%lu render thread=%lu receiver=%08x",GetCurrentThreadId(),nativeThread.load(),call.self());
    CubePlayer p{};CubeSession s{};std::string key;
    if(!context(p,s,key)){cube::mod().log.warn("boss draw: rejected boss candidate entity=%08x: context unavailable state=%d network=%d mode=%d connected=%d players=%d class=%d",entity,int(s.gameState),s.hasNetwork,int(s.networkMode),s.connected,s.playerCount,p.classForm);return;}
    // Original GC+0x2e4 World is passed through 0x60c510 -> 0x595a60 ->
    // 0x59b180. CubeSession.address is its owner GC, not this receiver.
    const unsigned worldReceiver=s.address+0x2e4;
    if(call.self()!=worldReceiver){cube::mod().log.warn("boss draw: rejected boss candidate entity=%08x: receiver=%08x expected World=%08x owner=%08x",entity,call.self(),worldReceiver,s.address);return;}
    std::uint64_t playerId=0;if(!get(p.address+8,playerId)){cube::mod().log.warn("boss draw: rejected boss candidate entity=%08x: local identity unreadable",entity);return;}
    loot::Ticket t;t.cl=unsigned(p.classForm);
    if(!loot::snapshot(&read,entity,playerId,t)){cube::mod().log.warn("boss draw: rejected boss candidate entity=%08x: %s",entity,snapshotFailure(entity,playerId));return;}
    char name[16]{};if(read(entity+0x1168,name,16)&&std::memchr(name,0,16)){
        wchar_t temp[32]{};const unsigned n=unsigned(std::strlen(name));
        if(n&&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name,int(n),temp,32)>0)t.bossName=name;
    }
    if(t.bossName.empty())t.bossName="首领 #"+std::to_string(t.bossId);
    // Death callbacks can run on the update thread. They only enqueue immutable
    // snapshots; FRAME validates the scope and performs every storage operation.
    std::lock_guard<std::mutex> guard(killMutex);
    if(!enabled.load())return;
    for(const auto& queued:kills)if(queued.scope==key&&queued.ticket.bossId==t.bossId&&queued.entity==entity)return;
    if(kills.size()>=loot::maxTickets){cube::mod().log.warn("boss draw: death observation queue full; original loot unaffected");return;}
    kills.push_back({std::move(key),std::move(t),entity});
}
// This leaf holds no C++ unwind objects. A partial/exceptional add is never replayed.
bool nativeAdd(unsigned fn,unsigned player,const loot::Item* item){
    __try{using Fn=void(__thiscall*)(void*,const loot::Item*,int);reinterpret_cast<Fn>(fn)(reinterpret_cast<void*>(player+0x11dc),item,-1);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool (*give)(unsigned,unsigned,const loot::Item*)=&nativeAdd;
bool standing(const CubePlayer& p){return p.alive&&p.hasState&&p.onGround&&p.actionId==0&&std::isfinite(p.speed)&&p.speed<.05f;}
bool canAct(const CubePlayer& p){
    if(!dataOk||!focused()||!standing(p))return false;CubeUi ui{};const auto api=cube::mod().raw();
    unsigned held=0;return api->ui.get(api,&ui)&&ui.valid&&!ui.anyOpen&&get(p.address+0x11e8,held)&&!held&&nativeThread.load()==GetCurrentThreadId();
}
void claim(const CubePlayer& p){
    if(ledger.tickets.empty()||ledger.tickets.front().stage!=loot::Stage::Revealed||!canAct(p))return;
    auto next=ledger;auto& t=next.tickets.front();int before=0;
    if(!loot::inventoryCount(&read,p.address,t.reward,before)){notice="背包暂时无法核实，奖励已保留，请稍后再领。";return;}
    t.stage=loot::Stage::Claiming;t.beforeCount=before;if(!persist(next))return;
    const loot::Item reward=t.reward;const auto serial=t.serial;
    const bool called=give(cube::mod().rebase(loot::addFunction),p.address,&reward);int after=0;
    if(!called||!loot::inventoryCount(&read,p.address,reward,after)||after!=before+1){
        notice="领奖结果无法确认，已锁定这张卡防止重复发放；请核对背包并保留日志。";
        cube::mod().log.error("boss draw: ambiguous native grant serial=%llu called=%d before=%d after=%d; no retry",serial,called?1:0,before,after);return;
    }
    next=ledger;next.tickets.erase(next.tickets.begin());
    if(!persist(next))return;
    notice="已领取 "+loot::name(reward)+"，请在背包查看和对比。";
    cube::mod().log.info("boss draw: granted serial=%llu type=%u subtype=%u quality=%u level=%d",serial,reward.b[0],reward.b[1],reward.b[12],loot::level(reward));
}
void CUBE_CALL frame(CubeEventArgs*){
    if(!enabled.load())return;
    const auto now=GetTickCount64();
    // session.get walks the entity tree. Poll idle context at 2 Hz; real deaths
    // and explicit draw/claim requests still revalidate immediately.
    bool hasKills=false;{std::lock_guard<std::mutex> guard(killMutex);hasKills=!kills.empty();}
    if(!scope.empty()&&now<nextContextCheck&&!doReveal&&!doClaim&&!hasKills)return;
    nextContextCheck=now+500;CubePlayer p{};CubeSession s{};std::string key;
    if(!context(p,s,key)){scope.clear();cube::mod().menu().setOpen(false);return;}
    DWORD expected=0;if(nativeThread.compare_exchange_strong(expected,GetCurrentThreadId()))
        cube::mod().log.info("boss draw: first render FRAME thread=%lu",GetCurrentThreadId());
    if(nativeThread.load()!=GetCurrentThreadId()){enabled.store(false);return;}
    if(scope!=key)load(key);
    std::vector<PendingDeath> observed;{std::lock_guard<std::mutex> guard(killMutex);observed.swap(kills);}
    if(now>=nextRespawn){nextRespawn=now+500;for(auto it=dead.begin();it!=dead.end();){std::uint64_t id=0;float hp=0;unsigned live=0;
        const int found=loot::entityAt(&read,s.address,it->first,live);
        if(found==0||(found==1&&live!=it->second)||(get(it->second+8,id)&&id==it->first&&get(it->second+0x16c,hp)&&std::isfinite(hp)&&hp>0))it=dead.erase(it);else ++it;}}
    for(const auto& queued:observed){
        if(queued.scope!=key){cube::mod().log.warn("boss draw: discarded death snapshot from a different world or character");continue;}
        if(!dataOk)break;
        const auto& t=queued.ticket;const auto previous=dead.find(t.bossId);
        if(previous!=dead.end()&&previous->second==queued.entity)continue;
        dead[t.bossId]=queued.entity;auto next=ledger;
        if(!loot::credit(next,t)){notice="待领取次数已满，请先领取；本次新增机会未能保存。";cube::mod().log.error("boss draw: credit queue full or invalid; original loot unaffected");continue;}
        if(persist(next)){notice="获得一次首领抽奖，站稳后按 F9 开奖。";cube::mod().log.info("boss draw: credited boss=%llu serial=%llu level=%d pool=%u",t.bossId,ledger.nextSerial-1,t.bossLevel,unsigned(t.pool.size()));}
    }
    if(doReveal){doReveal=false;if(canAct(p)){auto next=ledger;if(loot::reveal(next)&&persist(next)){animSerial=0;notice="装备已确定；关闭、重开或退出不会重抽。";}}}
    if(doClaim){doClaim=false;claim(p);}
    if(!focused())cube::mod().menu().setOpen(false);
}
void draw(){
    if(!enabled.load()||scope.empty()){cube::mod().menu().setOpen(false);return;}
    bossui::ScopedTextFont ownFont(font.ensure(ImGui::GetFont()));CubePlayer p{};CubeSession s{};std::string key;
    const bool available=context(p,s,key)&&key==scope&&canAct(p);
    if(!ledger.tickets.empty()&&ledger.tickets.front().stage!=loot::Stage::Sealed&&animSerial!=ledger.tickets.front().serial){animSerial=ledger.tickets.front().serial;revealedAt=ImGui::GetTime();}
    auto message=notice;if(!available&&dataOk)message="请在单人世界站稳、关闭原生菜单并放下手中物品后操作。";
    lootui::View view{&ledger,message,available,doReveal||doClaim,revealedAt};view.recordsValid=dataOk;const auto a=lootui::draw(view);
    if(a.close)cube::mod().menu().setOpen(false);else{doReveal|=a.reveal;doClaim|=a.claim;}
}
void drawHud(){
    if(!enabled.load()||scope.empty()||(ledger.tickets.empty()&&dataOk))return;
    bossui::ScopedTextFont ownFont(font.ensure(ImGui::GetFont()));const auto display=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({display.x*.5f,48},ImGuiCond_Always,{.5f,0});ImGui::SetNextWindowBgAlpha(.82f);
    if(ImGui::Begin("###boss_draw_hint",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoSavedSettings))
        ImGui::TextColored({.98f,.8f,.38f,1},!dataOk?"首领抽奖：记录异常，请按 F9 查看":
            (ledger.tickets.size()>=loot::maxTickets?"首领抽奖：暂存次数已满，请先领取  [F9]":"首领抽奖：待领取 %u 次  [F9]"),unsigned(ledger.tickets.size()));
    ImGui::End();
}
void stop(){
    enabled.store(false);auto& mod=cube::mod();const auto api=mod.raw();
    if(hooked){mod.eventHook.removeRaw(mod.rebase(loot::deathFunction));hooked=false;}
    {std::lock_guard<std::mutex> guard(killMutex);kills.clear();}
    if(frameToken&&api->events.unsubscribe){api->events.unsubscribe(api,frameToken);frameToken=0;}
    if(menuHandle&&api->overlay.unregisterMenu){api->overlay.unregisterMenu(api,menuHandle);menuHandle=0;}
    if(hudHandle&&api->overlay.unregisterMenu){api->overlay.unregisterMenu(api,hudHandle);hudHandle=0;}
}
void ready(){
    if(!supported()){cube::mod().log.error("boss draw refused at READY: %s",supportFailure);return;}
    hooked=cube::mod().eventHook.raw(cube::mod().rebase(loot::deathFunction),cube::CallConv::Thiscall,2,&death);
    if(!hooked){stop();cube::mod().log.error("boss draw refused: death observation unavailable");return;}
    enabled.store(true);cube::mod().log.info("boss draw 0.1.7 ready: native embedded World death receiver verified; queued snapshots; manual reveal and claim retained");
}
}
CUBE_MOD("Boss Draw","0.1.7","CubeWorld Local Mods"){
    mod.setId("local.cubeworld.boss-draw");mod.setCapabilities(cube::Capability::RawMem|cube::Capability::RawHooks|cube::Capability::Writes|cube::Capability::Overlay);
    mod.setPriority(110);if(!supported()){mod.log.error("boss draw refused: %s",supportFailure);return;}
    const auto api=mod.raw();if(!api->player.get||!api->world.get||!api->session.get||!api->ui.get||!api->events.subscribe||!api->events.unsubscribe||
        !api->storage.has||!api->storage.get||!api->storage.put||!api->storage.setScope||!api->overlay.registerMenu||!api->overlay.unregisterMenu){mod.log.error("boss draw refused: missing context, storage or menu API");return;}
    mod.menu().setToggleKey(VK_F9).setOpen(false).onDraw(&draw);menuHandle=mod.menu().handle();
    if(!menuHandle){stop();return;}hud=&mod.addMenu();hud->setToggleKey(0).setOpen(true).onDraw(&drawHud).setPassthrough(true);hudHandle=hud->handle();
    if(!hudHandle){stop();return;}frameToken=api->events.subscribe(api,CUBE_EVENT_FRAME,&frame,nullptr);if(!frameToken){stop();return;}
    mod.eventListener.on(cube::Event::Ready,&ready);
}
CUBE_MOD_API void CUBE_CALL CubeMod_Shutdown(){stop();}

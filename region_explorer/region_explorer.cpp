#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "cube_mod.hpp"
#include "explore_ui.hpp"
#include "explore_font.hpp"

namespace {
constexpr unsigned gcGlobal=0x76b1c8,mapOpenOffset=0x8006e4;
explore::Snapshot snapshot;
explore::Names names;
std::string scope,targetLabel,notice;
nav::Point target{},pressPoint{};
bool selected=false,leftWasDown=false,pressed=false,namesReady=false,logged=false;
int tab=0,bridgeState=-1;
size_t page=0;
ULONGLONG nextScan=0,nextBridge=0,noticeUntil=0;
navbridge::Sidebar reserved;
exploreui::TextFont textFont;
bool fontLogged=false;
bool predictionsBlocked=false;

unsigned controller() {auto& m=cube::mod();return m.read<unsigned>(m.rebase(gcGlobal));}
std::string worldName(unsigned gc) {
    auto& m=cube::mod();const unsigned str=gc+0x2e4+0x94;
    const auto n=m.read<unsigned>(str+0x10,0xffffffffu),capacity=m.read<unsigned>(str+0x14,0xffffffffu);
    if(!n||n>128||capacity<n||capacity>1024)return {};
    const unsigned address=capacity<16?str:m.read<unsigned>(str);
    std::string name(n,'\0');
    if(!m.raw()->mem.read(m.raw(),address,name.data(),n)||m.read<char>(address+n)!=0||
       std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32;}))return {};
    return name;
}
void loadNames() {
    auto& m=cube::mod();
    for(size_t i=0;i<names.size();++i) {
        wchar_t text[32]{};unsigned count=0;
        for(;count<31;++count) {
            if(!m.raw()->mem.read(m.raw(),m.rebase(0x400000+explore::nameRvas[i])+count*2,text+count,2))break;
            if(!text[count])break;
        }
        if(count==0||count>=31||text[count])continue;
        const int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,count,nullptr,0,nullptr,nullptr);
        if(bytes>0) {names[i].resize(bytes);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,count,names[i].data(),bytes,nullptr,nullptr);}
    }
    namesReady=true;
}
void message(const char* text) {notice=text;noticeUntil=GetTickCount64()+5000;}
navbridge::Request request() {
    navbridge::Request r;r.point=target;
    std::snprintf(r.scope,sizeof(r.scope),"%s",scope.c_str());std::snprintf(r.label,sizeof(r.label),"%s",targetLabel.c_str());return r;
}
void reset() {scope.clear();snapshot={};selected=leftWasDown=pressed=logged=predictionsBlocked=false;reserved.h=0;}
bool updateWorld(unsigned gc) {
    auto& m=cube::mod();const auto w=m.world();const auto name=worldName(gc);
    if(!w.valid()||!w.hasSeed()||name.empty()){reset();return false;}
    const auto next=nav::worldScope(w.getSeed(),name);
    if(next!=scope) {
        scope=next;snapshot={};selected=false;tab=0;page=0;nextScan=nextBridge=0;logged=predictionsBlocked=false;bridgeState=-1;notice.clear();
        m.storage().setScope(scope.c_str());
        if(m.storage().has("target")&&m.storage().size("target")<=512)
            selected=explore::decodeTarget(m.storage().getString("target"),target,targetLabel);
        if(selected)targetLabel=explore::reviewedTargetLabel(targetLabel);
        m.log.info("region explorer: world scope=%s restored=%d",scope.c_str(),selected?1:0);
    }
    return true;
}
bool mousePoint(nav::Screen& p) {
    const auto hwnd=GetForegroundWindow();DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);
    if(pid!=GetCurrentProcessId())return false;
    POINT cursor{};RECT rect{};
    if(!GetCursorPos(&cursor)||!ScreenToClient(hwnd,&cursor)||!GetClientRect(hwnd,&rect)||rect.right<=0||rect.bottom<=0)return false;
    if(cursor.x<0||cursor.y<0||cursor.x>=rect.right||cursor.y>=rect.bottom)return false;
    const auto size=ImGui::GetIO().DisplaySize;p={cursor.x*static_cast<double>(size.x)/rect.right,cursor.y*static_cast<double>(size.y)/rect.bottom};return true;
}
void choose(const explore::Place& place) {
    auto& m=cube::mod();const auto label=explore::title(place);
    if(!explore::safeLabel(label)||!m.storage().putString("target",explore::encodeTarget(place.position,label))) {
        message("目标保存失败，原目标保留");return;
    }
    target=place.position;targetLabel=label;selected=true;auto r=request();
    bridgeState=m.services().sendMessage(navbridge::provider,navbridge::select,&r,sizeof(r));nextBridge=GetTickCount64()+1000;
    message(bridgeState==1?"已设置导航，前往地点中心":bridgeState<0?"已设置探索目标，关闭地图看方向":"导航尚未就绪，请再次选择");
    m.log.info("region explorer: selected %s id=%u category=%u mission=%u state=%u x=%.3f y=%.3f bridge=%d",label.c_str(),place.id,place.category,place.mission,place.state,target.x,target.y,bridgeState);
}
void clearSelection() {
    auto& m=cube::mod();auto r=request();
    if(m.services().sendMessage(navbridge::provider,navbridge::clear,&r,sizeof(r))==0){message("导航取消失败，请重试");return;}
    if(m.storage().has("target")&&!m.storage().remove("target")){message("取消失败，请重试");return;}
    selected=false;message("探索目标已取消");
}
bool mapProjection(unsigned gc,nav::Point player,nav::Projection& result) {
    struct RawMatrix {float v[16];};struct Pan {float x,y,z;};auto& m=cube::mod();
    const auto pan=m.read<Pan>(gc+0x1000e4c);if(!std::isfinite(pan.x)||!std::isfinite(pan.y)||!std::isfinite(pan.z))return false;
    const auto p=m.read<RawMatrix>(gc+0x800d44+4),v=m.read<RawMatrix>(gc+0x800d44+0x44);
    nav::Matrix perspective{},view{};for(int i=0;i<16;++i){perspective[i]=p.v[i];view[i]=v.v[i];}
    const nav::Point origin{player.x+std::trunc(pan.x*65536.0)/65536,player.y+std::trunc(pan.y*65536.0)/65536,player.z+std::trunc(pan.z*65536.0)/65536};
    return nav::nativeMapProjection(perspective,view,origin,m.read<int>(gc+0x11c),m.read<int>(gc+0x120),result);
}
void draw() {
    auto& m=cube::mod();const auto gc=controller();const auto player=m.player();reserved.h=0;
    if(!gc||!m.session().isInWorld()||!player.valid()||!player.hasPosition()){reset();return;}
    if(!updateWorld(gc))return;
    struct Fixed {std::int64_t x,y,z;};Fixed raw{};
    if(!m.raw()->mem.read(m.raw(),player.raw().address+0x10,&raw,sizeof(raw)))return;
    const nav::Point position{raw.x/65536.0,raw.y/65536.0,raw.z/65536.0};
    if(!nav::finite(position))return;
    if(!namesReady)loadNames();
    const auto now=GetTickCount64();const bool open=m.read<unsigned char>(gc+mapOpenOffset)==1;
    if(open&&now>=nextScan) {
        nextScan=now+2000;
        const auto previous=snapshot.places.size();
        snapshot=explore::scan([&](unsigned address,void* out,unsigned size){return m.raw()->mem.read(m.raw(),address,out,size)!=0;},gc,position);
        for(auto& p:snapshot.places)p.name=explore::name(p.seed,names);
        if(tab==1) {
            const auto stats=explore::boss::enrich(snapshot,[&](unsigned address,void* out,unsigned bytes){return m.raw()->mem.read(m.raw(),address,out,bytes)!=0;},gc,position,!predictionsBlocked);
            if(stats.mismatches&&!predictionsBlocked) {
                predictionsBlocked=true;
                m.log.info("region explorer: boss class prediction differs from live entity; predictions disabled for this world session, confirmed readings retained");
            }
            if(stats.candidates)m.log.info("region explorer: boss classes candidates=%u confirmed=%u predicted=%u mismatches=%u zones=%u spawns=%u truncated=%d total scan=%llu ms",stats.candidates,stats.confirmed,stats.predicted,stats.mismatches,stats.zones,stats.spawns,stats.truncated?1:0,GetTickCount64()-now);
        }
        if(!logged||previous!=snapshot.places.size()) {
            const auto tasks=explore::counts(explore::rows(snapshot.places,position,1));
            m.log.info("region explorer: %u resident chunks, %u places, challenges=%u done=%u retained=%u, rejected=%u, scan=%llu ms",snapshot.chunks,static_cast<unsigned>(snapshot.places.size()),tasks.total,tasks.done,tasks.retained,snapshot.rejected,GetTickCount64()-now);logged=true;
        }
    }
    if(selected&&now>=nextBridge){auto r=request();bridgeState=m.services().sendMessage(navbridge::provider,navbridge::status,&r,sizeof(r));nextBridge=now+1000;}
    const auto size=ImGui::GetIO().DisplaySize;if(size.x<640||size.y<480)return;
    const auto s=std::clamp(size.y/1080.0f,.70f,1.45f);auto* d=ImGui::GetForegroundDrawList();
    ImFont* ownFont=(open||(selected&&bridgeState<0))?textFont.ensure(ImGui::GetFont()):nullptr;
    if(!fontLogged&&textFont.attempted()) {
        m.log.info("region explorer: private native-style font with system missing-glyph fallback %s; shared default font unchanged",ownFont?"ready":"unavailable");fontLogged=true;
    }
    exploreui::ScopedTextFont fontScope(ownFont);
    const auto flags=d->Flags;d->Flags&=~ImDrawListFlags_AntiAliasedFill;
    if(open) {
        nav::Screen mouse;const bool focus=mousePoint(mouse);const bool down=focus&&(GetAsyncKeyState(VK_LBUTTON)&0x8000);
        if(down&&!leftWasDown){pressPoint={mouse.x,mouse.y,0};pressed=true;}
        const bool click=focus&&!down&&leftWasDown&&pressed&&std::hypot(mouse.x-pressPoint.x,mouse.y-pressPoint.y)<5;
        if(!down)pressed=false;leftWasDown=down;
        const auto rows=explore::rows(snapshot.places,position,tab);const auto bounds=exploreui::layout(size,s,rows.size(),page,tab).panel;
        reserved={1,bounds.x,bounds.y,bounds.w,bounds.h};
        int playerLevel=-1;
        if(!m.raw()->mem.read(m.raw(),player.raw().address+0x190,&playerLevel,sizeof(playerLevel)))playerLevel=-1;
        const auto action=exploreui::panel(d,size,s,snapshot,position,tab,page,mouse,focus,click,selected,target,now<noticeUntil?notice:std::string{},playerLevel);
        if(action.tab>=0){if(tab!=action.tab)nextScan=0;tab=action.tab;page=0;}
        if(action.pageDelta<0&&page>0)--page;else if(action.pageDelta>0)++page;
        if(action.choose)choose(action.place);if(action.clear)clearSelection();
        if(selected&&bridgeState<0) {
            nav::Projection projection;nav::Screen pixel;
            if(mapProjection(gc,position,projection)&&nav::project(projection,target,pixel)) {
                const ImVec2 marker{static_cast<float>(pixel.x/projection.width*size.x),static_cast<float>(pixel.y/projection.height*size.y)};
                if(marker.x>=0&&marker.y>=0&&marker.x<size.x&&marker.y<size.y&&!exploreui::contains(bounds,{marker.x,marker.y}))navui::mapMarker(d,marker,s);
            }
        }
        exploreui::cursor(d,size,s,bounds,mouse,focus);
    } else {
        leftWasDown=pressed=false;
        const auto yaw=m.read<float>(gc+0x1ac);
        if(selected&&bridgeState<0&&std::isfinite(yaw))exploreui::guidance(d,size,s,position,yaw,target,targetLabel);
    }
    d->Flags=flags;
}
}

CUBE_MOD("Region Explorer","0.1.6","CubeWorld Navigation") {
    mod.setId(navbridge::consumer);mod.setCapabilities(cube::Capability::Overlay|cube::Capability::RawMem);
    mod.services().onMessage([](cube::Message& msg){
        if(msg.id()!=navbridge::sidebar||std::strcmp(msg.sender(),navbridge::provider)||msg.size()!=sizeof(reserved)||!msg.payload())return;
        if(reserved.h>0){std::memcpy(msg.payload(),&reserved,sizeof(reserved));msg.reply(1);}else msg.reply(0);
    });
    mod.menu().setToggleKey(0).setOpen(true).onDraw(&draw).setPassthrough(true);
    mod.log.info("region explorer 0.1.6 initialized; guarded boss class readings and limited task-5 predictions; resident records only; no generator calls, game RNG changes or game writes");
}

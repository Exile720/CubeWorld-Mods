#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "cube_mod.hpp"
#include "nav_math.hpp"
#include "nav_ui.hpp"
#include "../region_explorer/explore_font.hpp"
#include "town_scan.hpp"
#include "town_ui.hpp"
#include "nav_bridge.hpp"
#include <cstdint>
#include <cstdio>

namespace {
// All addresses are for the verified 2013 Alpha. They are rebased by the loader.
constexpr unsigned gcGlobal=0x0076b1c8;
constexpr unsigned mapOpenOffset=0x8006e4, mapObjectOffset=0x800d44;
constexpr unsigned mapProjectionOffset=0x4, mapViewOffset=0x44, worldOffset=0x2e4, worldNameOffset=0x94;
constexpr unsigned playerPositionOffset=0x10, actualYawOffset=0x1ac;
constexpr unsigned mapPanOffset=0x1000e4c;
struct FixedPoint {std::int64_t x,y,z;};
struct FloatMatrix {float v[16];};
struct FloatPoint {float x,y,z;};
struct MapFrame {nav::Projection projection; ULONGLONG time=0; unsigned gc=0;};
nav::Point target;
bool hasTarget=false;
std::string currentScope;
std::string notice;
ULONGLONG noticeUntil=0;
bool rightWasDown=false,deleteWasDown=false;
exploreui::TextFont textFont;
bool mapLogged=false;
bool snapshotLogged=false;
bool storageError=false;
std::vector<town::Place> townPlaces;
ULONGLONG nextTownScan=0,nextTownSave=0;
bool townDirty=false,townSaveError=false,townScanLogged=false;

unsigned controller() {return cube::mod().read<unsigned>(cube::mod().rebase(gcGlobal));}
bool nativeMapOpen(unsigned gc) {return gc && cube::mod().read<std::uint8_t>(gc+mapOpenOffset)==1;}
nav::Point point(FixedPoint p){return {p.x/65536.0,p.y/65536.0,p.z/65536.0};}

// Read the completed native map transform at EndScene. The caller at 0x4adf13 uses
// player.fixedPosition + trunc(GC.mapPan * 65536) as origin. WorldMap+0x44 is only
// the view transform; compose perspective at +0x4 before unprojecting a map click.
bool snapshotMap(unsigned gc,nav::Point player,MapFrame& next) {
    auto& mod=cube::mod();
    const unsigned object=gc+mapObjectOffset;
    if(!nativeMapOpen(gc)||!mod.readable(gc+mapPanOffset,sizeof(FloatPoint))||
       !mod.readable(object+mapProjectionOffset,sizeof(FloatMatrix))||
       !mod.readable(object+mapViewOffset,sizeof(FloatMatrix))) return false;
    const auto pan=mod.read<FloatPoint>(gc+mapPanOffset);
    if(!std::isfinite(pan.x)||!std::isfinite(pan.y)||!std::isfinite(pan.z)) return false;
    next.gc=gc;next.time=GetTickCount64();
    next.projection.origin={player.x+std::trunc(static_cast<double>(pan.x)*65536)/65536,
                            player.y+std::trunc(static_cast<double>(pan.y)*65536)/65536,
                            player.z+std::trunc(static_cast<double>(pan.z)*65536)/65536};
    next.projection.width=mod.read<int>(gc+0x11c);
    next.projection.height=mod.read<int>(gc+0x120);
    if(next.projection.width<320||next.projection.height<200) return false;
    const auto rawProjection=mod.read<FloatMatrix>(object+mapProjectionOffset);
    const auto rawView=mod.read<FloatMatrix>(object+mapViewOffset);
    nav::Matrix perspective{},view{};
    for(int i=0;i<16;++i) {perspective[i]=rawProjection.v[i];view[i]=rawView.v[i];}
    return nav::nativeMapProjection(perspective,view,next.projection.origin,
                                    next.projection.width,next.projection.height,next.projection);
}

std::string readWorldName(unsigned gc) {
    auto& mod=cube::mod();const unsigned str=gc+worldOffset+worldNameOffset;
    const auto length=mod.read<unsigned>(str+0x10,0xffffffffu);
    const auto capacity=mod.read<unsigned>(str+0x14,0xffffffffu);
    if(length==0||length>128||capacity<length||capacity>1024) return {};
    const unsigned data=capacity<16?str:mod.read<unsigned>(str);
    if(!data||!mod.readable(data,length+1)) return {};
    std::string name;name.reserve(length);
    for(unsigned i=0;i<length;++i) {
        const char c=mod.read<char>(data+i);
        if(static_cast<unsigned char>(c)<32) return {};
        name.push_back(c);
    }
    return mod.read<char>(data+length)==0?name:std::string{};
}
void showNotice(const char* text){notice=text;noticeUntil=GetTickCount64()+4000;}
void updateWorld(unsigned gc) {
    auto& mod=cube::mod();auto world=mod.world();
    const std::string name=readWorldName(gc);
    if(!world.valid()||!world.hasSeed()||name.empty()) {
        if(!currentScope.empty()){currentScope.clear();hasTarget=false;townPlaces.clear();}
        return;
    }
    const auto scope=nav::worldScope(world.getSeed(),name);
    if(scope==currentScope) return;
    currentScope=scope;hasTarget=false;storageError=false;
    townPlaces.clear();nextTownScan=nextTownSave=0;
    townDirty=townSaveError=townScanLogged=false;
    mod.storage().setScope(scope.c_str());
    if(mod.storage().has("town_index")) {
        const int size=mod.storage().size("town_index");
        if(size>0&&size<=65536) {
            if(!town::decode(mod.storage().getString("town_index"),townPlaces))
                mod.log.warn("town labels: invalid index ignored for %s",scope.c_str());
        }
    }
    if(mod.storage().has("destination")) {
        const auto size=mod.storage().size("destination");
        if(size>0&&size<=256) {
            const auto stored=mod.storage().getString("destination");
            hasTarget=nav::decode(stored,target);
            if(stored.rfind("CW_WAYPOINT_1 ",0)==0) showNotice("地图定位已更新，请重新标记目的地");
        }
        if(!hasTarget) mod.log.warn("waypoint: invalid stored destination ignored for %s",scope.c_str());
    }
    mod.log.info("waypoint: world=%s seed=%u scope=%s restored=%d",name.c_str(),world.getSeed(),scope.c_str(),hasTarget?1:0);
}
bool setTarget(nav::Point p,const char* label=nullptr) {
    if(currentScope.empty()||!nav::finite(p)) {showNotice("世界信息尚未就绪，请稍后再标点");return false;}
    // Commit persistent state first; a failed save never silently replaces the existing marker.
    if(!cube::mod().storage().putString("destination",nav::encode(p))) {
        storageError=true;showNotice("标点保存失败，原标点已保留");return false;
    }
    target=p;hasTarget=true;storageError=false;
    cube::mod().log.info("waypoint: set x=%.6f y=%.6f z=%.6f",p.x,p.y,p.z);
    if(label)showNotice((std::string("已导航至")+label).c_str());
    else showNotice("目的地已标记");
    return true;
}
void refreshTown(unsigned gc,nav::Point player) {
    const auto now=GetTickCount64();auto& mod=cube::mod();
    if(now>=nextTownScan) {
        nextTownScan=now+1500;
        const auto found=town::scan(mod,gc,player);
        size_t added=0;const bool reconciled=town::reconcile(townPlaces,found,added);
        if(reconciled)townDirty=true;
        if(!townScanLogged||reconciled) {
            mod.log.info("town labels: loaded=%u new=%u known=%u reconciled=%d scan=%llu ms",static_cast<unsigned>(found.size()),static_cast<unsigned>(added),static_cast<unsigned>(townPlaces.size()),reconciled?1:0,GetTickCount64()-now);
            townScanLogged=true;
        }
    }
    if(townDirty&&now>=nextTownSave) {
        nextTownSave=now+8000;
        if(mod.storage().putString("town_index",town::encode(townPlaces))) {townDirty=false;townSaveError=false;}
        else if(!townSaveError) {
            townSaveError=true;mod.log.warn("town labels: index save failed; retry scheduled");
            showNotice("店铺位置暂未保存，稍后会重试");
        }
    }
}
void clearTarget() {
    if(!hasTarget||currentScope.empty()) return;
    auto store=cube::mod().storage();
    if(store.has("destination")&&!store.remove("destination")) {
        storageError=true;showNotice("取消标点失败，请重试");return;
    }
    hasTarget=false;storageError=false;showNotice("标点已取消");
    cube::mod().log.info("waypoint: cleared");
}
bool foregroundPoint(nav::Screen& pt) {
    const HWND hwnd=GetForegroundWindow();DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);
    if(pid!=GetCurrentProcessId()) return false;
    POINT p{};RECT r{};
    if(!GetCursorPos(&p)||!ScreenToClient(hwnd,&p)||!GetClientRect(hwnd,&r)||r.right<=0||r.bottom<=0) return false;
    if(p.x<0||p.y<0||p.x>=r.right||p.y>=r.bottom) return false;
    const auto sz=ImGui::GetIO().DisplaySize;
    pt={p.x*static_cast<double>(sz.x)/r.right,p.y*static_cast<double>(sz.y)/r.bottom};return true;
}
void drawNavigation() {
    auto& mod=cube::mod();const unsigned gc=controller();auto player=mod.player();
    if(!gc||!mod.session().isInWorld()||!player.valid()||!player.hasPosition()) {
        currentScope.clear();hasTarget=false;rightWasDown=deleteWasDown=false;return;
    }
    updateWorld(gc);
    if(currentScope.empty()) return; // Keep the game title and character menus free of navigation UI.
    const unsigned posAddress=player.raw().address+playerPositionOffset;
    if(!mod.readable(posAddress,sizeof(FixedPoint))) return;
    const nav::Point position=point(mod.read<FixedPoint>(posAddress));
    if(!nav::finite(position)) return;
    const double yaw=mod.read<float>(gc+actualYawOffset);
    if(!std::isfinite(yaw)) return;
    auto* draw=ImGui::GetForegroundDrawList();const auto size=ImGui::GetIO().DisplaySize;
    if(size.x<320||size.y<200) return;
    exploreui::ScopedTextFont fontScope(textFont.ensure(ImGui::GetFont()));
    const float scale=std::clamp(size.y/1080.0f,0.70f,1.45f);
    const auto previousFlags=draw->Flags;
    draw->Flags&=~ImDrawListFlags_AntiAliasedFill;
    navui::compass(draw,position,yaw,size.x,scale,hasTarget,target,town::targetName(townPlaces,target));
    nav::Screen mouse;const bool focus=foregroundPoint(mouse);
    const bool right=focus&&(GetAsyncKeyState(VK_RBUTTON)&0x8000);
    const bool del=focus&&(GetAsyncKeyState(VK_DELETE)&0x8000);
    const bool rightPressed=right&&!rightWasDown,deletePressed=del&&!deleteWasDown;
    rightWasDown=right;deleteWasDown=del;
    const bool mapOpen=nativeMapOpen(gc);bool shopCursor=false;
    if(mapOpen) {
        if(!mapLogged){mod.log.info("waypoint: native map opened (GC+0x8006e4)");mapLogged=true;}
        MapFrame frame;const bool ready=snapshotMap(gc,position,frame);
        const char* state=storageError?"标点保存失败":currentScope.empty()?"正在识别当前世界":"滚轮缩放  拖动查看";
        navui::mapHelp(draw,size,scale,ready,state,storageError);
        if(ready) {
            refreshTown(gc,position);
            town::Rect arrowBounds{};bool reserveArrow=false;
            nav::Screen markerPixel;
            if(hasTarget&&nav::project(frame.projection,target,markerPixel)) {
                const ImVec2 marker(static_cast<float>(markerPixel.x/frame.projection.width*size.x),static_cast<float>(markerPixel.y/frame.projection.height*size.y));
                if(marker.x>0&&marker.y>0&&marker.x<size.x&&marker.y<size.y) {
                    const auto bounds=navui::mapMarkerBounds(marker,scale);
                    arrowBounds={bounds.x,bounds.y,bounds.z,bounds.w};reserveArrow=true;
                }
            }
            navbridge::Sidebar sidebar;town::Rect extra{};bool reservePanel=false;
            if(mod.services().sendMessage(navbridge::consumer,navbridge::sidebar,&sidebar,sizeof(sidebar))==1&&
               sidebar.version==1&&std::isfinite(sidebar.x)&&std::isfinite(sidebar.y)&&std::isfinite(sidebar.w)&&std::isfinite(sidebar.h)&&
               sidebar.x>=0&&sidebar.y>=0&&sidebar.w>0&&sidebar.h>0&&sidebar.x+sidebar.w<=size.x&&sidebar.y+sidebar.h<=size.y) {
                extra={sidebar.x,sidebar.y,sidebar.w,sidebar.h};reservePanel=true;
            }
            const bool overExplorer=reservePanel&&focus&&town::contains(extra,mouse);
            const auto townHit=townui::draw(draw,size,scale,frame.projection,townPlaces,position,mouse,focus,reserveArrow?&arrowBounds:nullptr,reservePanel?&extra:nullptr);
            shopCursor=townHit.panel;
            if(!snapshotLogged) {
                mod.log.info("waypoint: map projection=%s origin=%.3f,%.3f,%.3f viewport=%.0fx%.0f",frame.projection.pixels?"pixels":"clip",frame.projection.origin.x,frame.projection.origin.y,frame.projection.origin.z,frame.projection.width,frame.projection.height);
                for(int i=0;i<4;++i) mod.log.info("waypoint: matrix[%d] %.8g %.8g %.8g %.8g",i,frame.projection.matrix[i],frame.projection.matrix[i+4],frame.projection.matrix[i+8],frame.projection.matrix[i+12]);
                snapshotLogged=true;
            }
            if(rightPressed&&townHit.place) {
                if(setTarget(townHit.target.position,town::name(townHit.target.type)))
                    mod.log.info("town labels: selected %s type=%d exact x=%.6f y=%.6f",town::name(townHit.target.type),townHit.target.type,townHit.target.position.x,townHit.target.position.y);
            } else if(rightPressed&&!townHit.panel&&!overExplorer) {
                nav::Point candidate;
                const nav::Screen pixel{mouse.x*frame.projection.width/size.x,mouse.y*frame.projection.height/size.y};
                // The map uses compressed terrain relief. Keep a fixed horizontal anchor for direction.
                if(nav::pick(frame.projection,pixel,frame.projection.origin.z,candidate)) {
                    if(setTarget(candidate)) mod.log.info("waypoint: click %.1f,%.1f initial distance=%.2f",pixel.x,pixel.y,nav::distance(position,candidate));
                }
                else showNotice("这个位置暂时无法标记，请在地图中间重试");
            }
            if(hasTarget) {
                nav::Screen pixel;
                if(nav::project(frame.projection,target,pixel)) {
                    const ImVec2 marker(static_cast<float>(pixel.x/frame.projection.width*size.x),static_cast<float>(pixel.y/frame.projection.height*size.y));
                    if(marker.x>0&&marker.y>0&&marker.x<size.x&&marker.y<size.y) {
                        navui::mapMarker(draw,marker,scale);
                    }
                }
            }
        }
        if(deletePressed) clearTarget();
    } else {mapLogged=false;snapshotLogged=false;}
    if(!notice.empty()&&GetTickCount64()<noticeUntil) navui::notice(draw,size.x,scale,notice.c_str());
    if(shopCursor)navui::handCursor(draw,size,scale,mouse,focus);
    draw->Flags=previousFlags;
}
}

CUBE_MOD("Waypoint Navigation","0.3.8","CubeWorld Navigation") {
    mod.setId("local.cubeworld.waypoint");
    mod.setCapabilities(cube::Capability::Overlay|cube::Capability::RawMem);
    mod.services().onMessage([](cube::Message& msg) {
        if((msg.id()!=navbridge::select&&msg.id()!=navbridge::status&&msg.id()!=navbridge::clear)||
           std::strcmp(msg.sender(),navbridge::consumer)||msg.size()!=sizeof(navbridge::Request)||!msg.payload())return;
        auto& m=cube::mod();const auto gc=controller();
        if(!gc||!m.session().isInWorld()) {msg.reply(0);return;}
        updateWorld(gc);navbridge::Request r;
        if(!m.raw()->mem.read(m.raw(),static_cast<unsigned>(reinterpret_cast<uintptr_t>(msg.payload())),&r,sizeof(r))||
           currentScope.empty()||!navbridge::valid(r,currentScope)){msg.reply(0);return;}
        if(msg.id()==navbridge::status){msg.reply(hasTarget&&nav::distance(target,r.point)<1?1:0);return;}
        if(msg.id()==navbridge::clear){if(hasTarget&&nav::distance(target,r.point)<1)clearTarget();msg.reply(hasTarget&&nav::distance(target,r.point)<1?0:1);return;}
        msg.reply(setTarget(r.point,r.label)?1:0);
    });
    mod.menu().setToggleKey(0).setOpen(true).onDraw(&drawNavigation).setPassthrough(true);
    mod.log.info("waypoint 0.3.8 initialized; local shop-sidebar cursor; optional Region Explorer navigation; no gameplay writes");
}

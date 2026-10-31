#pragma once
#include "explore.hpp"
#include "nav_ui.hpp"
#include "nav_bridge.hpp"
#include "difficulty.hpp"
#include "boss.hpp"

namespace exploreui {
struct Rect {float x,y,w,h;};
inline bool contains(Rect r,nav::Screen p) {return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.w&&p.y<r.y+r.h;}
struct Layout {Rect panel{};float rowStep=46;int capacity=1;size_t page=0,shown=0,pages=1;};
inline Layout layout(ImVec2 size,float s,size_t rows,size_t page,int tab=0) {
    Layout l;l.panel={18*s,110*s,344*s,0};
    l.rowStep=tab==1?62.f:46.f;
    l.capacity=std::clamp(static_cast<int>((size.y-l.panel.y-112*s-164*s)/(l.rowStep*s)),1,10);
    l.pages=std::max(size_t{1},(rows+l.capacity-1)/l.capacity);l.page=std::min(page,l.pages-1);
    l.shown=std::min(static_cast<size_t>(l.capacity),rows-l.page*l.capacity);
    l.panel.h=(164+l.rowStep*static_cast<float>(std::max(size_t{2},l.shown)))*s;return l;
}
struct Action {int tab=-1,pageDelta=0;bool choose=false,clear=false;explore::Place place{};};
inline void clipped(ImDrawList* d,ImVec2 pos,float width,const char* label,ImU32 color,float size) {
    d->PushClipRect(pos,{pos.x+width,pos.y+size+5},true);navui::text(d,pos,label,color,size);d->PopClipRect();
}
inline Action panel(ImDrawList* d,ImVec2 size,float s,const explore::Snapshot& snapshot,
                    nav::Point player,int tab,size_t& page,nav::Screen mouse,bool focused,bool click,
                    bool selected,nav::Point target,const std::string& notice,int playerLevel=-1) {
    Action action;const auto rows=explore::rows(snapshot.places,player,tab);
    const auto l=layout(size,s,rows.size(),page,tab);page=l.page;
    const float x=l.panel.x,y=l.panel.y,w=l.panel.w,h=l.panel.h;
    navui::panel(d,{x,y},{x+w,y+h},s);
    navui::text(d,navui::at(x+14*s,y+13*s),"区域探索",navui::gold,18*s);
    navui::text(d,navui::at(x+w-72*s,y+17*s),"测试版",navui::muted,11*s);
    const auto count=explore::counts(rows);
    char summary[128];
    if(tab==1)std::snprintf(summary,sizeof(summary),"周边记录 %u · 完成状态 %u",count.total,count.done);
    else std::snprintf(summary,sizeof(summary),"周边%s %u · 白低 / 青近 / 红高",tab==0?"城镇":"地标",count.total);
    clipped(d,navui::at(x+14*s,y+39*s),w-28*s,summary,navui::muted,12*s);
    const char* tabs[]={"城镇","挑战","地标"};
    for(int i=0;i<3;++i) {
        const Rect r{x+(12+i*107.0f)*s,y+61*s,103*s,27*s};
        const bool over=focused&&contains(r,mouse);
        navui::panel(d,{r.x,r.y},{r.x+r.w,r.y+r.h},s,i==tab?IM_COL32(80,77,47,255):navui::blue,i==tab?navui::gold:over?navui::cream:navui::muted);
        navui::centered(d,navui::at(r.x+r.w/2,r.y+7*s),tabs[i],i==tab?navui::gold:navui::cream,13*s);
        if(over&&click)action.tab=i;
    }
    for(size_t i=0;i<l.shown;++i) {
        const auto& p=rows[l.page*l.capacity+i];const float ry=y+(96+l.rowStep*static_cast<float>(i))*s;
        const Rect row{x+10*s,ry,w-20*s,(l.rowStep-2)*s};const bool over=focused&&contains(row,mouse);
        const bool active=selected&&nav::distance(target,p.position)<1;
        d->AddRectFilled({row.x,row.y},{row.x+row.w,row.y+row.h},over?IM_COL32(66,91,105,245):IM_COL32(20,39,56,160));
        const auto difficulty=explore::difficulty(p.level,playerLevel,p.city());
        const auto color=explore::difficultyColor(difficulty);
        if(active)d->AddRect({row.x,row.y},{row.x+row.w,row.y+row.h},navui::gold,0,0,std::max(1.0f,s));
        const auto title=explore::title(p);
        clipped(d,navui::at(x+18*s,ry+5*s),w-37*s,title.c_str(),color,15*s);
        char detail[96];
        if(p.city())std::snprintf(detail,sizeof(detail),"城镇");
        else std::snprintf(detail,sizeof(detail),"%s · %s",explore::kind(p),explore::difficultyLabel(difficulty));
        clipped(d,navui::at(x+18*s,ry+26*s),w-111*s,detail,p.done()?navui::green:navui::muted,11.5f*s);
        char distance[48];std::snprintf(distance,sizeof(distance),"%.0f m",nav::distance(player,p.position));
        navui::text(d,navui::at(x+w-18*s-navui::measure(distance,12*s).x,ry+25*s),distance,navui::gold,12*s);
        if(tab==1) {
            const auto label=explore::bossLabel(p);
            const auto bossColor=p.boss.source==explore::BossSource::Confirmed||p.boss.source==explore::BossSource::Special?navui::cream:
                p.boss.source==explore::BossSource::Predicted?navui::gold:navui::muted;
            clipped(d,navui::at(x+18*s,ry+43*s),w-37*s,label.c_str(),bossColor,11.5f*s);
        }
        if(over&&click){action.choose=true;action.place=p;}
    }
    if(rows.empty()) {
        navui::text(d,navui::at(x+18*s,y+111*s),"暂无可读取的记录",navui::cream,14*s);
        navui::text(d,navui::at(x+18*s,y+137*s),"稍候再看；未列出不代表没有",navui::muted,11.5f*s);
    }
    const float footer=y+h-62*s;
    for(int i=0;i<2;++i) {
        const Rect r{x+(i?245:13)*s,footer,86*s,25*s};const bool over=focused&&contains(r,mouse);
        navui::panel(d,{r.x,r.y},{r.x+r.w,r.y+r.h},s,navui::blue,over?navui::gold:navui::muted);
        navui::centered(d,navui::at(r.x+r.w/2,r.y+6*s),i?"下一页":"上一页",navui::cream,11.5f*s);
        if(over&&click&&((!i&&page>0)||(i&&page+1<l.pages)))action.pageDelta=i?1:-1;
    }
    char paging[32];std::snprintf(paging,sizeof(paging),"%u / %u",static_cast<unsigned>(page+1),static_cast<unsigned>(l.pages));
    navui::centered(d,navui::at(x+w/2,footer+7*s),paging,navui::muted,11*s);
    const Rect clear{x+13*s,footer+30*s,110*s,22*s};
    if(selected) {
        const bool over=focused&&contains(clear,mouse);
        navui::text(d,navui::at(clear.x+4*s,clear.y+3*s),"取消探索目标",over?navui::gold:navui::muted,11*s);
        if(over&&click)action.clear=true;
    }
    clipped(d,navui::at(x+(selected?132:16)*s,footer+33*s),w-(selected?148:32)*s,
        notice.empty()?"左键导航 · 周边记录，非全部历史":notice.c_str(),navui::muted,10.5f*s);
    return action;
}
inline void guidance(ImDrawList* d,ImVec2 size,float s,nav::Point player,double yaw,nav::Point target,const std::string& name) {
    const float w=320*s,x=size.x-w-18*s,y=size.y-108*s;
    navui::panel(d,{x,y},{x+w,y+81*s},s,navui::blue,navui::gold);
    clipped(d,navui::at(x+14*s,y+12*s),w-28*s,name.c_str(),navui::cream,15*s);
    const auto delta=nav::relativeBearing(player,target,yaw);const auto distance=nav::distance(player,target);
    const char* direction=distance<30?"已接近目标区域":std::abs(delta)>150?"向后转":std::abs(delta)<20?"向前走":delta>0?"向右转":"向左转";
    navui::text(d,navui::at(x+14*s,y+39*s),direction,distance<30?navui::green:navui::gold,14*s);
    char text[48];std::snprintf(text,sizeof(text),"%.0f m",distance);
    navui::text(d,navui::at(x+w-16*s-navui::measure(text,18*s).x,y+36*s),text,navui::gold,18*s);
}
// The native game paints its hand before EndScene. Our opaque panel covers it.
// Paint a local pixel hand last, using exactly the same hotspot as our hit tests.
// This does not change the OS cursor, ImGui input, or any other Mod's cursor state.
inline bool cursor(ImDrawList* d,ImVec2 size,float s,Rect panel,nav::Screen mouse,bool focused) {
    if(!focused||!contains(panel,mouse)||mouse.x<0||mouse.y<0||mouse.x>=size.x||mouse.y>=size.y)return false;
    static constexpr const char* pixels[]={
        ".....###........",
        "....#WWW#.......",
        "....#WWW#.......",
        "....#WWW#.......",
        "....#WWW####....",
        "....#WWW#WW##...",
        "....#WWW#WW#W#..",
        "....#WWW#WW#W#..",
        "....#WWWWWWWW#..",
        ".###.#WWWWWWW#..",
        "#WWW##WWWWWWW#..",
        "#WWWWWWWWWWWW#..",
        ".#WWWWWWWWWWW#..",
        "..#WWWWWWWWWW#..",
        "...#WWWWWWWWW#..",
        "....#WWWWWWW#...",
        ".....#WWWWWW#...",
        ".....########..."};
    const float u=std::max(1.0f,std::round(1.5f*s));
    const float x=std::round(static_cast<float>(mouse.x))-6*u,y=std::round(static_cast<float>(mouse.y));
    d->PushClipRect({0,0},size,true);
    for(int row=0;row<18;++row)for(int col=0;col<16;++col)if(pixels[row][col]!='.')
        d->AddRectFilled({x+(col+1)*u,y+(row+1)*u},{x+(col+2)*u,y+(row+2)*u},IM_COL32(0,0,0,150));
    for(int row=0;row<18;++row)for(int col=0;col<16;++col)if(pixels[row][col]!='.')
        d->AddRectFilled({x+col*u,y+row*u},{x+(col+1)*u,y+(row+1)*u},pixels[row][col]=='#'?IM_COL32(13,19,24,255):IM_COL32(255,252,237,255));
    d->PopClipRect();return true;
}
}

#pragma once
#include "imgui.h"
#include "nav_math.hpp"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace navui {
constexpr ImU32 ink=IM_COL32(20,24,30,255),rim=IM_COL32(236,241,224,255);
constexpr ImU32 blue=IM_COL32(34,58,77,232),gold=IM_COL32(255,216,106,255);
constexpr ImU32 cream=IM_COL32(255,246,216,255),muted=IM_COL32(188,211,210,255);
constexpr ImU32 green=IM_COL32(166,222,121,255),error=IM_COL32(255,167,132,255);
inline float snap(float x){return std::round(x);}
inline ImVec2 at(float x,float y){return {snap(x),snap(y)};}

// Seven non-overlapping strips produce three square corner steps, with no rounded geometry.
inline void stepped(ImDrawList* d,ImVec2 a,ImVec2 b,float p,ImU32 color) {
    if(b.x-a.x<8*p||b.y-a.y<6*p) {d->AddRectFilled(a,b,color);return;}
    for(int i=0;i<3;++i) {
        const float inset=(3-i)*p;
        d->AddRectFilled(at(a.x+inset,a.y+i*p),at(b.x-inset,a.y+(i+1)*p),color);
        d->AddRectFilled(at(a.x+inset,b.y-(i+1)*p),at(b.x-inset,b.y-i*p),color);
    }
    d->AddRectFilled(at(a.x,a.y+3*p),at(b.x,b.y-3*p),color);
}
inline void panel(ImDrawList* d,ImVec2 a,ImVec2 b,float s,ImU32 fill=blue,ImU32 edge=rim) {
    const float p=std::max(1.0f,snap(s));a=at(a.x,a.y);b=at(b.x,b.y);
    stepped(d,at(a.x+p,a.y+3*p),at(b.x+p,b.y+3*p),p,IM_COL32(12,19,27,150));
    stepped(d,a,b,p,ink);
    stepped(d,at(a.x+p,a.y+p),at(b.x-p,b.y-p),p,edge);
    stepped(d,at(a.x+3*p,a.y+3*p),at(b.x-3*p,b.y-3*p),p,ink);
    stepped(d,at(a.x+4*p,a.y+4*p),at(b.x-4*p,b.y-4*p),p,fill);
    d->AddRectFilled(at(a.x+7*p,a.y+4*p),at(b.x-7*p,a.y+5*p),IM_COL32(127,157,161,150));
}
inline ImVec2 measure(const char* label,float size) {
    return ImGui::GetFont()->CalcTextSizeA((std::max)(1.f,std::round(size)),FLT_MAX,0,label);
}
inline void text(ImDrawList* d,ImVec2 pos,const char* label,ImU32 color,float size) {
    pos=at(pos.x,pos.y);auto* font=ImGui::GetFont();
    size=(std::max)(1.f,std::round(size));
    // Keep the game's own face, with one quiet shadow instead of eight outlines.
    d->AddText(font,size,at(pos.x+1,pos.y+1),IM_COL32(20,24,30,145),label);
    d->AddText(font,size,pos,color,label);
}
inline void centered(ImDrawList* d,ImVec2 pos,const char* label,ImU32 color,float size) {
    text(d,at(pos.x-measure(label,size).x/2,pos.y),label,color,size);
}
inline float keycap(ImDrawList* d,ImVec2 pos,const char* key,float s) {
    const float size=12*s,w=std::max(20*s,measure(key,size).x+12*s);
    panel(d,pos,at(pos.x+w,pos.y+22*s),s,IM_COL32(82,104,116,255));
    centered(d,at(pos.x+w/2,pos.y+4*s),key,cream,size);
    return w;
}
// A tiny three-faced voxel, ending at the clicked point. No textures/assets are required.
inline void cubePin(ImDrawList* d,ImVec2 point,float s) {
    static const char* rows[]={
        "....###.....","..##www##...","##wwwwggg##.","#wwwwggggg#.",
        "#ww#gggggg#.","#ww#gggggg#.","#ww#gggg++#.",".#w#gg++##..",
        "..##++##....","....##......","....##......",".....#......"};
    const float u=std::max(1.0f,snap(2*s)),x=snap(point.x-5.5f*u),y=snap(point.y-12*u);
    for(int row=0;row<12;++row) for(int col=0;col<12;++col) {
        const char c=rows[row][col];if(c=='.') continue;
        const ImU32 color=c=='#'?ink:c=='w'?IM_COL32(255,245,179,255):c=='+'?IM_COL32(203,143,54,255):gold;
        d->AddRectFilled(at(x+col*u,y+row*u),at(x+(col+1)*u,y+(row+1)*u),color);
    }
}
inline void chevron(ImDrawList* d,ImVec2 p,float s,int direction,ImU32 color=gold) {
    const float u=std::max(1.0f,snap(s));
    for(int row=-3;row<=3;++row) {
        const float x=p.x+direction*(3-std::abs(row))*u;
        d->AddRectFilled(at(x-u,p.y+row*u),at(x+u,p.y+(row+1)*u),ink);
        d->AddRectFilled(at(x,p.y+row*u),at(x+u,p.y+(row+1)*u),color);
    }
}
inline void compass(ImDrawList* d,nav::Point player,double yaw,float width,float s,
                    bool hasTarget,nav::Point target,const char* targetLabel="目的地") {
    const float cx=snap(width/2),w=std::min(width*0.40f,490*s),half=w/2-18*s;
    const float top=12*s,bottom=top+43*s;
    panel(d,at(cx-w/2,top),at(cx+w/2,bottom),s);
    const double heading=nav::wrap(-yaw);
    const char* directions[]={"北","东北","东","东南","南","西南","西","西北"};
    d->PushClipRect(at(cx-w/2+9*s,top+5*s),at(cx+w/2-9*s,bottom-4*s),true);
    for(int i=0;i<24;++i) {
        const double relative=nav::wrap(i*15.0-heading);
        if(std::abs(relative)>90) continue;
        const float x=cx+static_cast<float>(relative/90)*half;
        const bool major=i%3==0;
        d->AddRectFilled(at(x-s,top+(major?29:32)*s),at(x+s,top+36*s),major?rim:muted);
        if(major) centered(d,at(x,top+9*s),directions[i/3],i==0?gold:cream,14*s);
    }
    d->PopClipRect();
    const float p=std::max(1.0f,snap(s));
    // Fixed center needle and a moving golden voxel show heading and destination separately.
    for(int i=0;i<4;++i) d->AddRectFilled(at(cx-(4-i)*p,top+(3+i)*p),at(cx+(4-i)*p,top+(4+i)*p),gold);
    if(hasTarget) {
        const double delta=nav::relativeBearing(player,target,yaw);
        const float x=cx+static_cast<float>(std::clamp(delta,-90.0,90.0)/90)*half;
        cubePin(d,at(x,bottom-4*s),0.5f*s);
        if(std::abs(delta)>90) chevron(d,at(x+(delta>0?9:-9)*s,top+31*s),s,delta>0?1:-1);
        const double distance=nav::distance(player,target);
        char number[64];std::snprintf(number,sizeof(number),"%.0f m",distance);
        const char* label=distance<20?"快到了":targetLabel;
        const float labelSize=13*s,numberSize=20*s;
        const float tagWidth=measure(label,labelSize).x+measure(number,numberSize).x+35*s;
        const float tagTop=bottom+3*s;
        panel(d,at(cx-tagWidth/2,tagTop),at(cx+tagWidth/2,tagTop+31*s),s,blue,distance<20?green:gold);
        const float left=cx-tagWidth/2+12*s;
        text(d,at(left,tagTop+10*s),label,muted,labelSize);
        text(d,at(left+measure(label,labelSize).x+10*s,tagTop+5*s),number,distance<20?green:gold,numberSize);
    } else {
        const float hintWidth=147*s,hintTop=bottom+3*s;
        panel(d,at(cx-hintWidth/2,hintTop),at(cx+hintWidth/2,hintTop+29*s),s);
        const float x=cx-hintWidth/2+8*s;
        keycap(d,at(x,hintTop+3*s),"M",s);
        text(d,at(x+28*s,hintTop+8*s),"打开地图标点",muted,13*s);
    }
}
inline void mapHelp(ImDrawList* d,ImVec2 size,float s,bool ready,const char* state,bool failure) {
    const float x=18*s,y=size.y-90*s,w=std::min(size.x-36*s,365*s);
    panel(d,at(x,y),at(x+w,y+72*s),s,blue,failure?error:rim);
    cubePin(d,at(x+23*s,y+33*s),0.65f*s);
    text(d,at(x+43*s,y+11*s),ready?"地图标点":"正在定位地图…",cream,17*s);
    if(ready) {
        const float keyWidth=keycap(d,at(x+140*s,y+9*s),"右键",s);
        text(d,at(x+148*s+keyWidth,y+14*s),"标记目的地",gold,14*s);
    }
    const float keyWidth=keycap(d,at(x+13*s,y+40*s),"Delete",s);
    text(d,at(x+20*s+keyWidth,y+45*s),"取消",muted,13*s);
    text(d,at(x+132*s,y+45*s),state,failure?error:muted,12*s);
}
inline float mapMarkerPixel(float s) {return std::clamp(2.1f*s,1.5f,3.0f);}
inline ImVec4 mapMarkerBounds(ImVec2 point,float s) {
    const float u=mapMarkerPixel(s);
    return {point.x-6.5f*u,point.y-13*u,13*u,13*u};
}
inline void mapMarker(ImDrawList* d,ImVec2 point,float s) {
    // The heavier arrow sits above the destination and its bottom tip lands on it.
    // Existing names avoid these bounds; selection never adds a second nameplate.
    static const char* rows[]={
        "...#######...","...#ggggg#...","...#ggggg#...","...#ggggg#...",
        "...#ggggg#...","####ggggg####","#ggggggggggg#",".#ggggggggg#.",
        "..#ggggggg#..","...#ggggg#...","....#ggg#....",".....#g#.....",
        "......#......"};
    const float u=mapMarkerPixel(s);const auto bounds=mapMarkerBounds(point,s);
    const float x=bounds.x,y=bounds.y;
    for(int row=0;row<13;++row) for(int col=0;col<13;++col) {
        const char c=rows[row][col];if(c=='.')continue;
        d->AddRectFilled(at(x+col*u,y+row*u),at(x+(col+1)*u,y+(row+1)*u),c=='#'?ink:gold);
    }
}
// The native hand is drawn before the overlay. Draw this only over our opaque
// shop sidebar, last, at the same pixel position used for selecting its rows.
inline bool handCursor(ImDrawList* d,ImVec2 size,float s,nav::Screen mouse,bool focused) {
    if(!focused||!std::isfinite(mouse.x)||!std::isfinite(mouse.y)||mouse.x<0||mouse.y<0||mouse.x>=size.x||mouse.y>=size.y)return false;
    static constexpr const char* pixels[]={
        ".....###........","....#WWW#.......","....#WWW#.......","....#WWW#.......",
        "....#WWW####....","....#WWW#WW##...","....#WWW#WW#W#..","....#WWW#WW#W#..",
        "....#WWWWWWWW#..",".###.#WWWWWWW#..","#WWW##WWWWWWW#..","#WWWWWWWWWWWW#..",
        ".#WWWWWWWWWWW#..","..#WWWWWWWWWW#..","...#WWWWWWWWW#..","....#WWWWWWW#...",
        ".....#WWWWWW#...",".....########..."};
    const float u=std::max(1.0f,std::round(1.5f*s));
    const float x=std::round(static_cast<float>(mouse.x))-6*u,y=std::round(static_cast<float>(mouse.y));
    d->PushClipRect({0,0},size,true);
    for(int row=0;row<18;++row)for(int col=0;col<16;++col)if(pixels[row][col]!='.')
        d->AddRectFilled({x+(col+1)*u,y+(row+1)*u},{x+(col+2)*u,y+(row+2)*u},IM_COL32(0,0,0,150));
    for(int row=0;row<18;++row)for(int col=0;col<16;++col)if(pixels[row][col]!='.')
        d->AddRectFilled({x+col*u,y+row*u},{x+(col+1)*u,y+(row+1)*u},pixels[row][col]=='#'?IM_COL32(13,19,24,255):IM_COL32(255,252,237,255));
    d->PopClipRect();return true;
}
inline void notice(ImDrawList* d,float width,float s,const char* message) {
    const float w=std::min(width-32*s,measure(message,15*s).x+30*s),y=106*s;
    panel(d,at((width-w)/2,y),at((width+w)/2,y+34*s),s,blue,gold);
    centered(d,at(width/2,y+10*s),message,cream,15*s);
}
}

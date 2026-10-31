#pragma once
#include "grouping.hpp"
#include "nav_ui.hpp"
#include <cmath>

namespace inventoryui {
struct Rect {float x,y,w,h;};
inline bool screenBounds(const std::array<float,4>& local,const std::array<float,16>& matrix,Rect& out) {
    for(float v:local)if(!std::isfinite(v))return false;
    for(float v:matrix)if(!std::isfinite(v))return false;
    if(local[2]<=local[0]||local[3]<=local[1])return false;
    float left=16384,top=16384,right=-16384,bottom=-16384;
    for(int i=0;i<4;++i) {
        const float x=local[(i&1)?2:0],y=local[(i&2)?3:1];
        const float w=matrix[3]*x+matrix[7]*y+matrix[15];
        if(w<=0.000001f)return false;
        const float sx=(matrix[0]*x+matrix[4]*y+matrix[12])/w;
        const float sy=(matrix[1]*x+matrix[5]*y+matrix[13])/w;
        if(!std::isfinite(sx)||!std::isfinite(sy)||std::abs(sx)>16384||std::abs(sy)>16384)return false;
        left=std::min(left,sx);top=std::min(top,sy);right=std::max(right,sx);bottom=std::max(bottom,sy);
    }
    if(right-left<8||bottom-top<8||right-left>16384||bottom-top>16384)return false;
    out={std::floor(left)-3,std::floor(top)-3,std::ceil(right)-std::floor(left)+6,
         std::ceil(bottom)-std::floor(top)+6};
    return true;
}
struct Frame {
    unsigned widget=0,gc=0;
    int page=0,columns=0,firstRow=0,visibleRows=0,cellW=0,cellH=0;
    float x=0,y=0,width=0,height=0;
    std::vector<inventory::Section> sections;
    std::vector<Rect> occlusions;
    bool valid=false;
};
inline bool geometry(Frame& f) {
    if(!std::isfinite(f.x)||!std::isfinite(f.y)||!std::isfinite(f.width)||!std::isfinite(f.height)||
       std::abs(f.x)>16384||std::abs(f.y)>16384||f.width<30||f.width>8192||f.height<50||f.height>8192||
       f.cellW<8||f.cellW>512||f.cellH<8||f.cellH>512)return false;
    f.columns=static_cast<int>((f.width-10)/(f.cellW+5));
    f.visibleRows=static_cast<int>((f.height-40)/(f.cellH+5));
    return f.columns>0&&f.columns<=32&&f.visibleRows>0&&f.visibleRows<=128;
}
inline bool intersects(const Rect& a,const Rect& b) {
    return a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h;
}
inline std::vector<Rect> visibleParts(Rect area,const std::vector<Rect>& occlusions) {
    std::vector<Rect> parts{area};
    for(const auto& blocked:occlusions) {
        std::vector<Rect> next;
        for(const auto& p:parts) {
            if(!intersects(p,blocked)){next.push_back(p);continue;}
            const float left=std::max(p.x,blocked.x),right=std::min(p.x+p.w,blocked.x+blocked.w);
            const float top=std::max(p.y,blocked.y),bottom=std::min(p.y+p.h,blocked.y+blocked.h);
            if(top>p.y)next.push_back({p.x,p.y,p.w,top-p.y});
            if(bottom<p.y+p.h)next.push_back({p.x,bottom,p.w,p.y+p.h-bottom});
            if(left>p.x)next.push_back({p.x,top,left-p.x,bottom-top});
            if(right<p.x+p.w)next.push_back({right,top,p.x+p.w-right,bottom-top});
        }
        parts.swap(next);
    }
    return parts;
}
inline bool occluded(const Frame& f,float x,float y) {
    for(const auto& r:f.occlusions)if(x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h)return true;
    return false;
}
inline Rect headerRect(const Frame& f,const inventory::Section& s) {
    const float top=f.y+40+(s.header/f.columns-f.firstRow)*(f.cellH+5.0f);
    return {f.x+10,top,f.columns*(f.cellW+5.0f)-5,f.cellH*1.0f};
}
inline bool headerVisible(const Frame& f,const inventory::Section& s) {
    const int row=s.header/f.columns;return row>=f.firstRow&&row<f.firstRow+f.visibleRows;
}
inline bool draw(ImDrawList* d,const Frame& f) {
    if(!d||!f.valid||f.columns<1||f.visibleRows<1||f.firstRow<0)return false;
    const ImVec2 clipMin{f.x+10,f.y+40},clipMax{f.x+10+f.columns*(f.cellW+5.0f)-5,f.y+40+f.visibleRows*(f.cellH+5.0f)-5};
    d->PushClipRect(clipMin,clipMax,true);
    const float nativeSize=std::clamp(f.cellH*.27f,13.0f,24.0f),font=std::round(nativeSize),scale=std::max(1.0f,std::round(nativeSize/16));
    bool painted=false;
    for(const auto& section:f.sections)if(headerVisible(f,section)) {
        const auto r=headerRect(f,section);
        // Native details are already on the framebuffer before the loader's overlay.
        // Excluding their actual rectangles gives them priority without repainting them.
        for(const auto& part:visibleParts({r.x-2,r.y-2,r.w+4,r.h+5},f.occlusions)) {
            d->PushClipRect({part.x,part.y},{part.x+part.w,part.y+part.h},true);
            d->AddRectFilled({r.x-2,r.y-2},{r.x+r.w+2,r.y+r.h+3},IM_COL32(23,33,16,248));
            d->AddRectFilled({r.x,r.y+r.h-2*scale},{r.x+r.w,r.y+r.h},IM_COL32(180,161,89,210));
            d->AddRectFilled({r.x+7*scale,r.y+(r.h-9*scale)/2},{r.x+12*scale,r.y+(r.h+9*scale)/2},IM_COL32(231,205,124,255));
            d->AddText(ImGui::GetFont(),font,{std::round(r.x+22*scale),std::round(r.y+(r.h-font)/2)},IM_COL32(246,228,176,255),inventory::sectionName(section.type));
            d->PopClipRect();painted=true;
        }
    }
    if(f.sections.empty())for(const auto& part:visibleParts({clipMin.x,clipMin.y,clipMax.x-clipMin.x,clipMax.y-clipMin.y},f.occlusions)) {
        d->PushClipRect({part.x,part.y},{part.x+part.w,part.y+part.h},true);
        d->AddText(ImGui::GetFont(),font,{std::round(clipMin.x+8),std::round(clipMin.y+12)},IM_COL32(240,230,197,255),"背包为空");
        d->PopClipRect();
    }
    d->PopClipRect();return painted;
}
}

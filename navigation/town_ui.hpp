#pragma once
#include "nav_ui.hpp"
#include "town.hpp"
#include "town_guide.hpp"

namespace townui {
struct Hit {bool panel=false,place=false;town::Place target{};};
inline Hit draw(ImDrawList* d,ImVec2 size,float s,const nav::Projection& projection,
                const std::vector<town::Place>& places,nav::Point player,nav::Screen mouse,bool focused,
                const town::Rect* markerBounds=nullptr,const town::Rect* extraPanel=nullptr) {
    Hit hit;auto rows=town::nearby(places,player);
    const float w=336*s,x=size.x-w-16*s,y=112*s;
    const int capacity=std::max(1,static_cast<int>((size.y-y-76*s)/(62*s)));
    const size_t shown=std::min(rows.size(),static_cast<size_t>(capacity));
    const float h=(rows.empty()?115.0f:74.0f+62*static_cast<float>(shown))*s;
    const town::Rect sidebar{x,y,w,h};
    if(focused&&town::contains(sidebar,mouse))hit.panel=true;
    std::vector<town::Rect> occupied={sidebar,{0,0,size.x,98*s},{0,size.y-94*s,400*s,94*s}};
    if(extraPanel)occupied.push_back(*extraPanel);
    auto ordered=places;
    std::stable_sort(ordered.begin(),ordered.end(),[&](town::Place a,town::Place b){return nav::distance(player,a.position)<nav::distance(player,b.position);});
    size_t labels=0;
    for(const auto& place:ordered) {
        nav::Screen pixel;if(!town::valid(place)||!nav::project(projection,place.position,pixel))continue;
        const nav::Screen point{pixel.x/projection.width*size.x,pixel.y/projection.height*size.y};
        if(point.x<8*s||point.x>size.x-8*s||point.y<100*s||point.y>size.y-8*s)continue;
        if(std::any_of(occupied.begin(),occupied.end(),[&](town::Rect r){return town::contains(r,point);}))continue;
        const auto color=navui::gold;
        d->AddRectFilled(navui::at(static_cast<float>(point.x)-3*s,static_cast<float>(point.y)-3*s),navui::at(static_cast<float>(point.x)+3*s,static_cast<float>(point.y)+3*s),navui::ink);
        d->AddRectFilled(navui::at(static_cast<float>(point.x)-s,static_cast<float>(point.y)-s),navui::at(static_cast<float>(point.x)+s,static_cast<float>(point.y)+s),color);
        const bool overPoint=focused&&std::hypot(mouse.x-point.x,mouse.y-point.y)<7*s;
        if(overPoint){hit.place=true;hit.target=place;}
        if(labels>=64)continue;
        town::Rect label;
        const float labelWidth=navui::measure(town::name(place.type),14*s).x+18*s;
        if(!town::labelRect(point,labelWidth,27*s,{8*s,100*s,size.x-16*s,size.y-108*s},occupied,label,markerBounds))continue;
        const bool hovered=focused&&town::contains(label,mouse);
        if(hovered){hit.place=true;hit.target=place;}
        d->AddLine(navui::at(static_cast<float>(point.x),static_cast<float>(point.y)),navui::at(label.x+label.w/2,label.y+label.h/2),navui::ink,3*s);
        navui::panel(d,navui::at(label.x,label.y),navui::at(label.x+label.w,label.y+label.h),s,navui::blue,hovered?color:navui::rim);
        navui::centered(d,navui::at(label.x+label.w/2,label.y+7*s),town::name(place.type),color,14*s);
        occupied.push_back(label);++labels;
    }
    navui::panel(d,navui::at(x,y),navui::at(x+w,y+h),s);
    navui::text(d,navui::at(x+13*s,y+12*s),"店铺指南",navui::cream,17*s);
    if(rows.empty()) {
        navui::text(d,navui::at(x+13*s,y+40*s),"暂未发现附近店铺",navui::muted,13*s);
        navui::text(d,navui::at(x+13*s,y+63*s),"走近城镇后打开地图",navui::muted,12*s);
    }
    for(size_t i=0;i<shown;++i) {
        const auto& place=rows[i];const float rowY=y+(40+62*static_cast<float>(i))*s;
        const town::Rect row{x+9*s,rowY,w-18*s,61*s};
        const bool hover=focused&&town::contains(row,mouse);
        if(hover){hit.panel=true;hit.place=true;hit.target=place;}
        d->AddRectFilled(navui::at(row.x,row.y),navui::at(row.x+row.w,row.y+row.h),hover?IM_COL32(69,94,107,245):IM_COL32(20,39,56,140));
        navui::text(d,navui::at(x+16*s,rowY+6*s),town::name(place.type),navui::gold,15*s);
        const auto guide=town::guide(place.type);
        navui::text(d,navui::at(x+16*s,rowY+26*s),guide.purpose,navui::cream,12.5f*s);
        navui::text(d,navui::at(x+16*s,rowY+42*s),guide.detail,navui::muted,12.5f*s);
        char distance[48];std::snprintf(distance,sizeof(distance),"%.0f m",nav::distance(player,place.position));
        navui::text(d,navui::at(x+w-17*s-navui::measure(distance,12*s).x,rowY+7*s),distance,navui::muted,12*s);
    }
    const char* footer=rows.empty()?"发现后会自动记录位置":shown<rows.size()?"附近还有店铺，可在地图上查看":"右键导航，商品以店内库存为准";
    navui::text(d,navui::at(x+13*s,y+h-23*s),footer,navui::muted,11.5f*s);
    return hit;
}
}

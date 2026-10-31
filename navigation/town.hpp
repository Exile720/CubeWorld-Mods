#pragma once
#include "nav_math.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace town {
constexpr size_t maxPlaces=512;
struct Place {int type=0;nav::Point position{};};
inline const char* name(int type) {
    // Internal facility IDs; never interpret these as native furniture type IDs.
    switch(type) {
        case 33:return "旅馆";case 34:return "道具店";case 35:return "武器店";
        case 36:return "防具店";case 37:return "鉴定店";case 38:return "铁匠铺";
        case 39:return "木工坊";case 40:return "纺织坊";default:return "";
    }
}
inline int markerType(unsigned kind,unsigned role) {
    // The native map draws shop signs for Creature kind 3, role byte 128..135.
    // GameController.cpp:91770..91805; 33/34 in Zone structures are shelves.
    if(kind!=3||role<128||role>135)return 0;
    constexpr int types[]={34,35,36,37,33,38,39,40};
    return types[role-128];
}
inline bool isShop(int type){return type>=33&&type<=40;}
inline bool valid(Place p){return name(p.type)[0]&&nav::finite(p.position);}
inline bool same(Place a,Place b) {
    return a.type==b.type&&nav::distance(a.position,b.position)<2&&std::abs(a.position.z-b.position.z)<4;
}
inline bool merge(std::vector<Place>& places,Place p) {
    if(!valid(p)||std::any_of(places.begin(),places.end(),[&](Place old){return same(old,p);})) return false;
    if(places.size()>=maxPlaces) return false;
    places.push_back(p);return true;
}
inline bool reconcile(std::vector<Place>& places,const std::vector<Place>& fixedPlaces,size_t& added) {
    // Older caches stored moving NPC coordinates. Snap only near a verified fixed shop,
    // choosing the nearest same-type anchor so separate inns remain separate.
    added=0;if(fixedPlaces.empty())return false;
    bool changed=false;std::vector<Place> next;next.reserve(places.size());
    for(auto old:places) {
        const Place* nearest=nullptr;double best=8;
        for(const auto& fixed:fixedPlaces) if(valid(fixed)&&fixed.type==old.type&&
                std::abs(fixed.position.z-old.position.z)<4) {
            const double distance=nav::distance(old.position,fixed.position);
            if(distance<best) {best=distance;nearest=&fixed;}
        }
        if(nearest) {
            if(old.position.x!=nearest->position.x||old.position.y!=nearest->position.y||
               old.position.z!=nearest->position.z) changed=true;
            old=*nearest;
        }
        if(!merge(next,old)) changed=true;
    }
    for(const auto& fixed:fixedPlaces) if(merge(next,fixed)) {++added;changed=true;}
    if(changed) places=std::move(next);
    return changed;
}
inline std::vector<Place> nearby(const std::vector<Place>& places,nav::Point player,double radius=1800) {
    // One nearest destination per facility type keeps the list short and stable as the map moves.
    std::vector<Place> rows;
    for(int type:{33,34,35,36,37,38,39,40}) {
        const Place* best=nullptr;double bestDistance=radius;
        for(const auto& p:places) if(p.type==type&&valid(p)) {
            const double distance=nav::distance(player,p.position);
            if(distance<bestDistance){best=&p;bestDistance=distance;}
        }
        if(best)rows.push_back(*best);
    }
    return rows;
}
inline const char* targetName(const std::vector<Place>& places,nav::Point target) {
    for(const auto& p:places) if(valid(p)&&nav::distance(p.position,target)<1&&std::abs(p.position.z-target.z)<1)
        return name(p.type);
    return "目的地";
}
inline std::string encode(const std::vector<Place>& places) {
    std::ostringstream out;out.imbue(std::locale::classic());out<<"CW_TOWN_2 "<<places.size()<<'\n'<<std::setprecision(17);
    for(const auto& p:places)out<<p.type<<' '<<p.position.x<<' '<<p.position.y<<' '<<p.position.z<<'\n';
    return out.str();
}
inline bool decode(const std::string& data,std::vector<Place>& places) {
    if(data.size()>65536)return false;
    std::istringstream in(data);in.imbue(std::locale::classic());std::string magic,extra;size_t count=0;
    if(!(in>>magic>>count)||magic!="CW_TOWN_2"||count>maxPlaces)return false;
    std::vector<Place> next;next.reserve(count);
    for(size_t i=0;i<count;++i) {
        Place p;if(!(in>>p.type>>p.position.x>>p.position.y>>p.position.z)||!valid(p))return false;
        merge(next,p);
    }
    if(in>>extra)return false;
    places=std::move(next);return true;
}
struct Rect {float x=0,y=0,w=0,h=0;};
inline bool contains(Rect r,nav::Screen p){return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.w&&p.y<r.y+r.h;}
inline bool overlaps(Rect a,Rect b,float gap=0) {
    return a.x<b.x+b.w+gap&&a.x+a.w+gap>b.x&&a.y<b.y+b.h+gap&&a.y+a.h+gap>b.y;
}
inline bool labelRect(nav::Screen anchor,float width,float height,Rect bounds,
                      const std::vector<Rect>& occupied,Rect& out,const Rect* markerBounds=nullptr) {
    const float x=static_cast<float>(anchor.x),y=static_cast<float>(anchor.y);
    const Rect candidates[]={{x-width/2,y-height-8,width,height},{x+9,y-height/2,width,height},
                             {x-width-9,y-height/2,width,height},{x-width/2,y+9,width,height}};
    for(const auto& r:candidates) {
        if(r.x<bounds.x||r.y<bounds.y||r.x+r.w>bounds.x+bounds.w||r.y+r.h>bounds.y+bounds.h)continue;
        if(markerBounds&&overlaps(r,*markerBounds,3))continue;
        if(std::any_of(occupied.begin(),occupied.end(),[&](Rect other){return overlaps(r,other,3);}))continue;
        out=r;return true;
    }
    return false;
}
}

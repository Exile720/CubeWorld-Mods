#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <vector>

namespace inventory {
constexpr unsigned controllerGlobal=0x76b1c8, creatureVtable=0x6fd8cc;
constexpr unsigned drawFunction=0x4c2050, selectFunction=0x4c6610;
constexpr unsigned scrollFunction=0x4c6350;
constexpr unsigned leftFunction=0x47b600, rightFunction=0x47ddd0;
constexpr unsigned playerOffset=0x8006d0, widgetOffset=0x800954, inventoryOffset=0x11dc;
constexpr unsigned itemsOffset=0x160, controllerOffset=0x164, hoverOffset=0x184, selectionOffset=0x18c;
constexpr unsigned modeOffset=0x194, pageOffset=0x1b4;
constexpr unsigned maxPages=32, maxCells=256, prefixSize=0x130;
constexpr unsigned positionFunction=0x62dc20,widthFunction=0x62f600,heightFunction=0x6291d0;
constexpr unsigned popupShapeVtable=0x71e92c,popupBoundsOffset=0x878;
constexpr std::array<unsigned,2> popupNodeOffsets{0x8008e4,0x8008ec};
using Read=bool(*)(unsigned,void*,unsigned);
struct Vector {unsigned begin=0,end=0,capacity=0;};
struct Slot {int page=-1,index=-1;};
struct Cell {
    int count=0;
    std::array<std::uint8_t,0x118> item{};
    bool occupied() const {return count>0 && item[0]!=0;}
};
static_assert(sizeof(Vector)==12 && sizeof(Slot)==8 && sizeof(Cell)==0x11c,"native Alpha layout");
inline bool count(const Vector& v,unsigned stride,unsigned limit,unsigned& n) {
    if(!v.begin) {n=0;return !v.end&&!v.capacity;}
    if((v.begin&3)||v.end<v.begin||v.capacity<v.end ||
       (v.end-v.begin)%stride || (v.capacity-v.begin)%stride ||
       (v.capacity-v.begin)/stride>limit)return false;
    n=(v.end-v.begin)/stride;return n<=limit;
}
// Only category determines grouping. Original order is stable within each group.
inline int categoryType(unsigned type) {
    switch(type) {
    case 4:return 0; case 5:return 1; case 6:return 2; case 7:return 3;
    case 3:return 4; case 8:return 5; case 9:return 6; case 2:return 7;
    case 1:return 8; case 20:return 9; case 11:return 10;
    default:return 100+type;
    }
}
inline int category(const Cell& c) {return c.occupied()?categoryType(c.item[0]):10000;}
// Native getItemTypeCategory (0x47fa30); held items can return only to their tab.
inline int nativePage(unsigned type) {
    switch(type) {
    case 3:case 4:case 5:case 6:case 7:case 8:case 9:case 23:case 24:return 0;
    case 11:return 2;
    case 19:case 20:return 3;
    default:return 1;
    }
}
struct Section {int type=0,header=0,first=0,count=0,bodyRows=0;};
struct Layout {
    unsigned controller=0,player=0,origin=0;
    std::array<std::vector<unsigned char>,maxPages> types;
    void bind(unsigned gc,unsigned p,unsigned items) {
        if(controller!=gc||player!=p||origin!=items) {
            for(auto& row:types)row.clear();
            controller=gc;player=p;origin=items;
        }
    }
};
inline const char* sectionName(int type) {
    switch(type) {
    case 0:return "放回背包";case 1:return "消耗品";case 2:return "卷轴 / 配方";
    case 3:return "武器";case 4:return "胸甲";case 5:return "手套";case 6:return "鞋子";
    case 7:return "肩甲";case 8:return "项链";case 9:return "戒指";case 11:return "材料";
    case 12:return "钱币";case 13:return "白金币";case 18:return "蜡烛";case 19:return "宠物";
    case 20:return "食物";case 21:return "配件";case 23:return "载具";case 24:return "灯具";
    case 25:return "魔力方块";default:return "其他物品";
    }
}
struct View {
    unsigned widget=0,player=0,origin=0;
    int page=-1;
    Vector outer{},row{};
    std::array<std::uint8_t,prefixSize> prefix{};
    std::vector<Vector> rows;
    std::vector<Cell> source,cells;
    std::vector<int> toNative,toDisplay;
    std::vector<Section> sections;
    int columns=0,returnIndex=-1;
    Cell held{};
    Slot displayedHover{};
    Layout localLayout;
    bool valid=false;
    bool prepare(Read read,unsigned gc,unsigned self,unsigned expectedVtable,int cols,Layout* shared=nullptr) {
        valid=false;
        displayedHover={};returnIndex=-1;
        if(cols<1||cols>32)return false;
        columns=cols;
        unsigned own=0,owner=0,vtable=0;int mode=-1;
        if(!gc || !read(gc+widgetOffset,&own,4) || own!=self ||
           !read(self+controllerOffset,&owner,4) || owner!=gc ||
           !read(self+modeOffset,&mode,4) || mode!=0 ||
           !read(gc+playerOffset,&player,4) || !player ||
           !read(player,&vtable,4) || vtable!=expectedVtable ||
           !read(self+itemsOffset,&origin,4) || origin!=player+inventoryOffset ||
           !read(self+pageOffset,&page,4) || page<0 ||
           !read(origin,prefix.data(),prefixSize))return false;
        std::memcpy(&outer,prefix.data(),sizeof(outer));
        unsigned pages=0,n=0;
        if(!count(outer,sizeof(Vector),maxPages,pages) || static_cast<unsigned>(page)>=pages)return false;
        rows.resize(pages);
        if(!read(outer.begin,rows.data(),pages*sizeof(Vector)))return false;
        row=rows[page];
        if(!count(row,sizeof(Cell),maxCells,n))return false;
        source.resize(n);
        if(n && !read(row.begin,source.data(),n*sizeof(Cell)))return false;
        // Negative stacks belong to recipe/merchant views, never a player inventory view.
        for(const auto& c:source)if(c.count<0 || (c.count>0 && (!c.item[0] || c.item[0]>25 || c.item[0]==22)))return false;
        std::memcpy(&held,prefix.data()+12,sizeof(held));
        if(held.count<0||(held.count>0&&(!held.item[0]||held.item[0]>25||held.item[0]==22)))return false;
        auto& layout=shared?*shared:localLayout;layout.bind(gc,player,origin);
        auto& types=layout.types[page];if(types.size()<n)types.resize(n);
        for(unsigned i=0;i<n;++i)if(source[i].occupied())types[i]=source[i].item[0];
        std::vector<int> ordered;
        int empty=-1;
        for(unsigned i=0;i<n;++i)if(!source[i].occupied()&&empty<0)empty=static_cast<int>(i);
        for(unsigned i=0;i<types.size();++i)if(types[i])ordered.push_back(static_cast<int>(i));
        if(held.occupied()&&nativePage(held.item[0])==page&&empty>=0) {
            returnIndex=empty;
            for(unsigned i=0;i<n;++i)if(!source[i].occupied()&&types[i]==held.item[0]){returnIndex=static_cast<int>(i);break;}
            if(!types[returnIndex])ordered.push_back(returnIndex);
        }
        const auto type=[&](int index){return types[index]?types[index]:held.item[0];};
        std::stable_sort(ordered.begin(),ordered.end(),[&](int a,int b){
            const int ca=categoryType(type(a)),cb=categoryType(type(b));
            return ca!=cb?ca<cb:a<b;
        });
        cells.clear();toNative.clear();sections.clear();toDisplay.assign(n,-1);
        const auto append=[&](Cell cell,int native) {
            if(native>=0&&toDisplay[native]<0)toDisplay[native]=static_cast<int>(cells.size());
            cells.push_back(cell);toNative.push_back(native);
        };
        const auto blank=[&](bool drop=false){append({},drop?returnIndex:-1);};
        for(std::size_t start=0;start<ordered.size();) {
            std::size_t end=start+1;const int kind=type(ordered[start]);
            while(end<ordered.size()&&type(ordered[end])==kind)++end;
            Section s{kind,static_cast<int>(cells.size()),0,static_cast<int>(end-start),0};
            for(int i=0;i<columns;++i)blank(); // one noninteractive title row
            s.first=static_cast<int>(cells.size());
            for(std::size_t i=start;i<end;++i) {
                const int original=ordered[i];const bool exists=static_cast<unsigned>(original)<n;
                const bool occupied=exists&&source[original].occupied();
                const int native=occupied?original:returnIndex<0?-1:exists?original:returnIndex;
                append(exists?source[original]:Cell{},native);
            }
            s.bodyRows=(s.count+columns-1)/columns;
            // While holding an item, visible alignment cells can route to the
            // same verified free native slot. Headers always remain noninteractive.
            while(cells.size()%columns)blank(returnIndex>=0);
            sections.push_back(s);start=end;
        }
        if(cells.size()>4096)return false;
        widget=self;
        const unsigned bytes=static_cast<unsigned>(cells.size()*sizeof(Cell));
        const unsigned data=bytes?static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(cells.data())):0;
        rows[page]={data,data+bytes,data+bytes};
        const unsigned headers=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(rows.data()));
        const Vector shadow{headers,headers+pages*sizeof(Vector),headers+pages*sizeof(Vector)};
        std::memcpy(prefix.data(),&shadow,sizeof(shadow));
        valid=true;return true;
    }
    Slot native(Slot s) const {
        if(!valid || s.page!=page || s.index<0 || static_cast<unsigned>(s.index)>=toNative.size())return {};
        return toNative[s.index]<0?Slot{}:Slot{s.page,toNative[s.index]};
    }
    Slot display(Slot s) const {
        if(!valid || s.page!=page || s.index<0 || static_cast<unsigned>(s.index)>=toDisplay.size())return {};
        const auto hovered=native(displayedHover);
        if(hovered.page==s.page&&hovered.index==s.index)return displayedHover;
        return toDisplay[s.index]<0?Slot{}:Slot{s.page,toDisplay[s.index]};
    }
    bool current(Read read,unsigned gc,unsigned expectedVtable) const {
        if(!valid)return false;
        unsigned own=0,p=0,o=0,owner=0,vt=0;int active=-1;
        Vector a{},b{};Cell cursor{};
        if(!read(gc+widgetOffset,&own,4)||own!=widget||!read(gc+playerOffset,&p,4)||p!=player||
           !read(p,&vt,4)||vt!=expectedVtable||!read(widget+controllerOffset,&owner,4)||owner!=gc||
           !read(widget+itemsOffset,&o,4)||o!=origin||!read(widget+pageOffset,&active,4)||active!=page||
           !read(origin,&a,sizeof(a))||std::memcmp(&a,&outer,sizeof(a))||!read(origin+12,&cursor,sizeof(cursor))||std::memcmp(&cursor,&held,sizeof(cursor))||
           !read(outer.begin+page*sizeof(Vector),&b,sizeof(b))||std::memcmp(&b,&row,sizeof(b)))return false;
        // Bounded stack storage: no allocation or game/session/entity scan on input callbacks.
        std::array<Cell,maxCells> actual{};
        const unsigned bytes=static_cast<unsigned>(source.size()*sizeof(Cell));
        return !bytes || (read(row.begin,actual.data(),bytes)&&!std::memcmp(actual.data(),source.data(),bytes));
    }
};
}

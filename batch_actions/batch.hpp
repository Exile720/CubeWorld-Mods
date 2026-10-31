#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace batch {
constexpr unsigned itemSize=0x118,cellSize=0x11c,limit=10,maxBatch=50;
constexpr unsigned buyFunction=0x47b600,craftFunction=0x4709c0;
struct Vector {unsigned begin=0,end=0,capacity=0;};
struct Item {std::array<unsigned char,itemSize> bytes{};};
struct Cell {int count=0;Item item;};
struct Ingredient {Item item;int count=0;};
static_assert(sizeof(Cell)==cellSize && sizeof(Ingredient)==cellSize);
template<class T>T field(const Item& item,unsigned at) {T v{};std::memcpy(&v,item.bytes.data()+at,sizeof(v));return v;}
inline Item item(unsigned type,unsigned subtype,int level=1,unsigned material=0) {
    Item v;v.bytes[0]=static_cast<unsigned char>(type);v.bytes[1]=static_cast<unsigned char>(subtype);
    v.bytes[0xd]=static_cast<unsigned char>(material);const auto l=static_cast<short>(level);
    std::memcpy(v.bytes.data()+0x10,&l,2);return v;
}
inline bool same(const Item& a,const Item& b) {
    // Native equality ignores padding. Only ordinary, unmodified supplies are eligible.
    return a.bytes[0]==b.bytes[0] && a.bytes[1]==b.bytes[1] &&
        field<int>(a,4)==field<int>(b,4) && field<int>(a,8)==field<int>(b,8) &&
        a.bytes[0xc]==b.bytes[0xc] && a.bytes[0xd]==b.bytes[0xd] && a.bytes[0xe]==b.bytes[0xe] &&
        field<short>(a,0x10)==field<short>(b,0x10) && field<int>(a,0x114)==0 && field<int>(b,0x114)==0;
}
inline bool life(const Item& v) {
    const int l=field<short>(v,0x10);
    return l>0 && (l-1)%4==0 && same(v,item(1,1,l));
}
inline bool size(Vector v,unsigned stride,unsigned maximum,unsigned& n) {
    n=0;if(!v.begin)return !v.end&&!v.capacity;
    if(v.begin%4 || v.end<v.begin || v.capacity<v.end || (v.end-v.begin)%stride ||
       (v.capacity-v.begin)%stride || (v.capacity-v.begin)/stride>maximum)return false;
    n=(v.end-v.begin)/stride;return true;
}
template<class Read>bool visible(Read read,unsigned root,bool& value) {
    value=false;unsigned state=0,table=0;int index=-1,on=0;
    if(!root || !read(root+0x3c,&state,4)||!state || !read(state+0x68,&index,4)||index<0||index>=256 ||
       !read(state+0x94,&table,4)||!table || !read(table+4*index,&on,4))return false;
    value=on!=0;return true;
}
template<class Read>bool panel(Read read,unsigned gc,unsigned offset,bool expected) {
    unsigned root=0;bool on=false;return read(gc+offset,&root,4)&&visible(read,root,on)&&on==expected;
}
template<class Read>bool base(Read read,unsigned gc,unsigned& player) {
    unsigned held=0;unsigned char network=1;
    return gc && read(gc+0x8006d0,&player,4)&&player && read(gc+0x8009b0,&network,1)&&!network &&
        read(player+0x11e8,&held,4)&&!held;
}
template<class Read>bool selected(Read read,unsigned header,int row,int col,Cell& cell,unsigned& address) {
    Vector outer{},inner{};unsigned rows=0,cols=0;
    if(!read(header,&outer,sizeof(outer))||!size(outer,sizeof(Vector),32,rows)||row<0||unsigned(row)>=rows ||
       !read(outer.begin+row*sizeof(Vector),&inner,sizeof(inner))||!size(inner,cellSize,256,cols)||
       col<0||unsigned(col)>=cols)return false;
    address=inner.begin+col*cellSize+4;return read(address-4,&cell,sizeof(cell));
}
template<class Read>bool count(Read read,unsigned player,const Item& wanted,int& total) {
    total=0;Vector outer{};unsigned rows=0,cells=0;
    if(!read(player+0x11dc,&outer,sizeof(outer))||!size(outer,sizeof(Vector),32,rows))return false;
    for(unsigned r=0;r<rows;++r) {
        Vector inner{};unsigned n=0;
        if(!read(outer.begin+r*sizeof(Vector),&inner,sizeof(inner))||!size(inner,cellSize,256,n)||
           cells+n>4096)return false;
        cells+=n;
        for(unsigned c=0;c<n;++c) {
            Cell v;if(!read(inner.begin+c*cellSize,&v,sizeof(v))||v.count<0)return false;
            if(v.count && same(v.item,wanted)) {if(v.count>50 || total>50-v.count)return false;total+=v.count;}
        }
    }
    return true;
}
struct Buy {unsigned player=0,merchant=0,stock=0,widget=0;int row=-1,col=-1;Item output;int owned=0,coins=0;};
template<class Read>bool buying(Read read,unsigned gc,Buy& out,bool requireRoom=true,const Buy* pinned=nullptr) {
    out={};unsigned widget=0,bag=0;unsigned char role=0;int mode=-1,row=-1,col=-1;
    bool bagOn=false;unsigned bagRoot=0;
    if(!base(read,gc,out.player)||!panel(read,gc,0x8008c4,true)||
       !read(gc+0x8008d8,&out.merchant,4)||!out.merchant || !read(out.merchant+0x140,&role,1)||role!=0x80 ||
       !read(gc+0x80095c,&widget,4)||!widget || !read(widget+0x1b4,&mode,4)||mode ||
       !read(gc+0x8008bc,&bagRoot,4)||!visible(read,bagRoot,bagOn))return false;
    if(pinned){row=pinned->row;col=pinned->col;}
    else if(!read(widget+0x184,&row,4)||!read(widget+0x188,&col,4))return false;
    // A player's bag hover takes precedence over the merchant in the original mouse handler.
    if(bagOn&&!pinned) {
        int br=-1,bc=-1;
        if(!read(gc+0x800954,&bag,4)||!bag||!read(bag+0x184,&br,4)||!read(bag+0x188,&bc,4))return false;
        if(br>=0 && bc>=0)return false;
    }
    Cell stock;
    if(!selected(read,gc+0x800c0c,row,col,stock,out.stock)||stock.count<=0||
       !same(stock.item,item(11,12,1,24)))return false;
    out.output=stock.item;
    out.widget=widget;out.row=row;out.col=col;
    return count(read,out.player,out.output,out.owned)&&(!requireRoom||out.owned<50) &&
        read(out.player+0x1304,&out.coins,4)&&out.coins>=0;
}
struct Craft {
    unsigned player=0,widget=0;Item output;std::array<Ingredient,2> inputs{};
    unsigned inputCount=0;int owned=0;std::array<int,2> available{};
};
template<class Read>bool crafting(Read read,unsigned gc,Craft& out,bool requireMaterials=true) {
    out={};float progress=0;Item selectedOutput;Vector inputs{};unsigned n=0;
    if(!base(read,gc,out.player)||!panel(read,gc,0x800ad4,true)||
       !read(gc+0x800964,&out.widget,4)||!out.widget ||
       !read(gc+0x1000e78,&progress,4)||progress!=-1.0f ||
       !read(gc+0x1000e7c,&out.output,sizeof(Item))||
       !read(out.widget+0x16c,&selectedOutput,sizeof(Item))||!same(out.output,selectedOutput)||
       !read(gc+0x1000f98,&inputs,sizeof(inputs))||!size(inputs,sizeof(Ingredient),32,n))return false;
    const bool water=same(out.output,item(11,26));
    if(!water&&!life(out.output))return false;
    out.inputCount=water?1:2;
    if(n!=out.inputCount||!read(inputs.begin,out.inputs.data(),n*sizeof(Ingredient)))return false;
    const std::array<Item,2> expected=water?std::array<Item,2>{item(11,12,1,24),{}}:
        std::array<Item,2>{item(11,22),item(11,26)};
    for(unsigned i=0;i<n;++i)
        if(out.inputs[i].count!=1||!same(out.inputs[i].item,expected[i])||
           !count(read,out.player,out.inputs[i].item,out.available[i])||(requireMaterials&&out.available[i]<1))return false;
    return count(read,out.player,out.output,out.owned)&&(!requireMaterials||out.owned<50);
}
inline bool sameContext(const Buy& a,const Buy& b) {
    return a.player==b.player&&a.merchant==b.merchant&&a.stock==b.stock&&a.widget==b.widget&&a.row==b.row&&a.col==b.col&&same(a.output,b.output);
}
inline bool sameContext(const Craft& a,const Craft& b) {
    if(a.player!=b.player||a.widget!=b.widget||a.inputCount!=b.inputCount||!same(a.output,b.output))return false;
    for(unsigned i=0;i<a.inputCount;++i)if(!same(a.inputs[i].item,b.inputs[i].item)||a.inputs[i].count!=b.inputs[i].count)return false;
    return true;
}
// A native call owns all mutations. A failed/partial native result is never retried.
template<class Read,class Ready,class Invoke>unsigned buyBatch(Read read,unsigned gc,Ready ready,Invoke invoke,int price,unsigned quantity=limit,const Buy* pinned=nullptr) {
    Buy origin;if(price<1||!buying(read,gc,origin,true,pinned)||origin.coins<price)return 0;
    unsigned done=0;
    for(unsigned i=0;i<quantity&&i<maxBatch&&ready();++i) {
        Buy before;
        if(!buying(read,gc,before,true,pinned)||!sameContext(origin,before)||before.coins<price)break;
        invoke();
        int owned=0,coins=-1;
        if(!count(read,origin.player,origin.output,owned)||!read(origin.player+0x1304,&coins,4)||
           owned!=before.owned+1||coins!=before.coins-price)break;
        ++done;
    }
    return done;
}
template<class Read,class Ready,class Invoke>unsigned craftBatch(Read read,unsigned gc,Ready ready,Invoke invoke,unsigned quantity=limit) {
    Craft origin;if(!crafting(read,gc,origin))return 0;
    unsigned done=0;
    for(unsigned i=0;i<quantity&&i<maxBatch&&ready();++i) {
        Craft before;if(!crafting(read,gc,before)||!sameContext(origin,before))break;
        invoke();int owned=0;
        if(!count(read,origin.player,origin.output,owned)||owned!=before.owned+1)break;
        bool consumed=true;
        for(unsigned k=0;k<before.inputCount;++k) {
            int left=0;if(!count(read,origin.player,before.inputs[k].item,left)||left!=before.available[k]-1)consumed=false;
        }
        ++done;if(!consumed)break;
    }
    return done;
}
inline int maximum(const Buy& v,int price) {return price>0?(std::min)(50-v.owned,v.coins/price):0;}
inline int maximum(const Craft& v) {
    int n=50-v.owned;for(unsigned i=0;i<v.inputCount;++i)n=(std::min)(n,v.available[i]);return n;
}
struct Quantity {
    int value=10,maximum=0;
    void fit(int n){maximum=(std::max)(0,(std::min)(50,n));value=(std::min)(maximum,(std::max)(maximum?1:0,value));}
    void step(int direction){if(!maximum){value=0;return;}value=(std::max)(1,(std::min)(maximum,value+10*direction));}
};
}

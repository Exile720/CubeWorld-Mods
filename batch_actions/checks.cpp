#include "batch.hpp"
#include <map>
#include <vector>
#include <cstdio>
#include <cstdlib>

namespace {
unsigned tests=0;
void check(bool v,const char* name){++tests;if(!v){std::printf("FAIL %s\n",name);std::exit(1);}}
std::map<unsigned,std::vector<unsigned char>> memory;
template<class T>void put(unsigned a,const T& v){
    for(auto& s:memory)if(a>=s.first&&std::uint64_t(a-s.first)+sizeof(v)<=s.second.size()){
        std::memcpy(s.second.data()+a-s.first,&v,sizeof(v));return;}
    auto& b=memory[a];b.resize(sizeof(v));std::memcpy(b.data(),&v,sizeof(v));
}
bool read(unsigned a,void* out,unsigned n){
    for(const auto& s:memory)if(a>=s.first&&std::uint64_t(a-s.first)+n<=s.second.size()){
        std::memcpy(out,s.second.data()+a-s.first,n);return true;}
    return false;
}
constexpr unsigned gc=0x1000000,player=0x3000000,fixtureRows=0x4000000,fixtureCells=0x4100000;
constexpr unsigned merchant=0x5000000,trade=0x5100000,tradeRows=0x5200000,tradeCells=0x5300000;
constexpr unsigned craftWidget=0x5400000,fixtureInputs=0x5500000,bagWidget=0x5600000;
unsigned calls=0;
void cell(unsigned i,int n,batch::Item item){put(fixtureCells+i*batch::cellSize,batch::Cell{n,item});}
void show(unsigned offset,unsigned at,bool on){put(gc+offset,at);put(at+0x3c,at+0x1000);put(at+0x1068,0);put(at+0x1094,at+0x2000);put(at+0x2000,int(on));}
void fixture(bool water=false,int outputCount=0,int materialCount=30,int coins=100){
    memory.clear();calls=0;put(gc+0x8006d0,player);put(gc+0x8009b0,static_cast<unsigned char>(0));
    put(player+0x11e8,0);put(player+0x1304,coins);
    put(player+0x11dc,batch::Vector{fixtureRows,fixtureRows+12,fixtureRows+12});
    put(fixtureRows,batch::Vector{fixtureCells,fixtureCells+4*batch::cellSize,fixtureCells+4*batch::cellSize});
    cell(0,outputCount,water?batch::item(11,26):batch::item(1,1,13));
    cell(1,materialCount,batch::item(11,12,1,24));cell(2,materialCount,batch::item(11,22));cell(3,materialCount,batch::item(11,26));
    // Water tests use only one water stack, so the output begins at zero unless requested.
    if(water)cell(3,0,batch::item(11,26));
    show(0x800ad4,0x5800000,true);put(gc+0x800964,craftWidget);put(gc+0x1000e78,-1.0f);
    const auto output=water?batch::item(11,26):batch::item(1,1,13);
    put(gc+0x1000e7c,output);put(craftWidget+0x16c,output);
    const unsigned n=water?1:2;put(gc+0x1000f98,batch::Vector{fixtureInputs,fixtureInputs+n*batch::cellSize,fixtureInputs+n*batch::cellSize});
    if(water)put(fixtureInputs,batch::Ingredient{batch::item(11,12,1,24),1});
    else put(fixtureInputs,std::array<batch::Ingredient,2>{batch::Ingredient{batch::item(11,22),1},batch::Ingredient{batch::item(11,26),1}});
    show(0x8008c4,0x5900000,true);show(0x8008bc,0x5a00000,true);
    put(gc+0x800954,bagWidget);put(bagWidget+0x184,-1);put(bagWidget+0x188,-1);
    put(gc+0x8008d8,merchant);put(merchant+0x140,static_cast<unsigned char>(0x80));put(gc+0x80095c,trade);
    put(trade+0x184,0);put(trade+0x188,0);put(trade+0x1b4,0);
    put(gc+0x800c0c,batch::Vector{tradeRows,tradeRows+12,tradeRows+12});
    put(tradeRows,batch::Vector{tradeCells,tradeCells+batch::cellSize,tradeCells+batch::cellSize});
    put(tradeCells,batch::Cell{1,batch::item(11,12,1,24)});
}
void change(unsigned index,int delta){batch::Cell v;check(read(fixtureCells+index*batch::cellSize,&v,sizeof(v)),"native fixture reads");v.count+=delta;put(fixtureCells+index*batch::cellSize,v);}
void nativeCraft(bool water=false){++calls;change(0,1);if(water)change(1,-1);else{change(2,-1);change(3,-1);}}
void nativeBuy(){++calls;change(1,1);int c=0;read(player+0x1304,&c,4);put(player+0x1304,c-2);}
unsigned make(bool water=false){return batch::craftBatch(&read,gc,[]{return true;},[=]{nativeCraft(water);});}
unsigned buy(){return batch::buyBatch(&read,gc,[]{return true;},&nativeBuy,2);}
}
int main(){
    fixture();check(make()==10&&calls==10,"life recipe 13: ten native outputs, consumed ingredients");
    fixture(true);check(make(true)==10&&calls==10,"water: ten outputs and ten empty bottles");
    fixture(false,0,3);check(make()==3&&calls==3,"craft stops at three available ingredients");
    fixture(true,0,1);check(make(true)==1&&calls==1,"water stops at one bottle");
    fixture(false,49);check(make()==1&&calls==1,"craft stops at native carry 50");
    fixture(true,50);check(make(true)==0&&calls==0,"full water stack cannot start batch");
    fixture(false,0,0);check(make()==0&&calls==0,"no ingredients: no extra native call");
    fixture();put(gc+0x8009b0,static_cast<unsigned char>(1));check(make()==0&&buy()==0,"networking rejects bulk");
    fixture();put(player+0x11e8,1);check(make()==0&&buy()==0,"held stack rejects bulk");
    fixture();put(craftWidget+0x16c,batch::item(1,1,17));check(make()==0,"changed selected recipe rejects old output");
    fixture();put(gc+0x1000e78,0.5f);check(make()==0,"unfinished native craft never replayed");
    fixture();put(fixtureInputs,batch::Ingredient{batch::item(11,22),0});check(make()==0,"zero ingredient requirement rejected");
    fixture();put(fixtureInputs,batch::Ingredient{batch::item(11,12,1,24),1});check(make()==0,"unexpected ingredient rejected");
    fixture();put(gc+0x1000f98,batch::Vector{fixtureInputs,fixtureInputs+3*batch::cellSize,fixtureInputs+3*batch::cellSize});check(make()==0,"oversized ingredient vector rejected");
    fixture();unsigned done=batch::craftBatch(&read,gc,[]{return calls<2;},[]{nativeCraft();});check(done==2&&calls==2,"release Shift cancels remaining calls");
    fixture();done=batch::craftBatch(&read,gc,[]{return true;},[]{++calls;});check(done==0&&calls==1,"failed native craft not retried");
    fixture();done=batch::craftBatch(&read,gc,[]{return true;},[]{++calls;change(0,1);});check(done==1&&calls==1,"missing native consumption stops replay");
    fixture();done=batch::craftBatch(&read,gc,[]{return true;},[]{nativeCraft();put(gc+0x8006d0,0u);});check(done==1&&calls==1,"character change stops remaining batch");
    fixture(false,0,0);check(buy()==10&&calls==10,"buying ten flasks uses ten paid native calls");
    fixture(false,0,0,7);check(buy()==3&&calls==3,"seven copper buys only three two-copper flasks");
    fixture(false,0,0,1);check(buy()==0&&calls==0,"insufficient copper cannot start");
    fixture();cell(1,49,batch::item(11,12,1,24));check(buy()==1&&calls==1,"buying stops at carry 50");
    fixture();put(bagWidget+0x184,0);put(bagWidget+0x188,0);check(buy()==0,"backpack hover never repeated as a sale");
    fixture();put(trade+0x1b4,1);check(buy()==0,"buyback list excluded");
    fixture();put(tradeCells,batch::Cell{1,batch::item(1,1,9)});check(buy()==0,"shop potions and equipment excluded");
    fixture();put(tradeCells,batch::Cell{1,batch::item(11,12,1,0)});check(buy()==0,"nonstandard empty bottle excluded");
    fixture();put(gc+0x800c0c,batch::Vector{tradeRows,tradeRows+1,tradeRows+12});check(buy()==0,"malformed inventory fails closed");
    fixture();memory.erase(fixtureCells+2*batch::cellSize);check(make()==0&&buy()==0,"unreadable inventory never partly trusted");
    fixture();done=batch::buyBatch(&read,gc,[]{return true;},[]{++calls;},2);check(done==0&&calls==1,"failed native buy not repeated");
    fixture();done=batch::buyBatch(&read,gc,[]{return true;},[]{nativeBuy();put(trade+0x188,1);},2);check(done==1&&calls==1,"selection change stops later buys");
    for(int level:{1,5,9,13,17,101,1001,32765}){fixture();cell(0,0,batch::item(1,1,level));put(gc+0x1000e7c,batch::item(1,1,level));put(craftWidget+0x16c,batch::item(1,1,level));check(make()==10,"future native life potion tiers work without table");}
    std::printf("%u batch assertions passed (synthetic native inventory; no game executed).\n",tests);
}

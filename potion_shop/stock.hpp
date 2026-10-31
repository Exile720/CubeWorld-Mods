#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace potions {
constexpr std::uint32_t itemBytes=0x118, cellBytes=0x11c;
constexpr std::uint32_t maxRows=32, maxPerRow=256, maxCells=4096;
constexpr int copperPrice=10;
constexpr unsigned maxRecipes=4096;
struct Vector {std::uint32_t begin=0,end=0,capacity=0;};
struct Item {std::array<std::uint8_t,itemBytes> bytes{};};
struct Cell {std::int32_t count=0;Item item;};
static_assert(sizeof(Vector)==12 && sizeof(Item)==itemBytes && sizeof(Cell)==cellBytes);

template<class T> T field(const Item& item,unsigned offset) {
    T value{};std::memcpy(&value,item.bytes.data()+offset,sizeof(value));return value;
}
inline bool potionLevel(int level) {
    return level>=1 && level<=32767 && (level-1)%4==0;
}
inline Item lifePotion(int level) {
    Item item;
    if(potionLevel(level)) {
        item.bytes[0]=1;item.bytes[1]=1;
        const auto value=static_cast<std::int16_t>(level);
        std::memcpy(item.bytes.data()+0x10,&value,sizeof(value));
    }
    return item;
}
inline Item basicPotion() {return lifePotion(1);}
inline bool isLifePotion(const Item& item,int level) {
    // Match the native semantic fields, not uninitialized structure padding.
    return potionLevel(level) && item.bytes[0]==1 && item.bytes[1]==1 &&
           field<std::int32_t>(item,4)==0 && field<std::int32_t>(item,8)==0 &&
           item.bytes[0xc]==0 && item.bytes[0xd]==0 && item.bytes[0xe]==0 &&
           field<std::int16_t>(item,0x10)==level && field<std::int32_t>(item,0x114)==0;
}
inline bool isBasicPotion(const Item& item) {return isLifePotion(item,1);}
inline int priceForLevel(int level) {
    if(!potionLevel(level)) return 0;
    // Native ordinary-potion healing is 200 * this scale (00445f60 / 004c6e10).
    // Accepted price: 10 + (healing - 200) / 40, rounded up, in copper.
    const double scale=std::pow(2.0,3.0*(1.0-1.0/(1.0+0.05*(level-1))));
    return static_cast<int>(std::ceil(5.0+5.0*scale));
}
struct Offer {
    unsigned player=0,merchant=0;
    int craftLevel=0,level=0,copper=0;
};
struct RecipeNode {unsigned next=0,previous=0;Item item;};
static_assert(sizeof(RecipeNode)==0x120);

template<class Read> bool shopOffer(Read read,unsigned gc,unsigned merchant,Offer& output) {
    output={};unsigned player=0,recipes=0,head=0,count=0;int playerLevel=0;
    if(!gc || !merchant || !read(gc+0x8006d0,&player,4) || !player ||
       !read(player+0x190,&playerLevel,4) || playerLevel<1 || playerLevel>32767 ||
       !read(player+0x1d28,&recipes,4) || !recipes ||
       !read(recipes+0x14,&head,4) || !head || head%4 ||
       !read(recipes+0x18,&count,4) || count>maxRecipes) return false;
    std::array<unsigned,2> links{};
    if(!read(head,links.data(),sizeof(links))) return false;
    unsigned node=links[0],previous=head;int highest=0;
    for(unsigned i=0;i<count;++i) {
        RecipeNode entry;
        if(!node || node==head || node%4 || !read(node,&entry,sizeof(entry)) ||
           entry.previous!=previous) return false;
        const int level=field<std::int16_t>(entry.item,0x10);
        if(level>highest && level<=playerLevel && isLifePotion(entry.item,level)) highest=level;
        previous=node;node=entry.next;
    }
    // Check the complete list before trusting any partially read recipe.
    if(node!=head || links[1]!=previous) return false;
    output.player=player;output.merchant=merchant;output.craftLevel=highest;
    if(highest>=5) {
        output.level=highest-4;output.copper=priceForLevel(output.level);
    }
    return true;
}
inline bool vectorCount(Vector v,unsigned stride,unsigned limit,unsigned& count) {
    count=0;
    if(!v.begin) return v.end==0 && v.capacity==0;
    if(v.begin%4 || v.end<v.begin || v.capacity<v.end ||
       (v.end-v.begin)%stride || (v.capacity-v.begin)%stride ||
       (v.capacity-v.begin)/stride>limit) return false;
    count=(v.end-v.begin)/stride;return count<=limit;
}
inline bool cellItemAddress(Vector row,unsigned address) {
    unsigned count=0;
    return vectorCount(row,cellBytes,maxPerRow,count) && count && address>=row.begin+4 &&
           address<row.end && (address-row.begin-4)%cellBytes==0;
}
template<class Read> bool itemShopContext(Read read,unsigned gc,unsigned& selected) {
    unsigned player=0,widget=0;std::uint8_t network=1,role=0;int mode=-1;
    selected=0;
    return gc && read(gc+0x8006d0,&player,4) && player &&
           read(gc+0x8009b0,&network,1) && network==0 &&
           read(gc+0x8008d8,&selected,4) && selected &&
           read(gc+0x80095c,&widget,4) && widget &&
           read(selected+0x140,&role,1) && role==0x80 &&
           read(widget+0x1b4,&mode,4) && mode==0;
}
template<class Read> bool exactStockItem(Read read,unsigned gc,unsigned address) {
    Vector outer{};unsigned count=0;
    if(!read(gc+0x800c0c,&outer,sizeof(outer)) ||
       !vectorCount(outer,sizeof(Vector),maxRows,count)) return false;
    for(unsigned i=0;i<count;++i) {
        Vector row{};
        if(!read(outer.begin+i*sizeof(row),&row,sizeof(row))) return false;
        if(cellItemAddress(row,address)) return true;
    }
    return false;
}
template<class Read> int purchasePrice(Read read,unsigned gc,unsigned address,const Offer& offer) {
    unsigned selected=0,player=0;Item item{};
    if(!offer.level || offer.copper!=priceForLevel(offer.level)) return -1;
    if(!itemShopContext(read,gc,selected) || !exactStockItem(read,gc,address) ||
       selected!=offer.merchant || !read(gc+0x8006d0,&player,4) || player!=offer.player ||
       !read(address,&item,sizeof(item)) || !isLifePotion(item,offer.level)) return -1;
    // Price belongs to the opened shop's offer, even if the player levels up meanwhile.
    return offer.copper;
}
enum class Prepare {Invalid,Full,Present,Added};

// This is a READ-ONLY source for the game's existing nested-vector assignment.
// The game copies these POD cells with its own allocator; it never owns this memory.
// Lifetime extends through the native copy after the hook returns. One game thread only.
struct StockSource {
    Vector header{};
    std::array<Vector,maxRows> rows{};
    std::array<Cell,maxCells+1> cells{};

    template<class Read> Prepare prepare(Read read,unsigned source,int level) {
        Vector original{};unsigned rowCount=0,total=0;
        if(!potionLevel(level) || !read(source,&original,sizeof(original)) ||
           !vectorCount(original,sizeof(Vector),maxRows,rowCount) || rowCount==0)
            return Prepare::Invalid;
        std::array<Vector,maxRows> inputs{};
        if(!read(original.begin,inputs.data(),rowCount*sizeof(Vector))) return Prepare::Invalid;
        std::array<unsigned,maxRows> counts{};
        bool present=false;
        for(unsigned r=0;r<rowCount;++r) {
            if(!vectorCount(inputs[r],cellBytes,maxPerRow,counts[r]) ||
               total+counts[r]>maxCells) return Prepare::Invalid;
            total+=counts[r];
        }
        if(counts[0]>=maxPerRow || total>=maxCells) return Prepare::Full;
        unsigned next=0;
        for(unsigned r=0;r<rowCount;++r) {
            const unsigned n=counts[r],start=next;
            if(n && !read(inputs[r].begin,cells.data()+start,n*sizeof(Cell))) return Prepare::Invalid;
            for(unsigned i=0;i<n;++i) {
                const auto& cell=cells[start+i];
                const int upgrades=field<std::int32_t>(cell.item,0x114);
                if(cell.count<0 || cell.count>1000000 || upgrades<0 || upgrades>32)
                    return Prepare::Invalid;
                if(cell.count>0 && isLifePotion(cell.item,level)) present=true;
            }
            next+=n;
            if(r==0) cells[next++]={1,lifePotion(level)};
            const auto begin=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(cells.data()+start));
            rows[r]={begin,begin+(next-start)*cellBytes,begin+(next-start)*cellBytes};
        }
        if(present) return Prepare::Present;
        const auto begin=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(rows.data()));
        header={begin,begin+rowCount*sizeof(Vector),begin+rowCount*sizeof(Vector)};
        return Prepare::Added;
    }
};
}

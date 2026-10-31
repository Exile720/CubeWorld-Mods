#include "stock.hpp"
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

namespace {
unsigned checks=0,failures=0;
void check(bool value,const char* description) {
    ++checks;
    if(!value) {++failures;std::printf("FAIL: %s\n",description);}
}
template<class T> void setField(potions::Item& item,unsigned offset,T value) {
    std::memcpy(item.bytes.data()+offset,&value,sizeof(value));
}
bool same(const potions::Cell& a,const potions::Cell& b) {
    return std::memcmp(&a,&b,sizeof(a))==0;
}

// Every input address is synthetic. The reader never dereferences a game pointer.
struct Memory {
    struct Segment {unsigned address;std::vector<unsigned char> bytes;};
    std::vector<Segment> segments;
    unsigned next=0x10000,deny=0;
    template<class T> unsigned store(const T* data,unsigned count) {
        if(!count) return 0;
        const unsigned address=next,size=count*static_cast<unsigned>(sizeof(T));
        Segment segment{address,std::vector<unsigned char>(size)};
        std::memcpy(segment.bytes.data(),data,size);
        segments.push_back(std::move(segment));next+=(size+3)&~3u;next+=0x100;
        return address;
    }
    template<class T> unsigned store(const T& data) {return store(&data,1);}
    template<class T> void at(unsigned address,const T& data) {
        Segment segment{address,std::vector<unsigned char>(sizeof(T))};
        std::memcpy(segment.bytes.data(),&data,sizeof(T));segments.push_back(std::move(segment));
    }
    template<class T> void mutate(unsigned address,const T& data) {
        for(auto& segment:segments) {
            if(address<segment.address) continue;
            const auto offset=std::uint64_t(address)-segment.address;
            if(offset+sizeof(T)<=segment.bytes.size()) {
                std::memcpy(segment.bytes.data()+static_cast<std::size_t>(offset),&data,sizeof(T));return;
            }
        }
        check(false,"test fixture mutation must target owned synthetic memory");
    }
    bool read(unsigned address,void* target,unsigned size) const {
        if(address==deny) return false;
        for(const auto& segment:segments) {
            const std::uint64_t offset=std::uint64_t(address)-segment.address;
            if(address>=segment.address && offset+size<=segment.bytes.size()) {
                std::memcpy(target,segment.bytes.data()+static_cast<std::size_t>(offset),size);
                return true;
            }
        }
        return false;
    }
    bool unchanged(const std::vector<Segment>& before) const {
        if(before.size()!=segments.size()) return false;
        for(std::size_t i=0;i<before.size();++i)
            if(before[i].address!=segments[i].address || before[i].bytes!=segments[i].bytes)
                return false;
        return true;
    }
};
struct Scene {
    Memory memory;
    std::vector<potions::Vector> rows;
    unsigned source=0,outer=0;
    explicit Scene(const std::vector<std::vector<potions::Cell>>& contents) {
        for(const auto& row:contents) {
            const unsigned begin=memory.store(row.data(),static_cast<unsigned>(row.size()));
            const unsigned end=begin+static_cast<unsigned>(row.size())*potions::cellBytes;
            rows.push_back({begin,end,end});
        }
        outer=memory.store(rows.data(),static_cast<unsigned>(rows.size()));
        source=memory.store(potions::Vector{outer,outer+static_cast<unsigned>(rows.size())*12,
                                                  outer+static_cast<unsigned>(rows.size())*12});
    }
    potions::Prepare prepare(potions::StockSource& output,int level=1) const {
        return output.prepare([this](unsigned address,void* target,unsigned size) {
            return memory.read(address,target,size);
        },source,level);
    }
};
potions::Cell ordinary(int count,unsigned char discriminator) {
    potions::Cell cell{count,{}};cell.item.bytes[0]=2;cell.item.bytes[8]=discriminator;
    setField<std::int16_t>(cell.item,0x10,1);return cell;
}
unsigned outputCount(const potions::StockSource& source,unsigned row) {
    unsigned count=0;
    return potions::vectorCount(source.rows[row],potions::cellBytes,potions::maxPerRow,count)
           ? count : std::numeric_limits<unsigned>::max();
}
const potions::Cell& outputCell(const potions::StockSource& source,unsigned row,unsigned index) {
    // Compare the stored 32-bit addresses without assuming a 64-bit host can dereference them.
    const auto base=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(source.cells.data()));
    return source.cells[(source.rows[row].begin-base)/potions::cellBytes+index];
}

void itemChecks() {
    const auto basic=potions::basicPotion();
    check(potions::copperPrice==10,"accepted basic potion price is ten copper");
    check(potions::isBasicPotion(basic),"constructed potion is the basic tier");
    check(basic.bytes[0]==1 && basic.bytes[1]==1,"native consumable and life potion identifiers");
    check(potions::field<std::int16_t>(basic,0x10)==1,"basic potion uses raw level one");
    bool clear=true;
    for(unsigned i=0;i<potions::itemBytes;++i)
        if(i!=0 && i!=1 && i!=0x10 && basic.bytes[i]) clear=false;
    check(clear,"all unused potion fields and upgrades start cleared");
    for(const unsigned offset:{0u,1u,0xcu,0xdu}) {
        auto changed=basic;++changed.bytes[offset];
        check(!potions::isBasicPotion(changed),"type subtype rarity and material are filtered");
    }
    for(const std::int16_t level:{std::int16_t(-1),std::int16_t(0),std::int16_t(2),std::int16_t(50)}) {
        auto changed=basic;setField(changed,0x10,level);
        check(!potions::isBasicPotion(changed),"nonbasic raw levels are filtered");
    }
    auto upgraded=basic;setField<std::int32_t>(upgraded,0x114,1);
    check(!potions::isBasicPotion(upgraded),"upgraded potion is excluded from basic tier");
}
void vectorChecks() {
    unsigned count=99;
    check(potions::vectorCount({},potions::cellBytes,256,count) && count==0,"null empty vector accepted");
    const potions::Vector row{0x20000,0x20000+3*potions::cellBytes,0x20000+4*potions::cellBytes};
    check(potions::vectorCount(row,potions::cellBytes,256,count) && count==3,"count excludes reserved capacity");
    check(potions::cellItemAddress(row,row.begin+4),"first exact item address accepted");
    check(potions::cellItemAddress(row,row.begin+2*potions::cellBytes+4),"last exact item address accepted");
    for(unsigned address:{0u,row.begin,row.begin+3,row.begin+5,row.begin+potions::cellBytes,
                          row.end,row.end+4,row.capacity+4})
        check(!potions::cellItemAddress(row,address),"count fields interior bytes and unused capacity rejected");
    for(const potions::Vector bad:{potions::Vector{0,4,4},potions::Vector{0x10001,0x10001,0x10001},
            potions::Vector{0x20000,0x1ffff,0x20000},potions::Vector{0x20000,0x2011c,0x20000},
            potions::Vector{0x20000,0x20001,0x20001},
            potions::Vector{0x20000,0x20000,0x20000+257*potions::cellBytes},
            potions::Vector{0xfffffffcu,0x118,0x118}}) {
        count=99;
        check(!potions::vectorCount(bad,potions::cellBytes,256,count) && count==0,
              "malformed vector fails and clears output count");
    }
}
void normalChecks() {
    const std::vector<std::vector<potions::Cell>> originals{
        {ordinary(7,11),ordinary(2,12)},{ordinary(1,21),ordinary(19,22)}, {}};
    Scene scene(originals);const auto before=scene.memory.segments;
    auto output=std::make_unique<potions::StockSource>();
    check(scene.prepare(*output)==potions::Prepare::Added,"ordinary multiple row stock gains a potion");
    unsigned rows=0;
    check(potions::vectorCount(output->header,12,32,rows) && rows==3,"outer row count preserved");
    for(unsigned r=0;r<originals.size();++r) {
        check(outputCount(*output,r)==originals[r].size()+(r==0?1:0),"only first row gains one cell");
        for(unsigned i=0;i<originals[r].size();++i)
            check(same(outputCell(*output,r,i),originals[r][i]),"original item count bytes and order preserved");
    }
    check(same(outputCell(*output,0,2),potions::Cell{1,potions::basicPotion()}),"single canonical potion appended to first row");
    check(scene.memory.unchanged(before),"prepare never writes to source memory");
    const auto firstHeader=output->header;const auto firstRows=output->rows;
    const std::vector<potions::Cell> firstCells(output->cells.begin(),output->cells.end());
    check(scene.prepare(*output)==potions::Prepare::Added,"repeated source preparation remains valid");
    check(std::memcmp(&firstHeader,&output->header,sizeof(firstHeader))==0 &&
          std::memcmp(firstRows.data(),output->rows.data(),sizeof(firstRows))==0 &&
          std::memcmp(firstCells.data(),output->cells.data(),firstCells.size()*sizeof(potions::Cell))==0,
          "repeated preparation does not accumulate duplicate stock");
    check(scene.memory.unchanged(before),"repeated preparation leaves original source unchanged");
    Scene emptyFirst({{}, {ordinary(3,2)}});
    check(emptyFirst.prepare(*output)==potions::Prepare::Added && outputCount(*output,0)==1 &&
          outputCount(*output,1)==1,"empty first row receives potion without changing later rows");
    Scene existing({{ordinary(5,8)},{{4,potions::basicPotion()}}});
    const auto existingBefore=existing.memory.segments;
    check(existing.prepare(*output)==potions::Prepare::Present,"existing positive potion stack suppresses addition in any row");
    check(existing.memory.unchanged(existingBefore),"existing potion stock remains byte identical");
    Scene exhausted({{{0,potions::basicPotion()}}});
    check(exhausted.prepare(*output)==potions::Prepare::Added,"zero count potion is not considered available stock");
    auto higher=potions::basicPotion();setField<std::int16_t>(higher,0x10,2);
    Scene graded({{{1,higher}}});
    check(graded.prepare(*output)==potions::Prepare::Added &&
          same(outputCell(*output,0,0),potions::Cell{1,higher}),"higher tier potion remains separate and untouched");
}
void invalidChecks() {
    auto output=std::make_unique<potions::StockSource>();
    Scene noRows({});
    check(noRows.prepare(*output)==potions::Prepare::Invalid,"missing outer rows rejected");
    Scene tooManyRows(std::vector<std::vector<potions::Cell>>(33));
    check(tooManyRows.prepare(*output)==potions::Prepare::Invalid,"outer row limit enforced");
    Scene full({std::vector<potions::Cell>(256,ordinary(1,1))});
    check(full.prepare(*output)==potions::Prepare::Full,"full first row left unchanged");
    std::vector<std::vector<potions::Cell>> totalBoundary(17);
    totalBoundary[0]={ordinary(1,1)};
    for(unsigned r=1;r<16;++r) totalBoundary[r].assign(256,ordinary(1,2));
    totalBoundary[16].assign(255,ordinary(1,3));
    Scene totalFull(totalBoundary);
    check(totalFull.prepare(*output)==potions::Prepare::Full,"total full bound enforced even when first row has room");
    totalBoundary[16].pop_back();Scene lastSlot(totalBoundary);
    check(lastSlot.prepare(*output)==potions::Prepare::Added && outputCount(*output,0)==2 &&
          outputCount(*output,16)==254,"final permitted total slot can be appended without disturbing later rows");
    Scene oversized({std::vector<potions::Cell>(257,ordinary(1,1))});
    check(oversized.prepare(*output)==potions::Prepare::Invalid,"oversized row rejected");
    Scene overTotal(std::vector<std::vector<potions::Cell>>(17,std::vector<potions::Cell>(256,ordinary(1,1))));
    check(overTotal.prepare(*output)==potions::Prepare::Invalid,"total cell bound enforced before copying");
    for(unsigned stage=0;stage<3;++stage) {
        Scene unreadable({{ordinary(1,1)}});
        unreadable.memory.deny=stage==0?unreadable.source:stage==1?unreadable.outer:unreadable.rows[0].begin;
        check(unreadable.prepare(*output)==potions::Prepare::Invalid,"source header row header and cell read failures rejected");
    }
    for(const int count:{-1,1000001}) {
        auto bad=ordinary(count,2);Scene scene({{ordinary(1,1)},{bad}});
        const auto before=scene.memory.segments;
        check(scene.prepare(*output)==potions::Prepare::Invalid,"malformed late row item count invalidates whole result");
        check(scene.memory.unchanged(before),"late row failure does not alter source");
    }
    for(const int upgrades:{-1,33,std::numeric_limits<int>::max()}) {
        auto bad=ordinary(1,2);setField(bad.item,0x114,upgrades);
        Scene scene({{{1,potions::basicPotion()}},{bad}});
        check(scene.prepare(*output)==potions::Prepare::Invalid,"late upgrade overflow rejected even after existing potion");
    }
    auto edge=ordinary(1000000,2);setField<std::int32_t>(edge.item,0x114,32);
    Scene legalBoundary({{edge}});
    check(legalBoundary.prepare(*output)==potions::Prepare::Added && same(outputCell(*output,0,0),edge),
          "maximum allowed item count and upgrades preserved");
    Scene malformedLate({{ordinary(1,1)},{ordinary(1,2)}});
    auto broken=malformedLate.rows[1];++broken.end;
    // This is an input-header mutation in the fixture, never a game-memory operation.
    for(auto& segment:malformedLate.memory.segments)
        if(segment.address==malformedLate.outer)
            std::memcpy(segment.bytes.data()+sizeof(potions::Vector),&broken,sizeof(broken));
    check(malformedLate.prepare(*output)==potions::Prepare::Invalid,"malformed late row header invalidates whole result");
}
struct PriceScene {
    static constexpr unsigned gc=0x1000000,player=0x2000000,merchant=0x2100000,widget=0x2200000;
    Memory memory;
    potions::Offer offer{player,merchant,5,1,10};
    unsigned stock=0,rows=0,basicAddress=0,equipmentAddress=0,capacityAddress=0,playerCopy=0;
    PriceScene() {
        memory.at(gc+0x8006d0,player);
        memory.at<std::uint8_t>(gc+0x8009b0,0);
        memory.at(gc+0x8008d8,merchant);
        memory.at(gc+0x80095c,widget);
        memory.at<std::uint8_t>(merchant+0x140,0x80);
        memory.at<int>(widget+0x1b4,0);
        const std::vector<potions::Cell> cells{{1,potions::basicPotion()},ordinary(1,42),{1,potions::basicPotion()}};
        stock=memory.store(cells.data(),static_cast<unsigned>(cells.size()));
        rows=memory.store(potions::Vector{stock,stock+2*potions::cellBytes,stock+3*potions::cellBytes});
        memory.at(gc+0x800c0c,potions::Vector{rows,rows+12,rows+12});
        basicAddress=stock+4;equipmentAddress=stock+potions::cellBytes+4;
        capacityAddress=stock+2*potions::cellBytes+4;
        playerCopy=player+0x2000;memory.at(playerCopy,potions::basicPotion());
    }
    bool context(unsigned& selected) const {
        return potions::itemShopContext([this](unsigned address,void* target,unsigned size) {
            return memory.read(address,target,size);
        },gc,selected);
    }
    bool exact(unsigned item) const {
        return potions::exactStockItem([this](unsigned address,void* target,unsigned size) {
            return memory.read(address,target,size);
        },gc,item);
    }
    int price(unsigned item) const {
        return potions::purchasePrice([this](unsigned address,void* target,unsigned size) {
            return memory.read(address,target,size);
        },gc,item,offer);
    }
};
void priceChecks() {
    PriceScene good;unsigned selected=0;
    const auto before=good.memory.segments;
    check(good.context(selected) && selected==PriceScene::merchant,"single player Item Shop trade context identifies selected merchant");
    check(good.exact(good.basicAddress) && good.exact(good.equipmentAddress),"temporary inventory exact item bodies accepted independently of type");
    check(good.price(good.basicAddress)==10,"basic potion purchase receives accepted ten copper price");
    check(good.price(good.playerCopy)==-1,"player inventory potion and native sell pointer delegate price");
    check(!good.exact(good.playerCopy),"matching potion bytes outside trade inventory are excluded");
    check(good.price(good.equipmentAddress)==-1,"equipment price delegates to native function");
    for(const unsigned address:{0u,good.stock,good.basicAddress-1,good.basicAddress+1,
                               good.stock+potions::cellBytes,good.capacityAddress,
                               good.stock+3*potions::cellBytes+4}) {
        check(!good.exact(address) && good.price(address)==-1,
              "null count pointer misaligned pointer and capacity slots cannot receive potion price");
    }
    check(good.memory.unchanged(before),"all context item and price queries remain read only");
    for(const unsigned offset:{0u,1u,0xcu,0xdu}) {
        PriceScene changed;auto item=potions::basicPotion();++item.bytes[offset];
        changed.memory.mutate(changed.basicAddress,item);
        check(changed.price(changed.basicAddress)==-1,"other item types consumables rarity and material retain native prices");
    }
    for(const std::int16_t level:{std::int16_t(0),std::int16_t(2),std::int16_t(50)}) {
        PriceScene changed;auto item=potions::basicPotion();setField(item,0x10,level);
        changed.memory.mutate(changed.basicAddress,item);
        check(changed.price(changed.basicAddress)==-1,"higher or malformed potion grade delegates price");
    }
    PriceScene upgraded;auto item=potions::basicPotion();setField<std::int32_t>(item,0x114,1);
    upgraded.memory.mutate(upgraded.basicAddress,item);
    check(upgraded.price(upgraded.basicAddress)==-1,"upgraded potion retains native price");
    for(const std::uint8_t role:{std::uint8_t(0),std::uint8_t(0x81),std::uint8_t(0x82),std::uint8_t(0x83)}) {
        PriceScene wrong;wrong.memory.mutate<std::uint8_t>(PriceScene::merchant+0x140,role);
        check(!wrong.context(selected) && wrong.price(wrong.basicAddress)==-1,"non Item Shop NPC cannot trigger price override");
    }
    for(const int mode:{-1,1,2}) {
        PriceScene craft;craft.memory.mutate(PriceScene::widget+0x1b4,mode);
        check(!craft.context(selected) && craft.price(craft.basicAddress)==-1,"crafting and unknown widget modes retain native prices");
    }
    PriceScene multiplayer;multiplayer.memory.mutate<std::uint8_t>(PriceScene::gc+0x8009b0,1);
    check(!multiplayer.context(selected) && multiplayer.price(multiplayer.basicAddress)==-1,"multiplayer session cannot trigger override");
    for(const unsigned offset:{0x8006d0u,0x8008d8u,0x80095cu}) {
        PriceScene absent;absent.memory.mutate<unsigned>(PriceScene::gc+offset,0);
        check(!absent.context(selected) && absent.price(absent.basicAddress)==-1,
              "missing player selected merchant or inventory widget delegates price");
    }
    const std::vector<unsigned> failedReads{PriceScene::gc+0x8006d0,PriceScene::gc+0x8009b0,
        PriceScene::gc+0x8008d8,PriceScene::gc+0x80095c,PriceScene::merchant+0x140,
        PriceScene::widget+0x1b4,PriceScene::gc+0x800c0c,good.rows,good.basicAddress};
    for(const auto address:failedReads) {
        PriceScene unreadable;unreadable.memory.deny=address;
        check(unreadable.price(unreadable.basicAddress)==-1,"any required context stock header or item read failure delegates price");
    }
    PriceScene brokenOuter;
    brokenOuter.memory.mutate(PriceScene::gc+0x800c0c,potions::Vector{brokenOuter.rows,brokenOuter.rows+1,brokenOuter.rows+12});
    check(!brokenOuter.exact(brokenOuter.basicAddress) && brokenOuter.price(brokenOuter.basicAddress)==-1,
          "malformed temporary outer inventory cannot trigger override");
    PriceScene brokenRow;
    brokenRow.memory.mutate(brokenRow.rows,potions::Vector{brokenRow.stock,brokenRow.stock+1,brokenRow.stock+potions::cellBytes});
    check(!brokenRow.exact(brokenRow.basicAddress) && brokenRow.price(brokenRow.basicAddress)==-1,
          "malformed temporary item row cannot trigger override");
}
struct RecipeScene {
    static constexpr unsigned gc=0x1000000,player=0x2000000,merchant=0x2100000;
    static constexpr unsigned recipes=0x2300000,head=0x2400000,nodeBase=0x2500000;
    Memory memory;
    explicit RecipeScene(int level,const std::vector<potions::Item>& items) {
        memory.at(gc+0x8006d0,player);memory.at(player+0x190,level);
        memory.at(player+0x1d28,recipes);memory.at(recipes+0x14,head);
        const unsigned count=static_cast<unsigned>(items.size());memory.at(recipes+0x18,count);
        memory.at(head,std::array<unsigned,2>{count?nodeBase:head,count?nodeBase+(count-1)*0x200:head});
        for(unsigned i=0;i<count;++i)
            memory.at(nodeBase+i*0x200,potions::RecipeNode{
                i+1<count?nodeBase+(i+1)*0x200:head,i?nodeBase+(i-1)*0x200:head,items[i]});
    }
    bool offer(potions::Offer& output) const {
        return potions::shopOffer([this](unsigned address,void* target,unsigned size) {
            return memory.read(address,target,size);
        },gc,merchant,output);
    }
};
void progressionChecks() {
    struct Expected {int player,craft,shop,copper;};
    const Expected cases[]={{1,1,0,0},{4,1,0,0},{5,5,1,10},{8,5,1,10},
        {9,9,5,13},{12,9,5,13},{13,13,9,15},{14,13,9,15},{17,17,13,16},
        {21,21,17,18},{25,25,21,20},{53,53,49,27},{101,101,97,33}};
    for(const auto& expected:cases) {
        // Recipes are deliberately unsorted; unrelated items and future recipes are ignored.
        std::vector<potions::Item> items{ordinary(1,9).item};
        for(int level=expected.craft;level>=1;level-=4) items.push_back(potions::lifePotion(level));
        items.push_back(potions::lifePotion(expected.craft+4));
        RecipeScene scene(expected.player,items);potions::Offer offer;
        const auto before=scene.memory.segments;
        check(scene.offer(offer) && offer.craftLevel==expected.craft && offer.level==expected.shop &&
              offer.copper==expected.copper,"real unlocked recipe determines previous tier and accepted price");
        check(scene.memory.unchanged(before),"recipe traversal never changes character or recipe bytes");
    }
    RecipeScene missing(17,{potions::basicPotion()});potions::Offer offer;
    check(missing.offer(offer) && offer.craftLevel==1 && offer.level==0,
          "character level alone never invents an unlocked recipe");
    RecipeScene empty(17,{});
    check(empty.offer(offer) && offer.level==0,"empty valid recipe list creates no shop potion");
    auto padded=potions::lifePotion(13);
    for(unsigned offset:{2u,3u,0xfu,0x12u,0x13u,0x50u}) padded.bytes[offset]=0xa5;
    RecipeScene padding(14,{padded});
    check(padding.offer(offer) && offer.level==9,"native unused bytes do not hide an ordinary recipe");
    for(unsigned offset:{0u,1u,4u,8u,0xcu,0xdu,0xeu,0x114u}) {
        auto other=potions::lifePotion(13);++other.bytes[offset];
        RecipeScene scene(14,{other,potions::lifePotion(5)});
        check(scene.offer(offer) && offer.level==1,"unrelated or modified recipes cannot raise the sale tier");
    }
    for(unsigned failed:{RecipeScene::gc+0x8006d0,RecipeScene::player+0x190,
            RecipeScene::player+0x1d28,RecipeScene::recipes+0x14,RecipeScene::recipes+0x18,
            RecipeScene::head,RecipeScene::nodeBase,RecipeScene::nodeBase+0x200}) {
        RecipeScene scene(14,{potions::lifePotion(13),potions::lifePotion(5)});
        scene.memory.deny=failed;offer={1,2,13,9,15};
        check(!scene.offer(offer) && offer.level==0,"incomplete recipe read rejects and clears the whole offer");
    }
    for(int bad:{0,-1,32768,std::numeric_limits<int>::max()}) {
        RecipeScene scene(bad,{potions::lifePotion(13)});
        check(!scene.offer(offer) && offer.level==0,"invalid native character level cannot produce a sale");
    }
    for(unsigned bad:{0u,2u,potions::maxRecipes+1}) {
        RecipeScene scene(14,{potions::lifePotion(13)});scene.memory.mutate(RecipeScene::recipes+0x18,bad);
        check(!scene.offer(offer) && offer.level==0,"recipe count mismatch or overflow rejects partial data");
    }
    RecipeScene cycle(14,{potions::lifePotion(13)});
    auto node=potions::RecipeNode{RecipeScene::nodeBase,RecipeScene::head,potions::lifePotion(13)};
    cycle.memory.mutate(RecipeScene::nodeBase,node);
    check(!cycle.offer(offer) && offer.level==0,"cyclic recipe links cannot keep a partial offer");
    node.next=RecipeScene::head;node.previous=0;cycle.memory.mutate(RecipeScene::nodeBase,node);
    check(!cycle.offer(offer),"broken recipe back link rejects the list");
    int lastPrice=0;bool bounded=true;
    for(int level=1;level<=32767;level+=4) {
        const int price=potions::priceForLevel(level);
        if(price<10 || price>45 || price<lastPrice) bounded=false;
        lastPrice=price;
    }
    check(bounded,"all representable future tiers have gradual monotonic prices bounded by 45 copper");
    for(int bad:{0,-1,2,6,32768})
        check(potions::priceForLevel(bad)==0,"invalid potion tier has no custom price");
    PriceScene priced;priced.offer={PriceScene::player,PriceScene::merchant,13,9,15};
    priced.memory.mutate(priced.basicAddress,potions::lifePotion(9));
    check(priced.price(priced.basicAddress)==15,"level nine sale costs fifteen copper");
    priced.memory.at(PriceScene::player+0x190,17);
    check(priced.price(priced.basicAddress)==15,"level-up during an open shop cannot reprice its existing item");
    priced.memory.mutate(PriceScene::gc+0x8006d0,PriceScene::player+0x10000);
    check(priced.price(priced.basicAddress)==-1,"offer from another character is never reused");
    PriceScene stale;stale.offer.merchant+=4;
    check(stale.price(stale.basicAddress)==-1,"offer from another merchant is never reused");
    stale.offer={PriceScene::player,PriceScene::merchant,13,9,15};
    stale.memory.mutate(stale.basicAddress,potions::lifePotion(13));
    check(stale.price(stale.basicAddress)==-1,"current craft tier is excluded from previous-tier price");
    Scene graded({{ordinary(7,1)},{ordinary(3,2)}});
    auto stock=std::make_unique<potions::StockSource>();
    check(graded.prepare(*stock,9)==potions::Prepare::Added &&
          potions::isLifePotion(outputCell(*stock,0,1).item,9),"stock appends selected graded potion");
    Scene present({{{2,potions::lifePotion(9)}}});
    check(present.prepare(*stock,9)==potions::Prepare::Present,"matching previous-tier potion is not duplicated");
    Scene lower({{{2,potions::basicPotion()}}});
    check(lower.prepare(*stock,9)==potions::Prepare::Added && outputCount(*stock,0)==2,
          "existing lower-tier native item is preserved beside selected offer");
    check(lower.prepare(*stock,0)==potions::Prepare::Invalid,"no previous tier never creates a basic fallback");
}
}
int main() {
    itemChecks();vectorChecks();normalChecks();invalidChecks();priceChecks();progressionChecks();
    std::printf("Potion stock checks: %u passed, %u failed (%u total).\n",checks-failures,failures,checks);
    return failures?1:0;
}

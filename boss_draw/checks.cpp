#include "loot.hpp"
#include <cstdio>
#include <cstdlib>
#include <map>
static unsigned assertions=0;
static void check(bool value,const char* text){++assertions;if(!value){std::printf("FAIL: %s\n",text);std::exit(1);}}
int main(){
    loot::Rng rng{42};std::array<bool,18> weapons{};
    for(unsigned cl=1;cl<=4;++cl)for(unsigned k=0;k<2000;++k){const auto i=loot::make(cl,50,k%5,rng);
        check(loot::valid(i)&&loot::required(i)==cl&&loot::field<unsigned>(i,4)>=1&&loot::field<unsigned>(i,4)<=99,"generated equipment obeys native class, seed domain and legal fields");if(i.b[0]==3)weapons[i.b[1]]=true;}
    check(weapons[7],"crossbow is deliberately present in new Ranger pool");
    for(unsigned k=0;k<100;++k){const auto i=loot::make(0,30,2,rng);check(loot::valid(i)&&loot::required(i)==0&&(i.b[0]==8||i.b[0]==9),"universal branch produces jewelry");}
    loot::Ticket origin;origin.bossId=99;origin.cl=3;origin.bossLevel=40;origin.rank=2;origin.bossName="首领";
    auto bossItem=loot::make(1,40,4,rng);loot::set<int>(bossItem,0x114,1);bossItem.b[20]=91;origin.pool={bossItem};
    std::array<unsigned,3> branches{};unsigned forcedWhite=0;
    for(unsigned k=0;k<10000;++k){loot::State s;s.rng=k;check(loot::credit(s,origin)&&loot::reveal(s),"credit/reveal valid Boss");
        const auto& t=s.tickets.front();++branches[unsigned(t.source)];
        check(loot::valid(t.reward),"every result renderable");
        if(t.source==loot::Source::Boss)check(t.reward.b==bossItem.b,"Boss item copied including seed, quality, upgrades and native level");
        if(t.source==loot::Source::Other)check(loot::required(t.reward)!=3,"other branch cannot silently choose current class");
        const auto encoded=loot::encode(s);loot::State restored;check(loot::decode(encoded,restored)&&loot::encode(restored)==encoded,"exact persisted round trip");
        check(!loot::reveal(restored)&&loot::equal(restored.tickets.front().reward,t.reward),"reopen cannot reroll an already revealed card");
        s={};s.rng=k;s.misses=3;origin.rank=0;check(loot::credit(s,origin)&&loot::reveal(s)&&s.tickets.front().pity&&loot::required(s.tickets.front().reward)==3&&s.misses==0,"fourth after three misses is class specific and resets");
        if(s.tickets.front().reward.b[12]==0)++forcedWhite;origin.rank=2;
    }
    check(branches[0]>3700&&branches[0]<4300&&branches[1]>3700&&branches[1]<4300&&branches[2]>1700&&branches[2]<2300,"40/40/20 statistical smoke check");
    check(forcedWhite>9700,"class pity is not rarity promotion");
    loot::State state;state.rng=321;origin.pool.clear();
    for(unsigned k=0;k<loot::maxTickets;++k)check(loot::credit(state,origin),"queue accepts bounded pending rewards");
    check(!loot::credit(state,origin),"capacity never silently wraps counters");
    check(loot::encode(state).size()<loot::maxBytes,"maximum queue fits storage budget");
    check(loot::reveal(state)&&state.tickets.front().source!=loot::Source::Boss,"equipmentless Boss redistributes unavailable branch");
    auto text=loot::encode(state);loot::State out;
    for(std::size_t n=0;n<text.size();n+=113)check(!loot::decode(text.substr(0,n),out),"truncated data rejected");
    for(std::size_t n=0;n<text.size();n+=127){auto corrupt=text;corrupt[n]^=1;check(!loot::decode(corrupt,out),"corrupt data rejected without resetting rewards");}
    state.tickets.front().stage=loot::Stage::Claiming;check(loot::decode(loot::encode(state),out)&&!loot::reveal(out),"interrupted grant remains locked after reload");
    for(int l:{1,2,30,60,200,32767})for(int k=0;k<100;++k){int result=loot::jitter(l,rng);check(result>=1&&result<=32767,"level jitter stays inside native signed field");}
    std::printf("PASS %u loot assertions; sources=%u/%u/%u. Offline only.\n",assertions,branches[0],branches[1],branches[2]);
}

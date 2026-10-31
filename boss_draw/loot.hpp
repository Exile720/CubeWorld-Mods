#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <limits>

namespace loot {
constexpr unsigned deathFunction=0x59b180,addFunction=0x46ebe0,maxTickets=128,maxBytes=400000;
struct Item {std::array<unsigned char,0x118> b{};};
template<class T>T field(const Item& i,unsigned at){T v{};std::memcpy(&v,i.b.data()+at,sizeof(v));return v;}
template<class T>void set(Item& i,unsigned at,T v){std::memcpy(i.b.data()+at,&v,sizeof(v));}
inline int level(const Item& i){return field<short>(i,16);}
inline bool valid(const Item& i){
    if(i.b[0]<3||i.b[0]>9||i.b[12]>4||level(i)<1||i.b[13]>27)return false;
    if(i.b[0]==3&&(i.b[1]>17||i.b[1]==9||i.b[1]==14))return false;
    return field<int>(i,0x114)>=0&&field<int>(i,0x114)<=32;
}
inline unsigned required(const Item& i){
    if(i.b[0]==3){switch(i.b[1]){
        case 0:case 1:case 2:case 13:case 15:case 16:case 17:return 1;
        case 3:case 4:case 5:return 4;case 6:case 7:case 8:return 2;
        case 10:case 11:case 12:return 3;default:return 0;}}
    if(i.b[0]>=4&&i.b[0]<=7){switch(i.b[13]){case 1:return 1;case 25:return 3;case 26:return 2;case 27:return 4;}}
    return 0;
}
inline bool equal(const Item& a,const Item& b){
    return a.b[0]==b.b[0]&&a.b[1]==b.b[1]&&std::memcmp(a.b.data()+4,b.b.data()+4,11)==0&&
        std::memcmp(a.b.data()+16,b.b.data()+16,2)==0&&std::memcmp(a.b.data()+20,b.b.data()+20,260)==0;
}
inline std::uint64_t hash(const void* data,std::size_t n){
    auto p=static_cast<const unsigned char*>(data);std::uint64_t h=14695981039346656037ull;
    for(std::size_t k=0;k<n;++k){h^=p[k];h*=1099511628211ull;}return h;
}
struct Rng {
    std::uint64_t state;
    std::uint64_t next(){std::uint64_t z=(state+=0x9e3779b97f4a7c15ull);z=(z^(z>>30))*0xbf58476d1ce4e5b9ull;z=(z^(z>>27))*0x94d049bb133111ebull;return z^(z>>31);}
    unsigned pick(unsigned n){if(!n)return 0;const auto limit=(std::numeric_limits<std::uint64_t>::max()-n+1)%n;std::uint64_t v;do{v=next();}while(v<limit);return unsigned(v%n);}
};
inline float curve(int l){return 1.f-1.f/((float(l)-1.f)*.05f+1.f);}
inline int power(const Item& i){return int(curve(level(i))*100.f+1.f);}
inline int jitter(int l,Rng& r){
    if(l<=1)return 1;const float c=curve(l),lo=c-.05f,hi=(std::min)(.9999f,c+.05f);
    const auto inverse=[](float f){return (std::clamp)(int((1.f/(1.f-f)-1.f)*20.f+1.f),1,32767);};
    const int a=lo<=0?1:inverse(lo),b=(std::max)(a,inverse(hi));return a+int(r.pick(unsigned(b-a+1)));
}
inline unsigned quality(unsigned rank,Rng& r){unsigned q=r.pick(rank+1);q+=r.pick(100)==0;q+=r.pick(1000)==0;q+=r.pick(10000)==0;return (std::min)(4u,q);}
inline Item make(unsigned cl,int l,unsigned q,Rng& r){
    Item i;unsigned sub=0,mat=1,type=3;
    if(cl==0){type=8+r.pick(2);mat=11+r.pick(2);}
    else if(r.pick(2)){
        if(cl==1){unsigned k=r.pick(3);sub=k==0?r.pick(3):(k==1?15+r.pick(3):13);}
        if(cl==2){sub=6+r.pick(3);mat=2;}
        if(cl==3){sub=10+r.pick(3);mat=sub==12?11+r.pick(2):2;}
        if(cl==4){sub=3+r.pick(3);}
    }else{type=4+r.pick(4);mat=cl==1?1:(cl==3?25:(cl==2?26:27));}
    i.b[0]=static_cast<unsigned char>(type);i.b[1]=static_cast<unsigned char>(sub);
    i.b[12]=static_cast<unsigned char>(q);i.b[13]=static_cast<unsigned char>(mat);
    set<short>(i,16,static_cast<short>(l));set<std::uint32_t>(i,4,1+r.pick(99));return i;
}
enum class Stage:unsigned {Sealed,Revealed,Claiming};
enum class Source:unsigned {OwnClass,Boss,Other};
struct Ticket {
    std::uint64_t serial=0,bossId=0,seed=0;int bossLevel=1;unsigned rank=0,cl=0;
    std::string bossName;std::vector<Item> pool;Stage stage=Stage::Sealed;
    Item reward;Source source=Source::Other;bool pity=false;int beforeCount=0;
};
struct State {std::uint64_t nextSerial=1,rng=0;unsigned misses=0;std::vector<Ticket> tickets;};
inline bool credit(State& s,Ticket t){
    if(s.tickets.size()>=maxTickets||t.cl<1||t.cl>4||t.rank>4||t.bossLevel<1||t.bossLevel>32767||t.pool.size()>7||t.bossName.size()>96)return false;
    for(const auto& i:t.pool)if(!valid(i))return false;
    if(s.nextSerial==std::numeric_limits<std::uint64_t>::max())return false;
    t.serial=s.nextSerial++;Rng r{s.rng};t.seed=r.next();s.rng=r.state;s.tickets.push_back(std::move(t));return true;
}
inline bool reveal(State& s){
    if(s.tickets.empty()||s.tickets.front().stage!=Stage::Sealed)return false;
    auto& t=s.tickets.front();Rng r{t.seed};const unsigned q=quality(t.rank,r);
    const int l=jitter((std::max)(1,t.bossLevel-1+int(r.pick(2))),r);
    unsigned branch=r.pick(100);t.pity=s.misses>=3;
    if(t.pity)branch=0;
    if(branch>=40&&branch<80&&t.pool.empty())branch=r.pick(3)?0:99;
    if(branch<40){t.source=Source::OwnClass;t.reward=make(t.cl,l,q,r);}
    else if(branch<80){t.source=Source::Boss;t.reward=t.pool[r.pick(unsigned(t.pool.size()))];}
    else{t.source=Source::Other;unsigned cl=r.pick(4);if(cl){unsigned n=0;for(unsigned c=1;c<=4;++c)if(c!=t.cl&&++n==cl){cl=c;break;}}t.reward=make(cl,l,q,r);}
    if(!valid(t.reward))return false;
    s.misses=required(t.reward)==t.cl?0:(std::min)(3u,s.misses+1);t.stage=Stage::Revealed;return true;
}
struct Writer {
    std::string b;
    template<class T>void put(T v){const auto p=reinterpret_cast<const char*>(&v);b.append(p,sizeof(v));}
    void item(const Item& i){b.append(reinterpret_cast<const char*>(i.b.data()),i.b.size());}
    void text(const std::string& s){put<unsigned>(unsigned(s.size()));b+=s;}
};
struct Reader {
    const std::string& b;std::size_t pos=0;bool ok=true;
    template<class T>T get(){T v{};if(pos+sizeof(v)>b.size()){ok=false;return v;}std::memcpy(&v,b.data()+pos,sizeof(v));pos+=sizeof(v);return v;}
    Item item(){Item i;if(pos+i.b.size()>b.size()){ok=false;return i;}std::memcpy(i.b.data(),b.data()+pos,i.b.size());pos+=i.b.size();return i;}
    std::string text(){const unsigned n=get<unsigned>();if(n>96||pos+n>b.size()){ok=false;return {};}auto s=b.substr(pos,n);pos+=n;return s;}
};
inline std::string encode(const State& s){
    Writer w;w.b="CWBOSS01";w.put(s.nextSerial);w.put(s.rng);w.put(s.misses);w.put<unsigned>(unsigned(s.tickets.size()));
    for(const auto& t:s.tickets){w.put(t.serial);w.put(t.bossId);w.put(t.seed);w.put(t.bossLevel);w.put(t.rank);w.put(t.cl);w.text(t.bossName);
        w.put<unsigned>(unsigned(t.pool.size()));for(const auto& i:t.pool)w.item(i);
        w.put<unsigned>(unsigned(t.stage));w.item(t.reward);w.put<unsigned>(unsigned(t.source));w.put<unsigned>(t.pity?1:0);w.put(t.beforeCount);}
    w.put(hash(w.b.data(),w.b.size()));return w.b;
}
inline bool decode(const std::string& b,State& out){
    if(b.size()<40||b.size()>maxBytes||b.substr(0,8)!="CWBOSS01")return false;
    std::uint64_t checksum=0;std::memcpy(&checksum,b.data()+b.size()-8,8);if(checksum!=hash(b.data(),b.size()-8))return false;
    Reader r{b,8};State s;s.nextSerial=r.get<std::uint64_t>();s.rng=r.get<std::uint64_t>();s.misses=r.get<unsigned>();const unsigned n=r.get<unsigned>();
    if(!s.nextSerial||s.misses>3||n>maxTickets)return false;std::uint64_t previous=0;
    for(unsigned k=0;k<n&&r.ok;++k){Ticket t;t.serial=r.get<std::uint64_t>();t.bossId=r.get<std::uint64_t>();t.seed=r.get<std::uint64_t>();t.bossLevel=r.get<int>();t.rank=r.get<unsigned>();t.cl=r.get<unsigned>();t.bossName=r.text();
        const unsigned count=r.get<unsigned>();if(count>7)return false;for(unsigned i=0;i<count;++i){auto v=r.item();if(!valid(v))return false;t.pool.push_back(v);}
        const unsigned stage=r.get<unsigned>();t.reward=r.item();const unsigned source=r.get<unsigned>(),pity=r.get<unsigned>();t.beforeCount=r.get<int>();
        if(!t.serial||t.serial<=previous||t.serial>=s.nextSerial||t.bossLevel<1||t.bossLevel>32767||t.rank>4||t.cl<1||t.cl>4||stage>2||source>2||pity>1||t.beforeCount<0||t.beforeCount>4096||t.bossName.find('\0')!=std::string::npos)return false;
        if((k&&stage!=0)||(stage&&!valid(t.reward)))return false;
        t.stage=static_cast<Stage>(stage);t.source=static_cast<Source>(source);t.pity=pity!=0;previous=t.serial;s.tickets.push_back(std::move(t));}
    if(!r.ok||r.pos+8!=b.size())return false;out=std::move(s);return true;
}
constexpr std::array<unsigned,7> slots{0x990,0xaa8,0x530,0x878,0x760,0x648,0x418};
// 1 = found, 0 = confirmed absent, -1 = unreadable. A missing snapshot is never a death.
template<class Read>int entityAt(Read read,unsigned gc,std::uint64_t id,unsigned& entity){
    unsigned head=0,node=0;entity=0;if(!read(gc+0x2e8,&head,4)||!head||!read(head+4,&node,4))return -1;
    std::array<unsigned,64> visited{};unsigned count=0;
    while(node!=head){if(!node||count==visited.size()||std::find(visited.begin(),visited.begin()+count,node)!=visited.begin()+count)return -1;visited[count++]=node;
        std::array<unsigned char,0x1c> v{};if(!read(node,v.data(),unsigned(v.size()))||v[0xd])return -1;
        std::uint64_t key=0;std::memcpy(&key,v.data()+0x10,8);if(key==id){std::memcpy(&entity,v.data()+0x18,4);return entity?1:-1;}
        std::memcpy(&node,v.data()+(id<key?0:8),4);}
    return 0;
}
// Read the local player's contribution, never infer a kill from range or disappearance.
template<class Read>bool contributed(Read read,unsigned enemy,std::uint64_t playerId){
    unsigned head=0,node=0;if(!read(enemy+0x13a4,&head,4)||!head||!read(head+4,&node,4))return false;
    std::array<unsigned,64> visited{};unsigned count=0;
    while(node!=head){if(!node||count==visited.size()||std::find(visited.begin(),visited.begin()+count,node)!=visited.begin()+count)return false;visited[count++]=node;
        std::array<unsigned char,0x1c> v{};if(!read(node,v.data(),unsigned(v.size()))||v[0xd])return false;
        std::uint64_t key=0;std::memcpy(&key,v.data()+0x10,8);if(key==playerId){float amount=0;std::memcpy(&amount,v.data()+0x18,4);return std::isfinite(amount)&&amount>0;}
        std::memcpy(&node,v.data()+(playerId<key?0:8),4);}
    return false;
}
template<class Read>bool snapshot(Read read,unsigned enemy,std::uint64_t playerId,Ticket& t){
    unsigned char category=0,rank=0;unsigned short flags=0;float health=1;
    if(!enemy||!read(enemy+0x60,&category,1)||category!=1||!read(enemy+0x7e,&flags,2)||(flags&0x200)==0||(flags&0x800)||
       !read(enemy+0x16c,&health,4)||!std::isfinite(health)||health>0||!contributed(read,enemy,playerId)||
       !read(enemy+8,&t.bossId,8)||!read(enemy+0x190,&t.bossLevel,4)||t.bossLevel<1||t.bossLevel>32767||
       !read(enemy+0x1a8,&rank,1)||rank>4)return false;t.rank=rank;
    t.pool.clear();for(unsigned off:slots){Item i;if(!read(enemy+off,i.b.data(),unsigned(i.b.size())))return false;if(!i.b[0])continue;if(!valid(i))return false;t.pool.push_back(i);}return true;
}
struct Vector {unsigned begin,end,cap;};
template<class Read>bool inventoryCount(Read read,unsigned player,const Item& wanted,int& total){
    total=0;Vector outer{};if(!read(player+0x11dc,&outer,sizeof(outer)))return false;
    auto size=[](Vector v,unsigned stride,unsigned max,unsigned& n){n=0;if(!v.begin)return !v.end&&!v.cap;
        if(v.begin%4||v.end<v.begin||v.cap<v.end||(v.end-v.begin)%stride||(v.cap-v.begin)%stride||(v.cap-v.begin)/stride>max)return false;n=(v.end-v.begin)/stride;return true;};
    unsigned rows=0,cells=0;if(!size(outer,12,32,rows))return false;
    for(unsigned row=0;row<rows;++row){Vector v{};unsigned n=0;if(!read(outer.begin+row*12,&v,12)||!size(v,0x11c,256,n)||(cells+=n)>4096)return false;
        for(unsigned col=0;col<n;++col){std::array<unsigned char,0x11c> cell{};if(!read(v.begin+col*0x11c,cell.data(),unsigned(cell.size())))return false;
            int count=0;std::memcpy(&count,cell.data(),4);if(count<0||count>50)return false;Item i;std::memcpy(i.b.data(),cell.data()+4,i.b.size());
            if(count&&equal(i,wanted)){if(total>4096-count)return false;total+=count;}}}return true;
}
inline const char* className(unsigned c){switch(c){case 1:return "战士";case 2:return "游侠";case 3:return "法师";case 4:return "盗贼";default:return "通用";}}
inline const char* grade(unsigned q){constexpr const char* v[]{"普通 · 白","优秀 · 绿","稀有 · 蓝","史诗 · 紫","传说 · 金"};return q<5?v[q]:"未知";}
inline std::string name(const Item& i){
    if(i.b[0]==3){constexpr const char* v[]{"单手剑","单手斧","单手锤","匕首","拳套","长剑","弓","弩","回旋镖","未知","法杖","魔杖","手镯","盾牌","未知","双手剑","双手斧","双手锤"};return i.b[1]<18?v[i.b[1]]:"武器";}
    switch(i.b[0]){case 4:return "胸甲";case 5:return "手套";case 6:return "鞋子";case 7:return "肩甲";case 8:return "项链";case 9:return "戒指";default:return "装备";}
}
}

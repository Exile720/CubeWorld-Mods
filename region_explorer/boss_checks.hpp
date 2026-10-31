#pragma once
#include "boss.hpp"
#include <cstdlib>

template<class Check>inline void bossChecks(Check check) {
    using namespace explore;
    for(unsigned seed:{0u,1u,42u,32767u,0x7fffffffu,0xffffffffu}) {
        const std::uint64_t id=0x00800a800a00ull;
        std::srand(seed+static_cast<unsigned>(id));const unsigned expected=std::rand()%4+1;
        check(boss::predict(seed,id,2,0x1200,0)==expected,"independent generator matches the MSVC first random roll including uint32 overflow");
    }
    check(boss::predict(1,7,97,0x1200,0)==4,"Vampire's verified class override remains rogue regardless of the random roll");
    check(!boss::predict(1,7,115,0x1200,0)&&!boss::predict(1,7,43,0x1200,0),"special bosses and excluded wizard path are never predicted as regular classes");
    check(!boss::predict(1,7,2,0x1000,0)&&!boss::predict(1,7,2,0x200,0),"both mission and giant-leader spawn flags are necessary");
    check(!boss::predict(1,7,2,0x1240,0)&&!boss::predict(1,7,2,0x1210,0)&&!boss::predict(1,7,2,0x1200,128),"native early-exit paths and ability class bytes fail closed");
    constexpr unsigned gc=0x01000000,region=0x11000000,zone=0x12000000,spawn=0x13000000,vector=0x14000000,head=0x15000000,node=0x16000000,entity=0x17000000;
    const nav::Point player{8391168,8391168,80};const unsigned zx=32778,zy=32778,bx=zx/8,by=zy/8;
    const std::uint64_t id=((std::uint64_t(zy)<<16)+zx)*256;
    using Memory=std::map<unsigned,std::vector<unsigned char>>;Memory memory;
    auto put=[&](unsigned address,const auto& value){auto& b=memory[address];b.resize(sizeof(value));std::memcpy(b.data(),&value,sizeof(value));};
    auto fieldPut=[](auto& b,unsigned off,const auto& value){std::memcpy(b.data()+off,&value,sizeof(value));};
    const Reader read=[&](unsigned address,void* out,unsigned size){for(const auto& entry:memory)if(address>=entry.first&&std::uint64_t(address)-entry.first+size<=entry.second.size()){std::memcpy(out,entry.second.data()+address-entry.first,size);return true;}return false;};
    std::array<unsigned char,recordSize> record{};
    fieldPut(record,0,std::int64_t(player.x*65536));fieldPut(record,8,std::int64_t(player.y*65536));
    fieldPut(record,0x18,14u);fieldPut(record,0x1c,5u);fieldPut(record,0x20,1863u);fieldPut(record,0x24,18u);
    fieldPut(record,0x30,32199u);fieldPut(record,0x34,5u);fieldPut(record,0x38,2u);fieldPut(record,0x3c,19u);
    Place place;check(parse(record.data(),bx,by,80,place)&&place.missionSeed==32199&&place.missionRace==2&&place.missionLevel==19,"task seed, species and monster level are read from their distinct native fields");
    const unsigned recordAddress=region+0x14018+((bx%8)*8+by%8)*recordSize;
    put(gc+0x2e4+0xbc+((zx/64)*1024+zy/64)*4,region);
    put(region+0x10018+((zx%64)*64+zy%64)*4,zone);put(recordAddress,record);
    put(zone+0x18,std::array<unsigned,2>{vector,vector+4});put(vector,spawn);
    std::array<unsigned char,0x80> spawnData{};
    fieldPut(spawnData,0x10,std::int64_t(player.x*65536));fieldPut(spawnData,0x18,std::int64_t(player.y*65536));fieldPut(spawnData,0x20,std::int64_t(player.z*65536));
    fieldPut(spawnData,0x28,1u);fieldPut(spawnData,0x2c,2u);fieldPut(spawnData,0x34,19u);fieldPut(spawnData,0x48,id);fieldPut(spawnData,0x7a,std::uint16_t{0x1200});spawnData[0x58]=1;
    put(spawn,spawnData);put(gc+0x2e8,head);put(head+4,head);
    const auto pristine=memory;
    auto run=[&](bool allow=true){Snapshot s;s.places={place};const auto stats=boss::enrich(s,read,gc,player,allow);return std::make_pair(s.places.front(),stats);};
    const auto predicted=run();
    check(predicted.second.candidates==1&&predicted.second.predicted==1&&predicted.first.boss.source==BossSource::Predicted,"resident mission leader without a live entity receives a clearly separated prediction");
    check(bossLabel(predicted.first)==std::string("首领：")+combatClassName(predicted.first.boss.combatClass)+" · 预测","prediction is never labeled confirmed in the UI");
    check(memory==pristine,"prediction leaves game data and the native RNG state untouched");
    check(run(false).first.boss.source==BossSource::Unknown,"a disabled prediction gate retains unknown instead of guessing");
    std::array<unsigned char,0x1c> nodeData{};fieldPut(nodeData,0,head);fieldPut(nodeData,8,head);fieldPut(nodeData,0x10,id);fieldPut(nodeData,0x18,entity);
    put(node,nodeData);put(head+4,node);
    std::array<unsigned char,0x1a9> entityData{};entityData[0x60]=1;entityData[0x140]=static_cast<unsigned char>(predicted.first.boss.combatClass);entityData[0x1a8]=1;
    fieldPut(entityData,0x64,2u);fieldPut(entityData,0x190,19u);fieldPut(entityData,0x16c,100.f);put(entity,entityData);
    const auto confirmedMemory=memory;
    const auto confirmed=run();
    check(confirmed.first.boss.source==BossSource::Confirmed&&confirmed.second.mismatches==0&&confirmed.second.confirmed==1,"exact spawn ID and current task identify the live leader independently of nearest enemy or camera visibility");
    check(bossLabel(confirmed.first).find("已确认")!=std::string::npos&&memory==confirmedMemory,"confirmation is labeled separately and performs no game writes");
    entityData[0x140]=entityData[0x140]%4+1;put(entity,entityData);
    check(run().second.mismatches==1&&run().first.boss.source==BossSource::Confirmed,"live class overrides a differing prediction and reports the mismatch");
    memory=confirmedMemory;fieldPut(entityData,0x64,9u);put(entity,entityData);
    check(run().first.boss.source==BossSource::Unknown,"reused or mismatched entity pointer cannot confirm a different creature");
    memory=confirmedMemory;memory.erase(entity);
    check(run().first.boss.source==BossSource::Unknown,"unreadable present entity cannot fall back to a seemingly safe prediction");
    memory=pristine;auto changed=record;fieldPut(changed,0x30,32200u);put(recordAddress,changed);
    check(run().first.boss.source==BossSource::Unknown,"changed native task seed invalidates a stale coarse-map prediction");
    memory=pristine;auto changedSpawn=spawnData;fieldPut(changedSpawn,0x48,id+1);put(spawn,changedSpawn);
    check(run().first.boss.source==BossSource::Unknown,"spawn ID must match its generating zone and index exactly");
    memory=pristine;changedSpawn=spawnData;fieldPut(changedSpawn,0x7a,std::uint16_t{0x1000});put(spawn,changedSpawn);
    check(run().first.boss.source==BossSource::Unknown,"ordinary mission monsters cannot masquerade as the leader");
    memory=pristine;changedSpawn=spawnData;fieldPut(changedSpawn,0x10,std::int64_t((player.x+2048)*65536));put(spawn,changedSpawn);
    check(run().first.boss.source==BossSource::Unknown,"leader's original spawn must belong to this area rather than the nearest landmark");
    memory=pristine;put(zone+0x18,std::array<unsigned,2>{vector,vector+8});put(vector,std::array<unsigned,2>{spawn,spawn+0x1000});changedSpawn=spawnData;fieldPut(changedSpawn,0x48,id+1);put(spawn+0x1000,changedSpawn);
    check(run().first.boss.source==BossSource::Unknown&&run().second.candidates==2,"multiple leaders remain unknown rather than choosing an arbitrary class");
    memory=pristine;put(zone+0x18,std::array<unsigned,2>{vector,vector+4097*4});
    check(run().second.truncated&&run().first.boss.source==BossSource::Unknown,"oversized resident vectors fail closed with bounded work");
    memory=pristine;put(head+4,node);nodeData[0xd]=0;fieldPut(nodeData,0x10,id+1);fieldPut(nodeData,0,node);put(node,nodeData);
    check(run().first.boss.source==BossSource::Unknown,"cyclic entity trees terminate without generating a result");
    memory=confirmedMemory;changedSpawn=spawnData;fieldPut(changedSpawn,0x2c,115u);put(spawn,changedSpawn);entityData={};entityData[0x60]=1;entityData[0x1a8]=1;entityData[0x140]=1;fieldPut(entityData,0x64,115u);fieldPut(entityData,0x190,19u);fieldPut(entityData,0x16c,100.f);put(entity,entityData);
    check(run().first.boss.source==BossSource::Special&&bossLabel(run().first)=="特殊首领 · 已确认","special species are not forcibly described as player combat classes");
    memory=pristine;place.state=2;
    check(run().second.candidates==0&&bossLabel(run().first)=="首领职业未记录","completed tasks do not invent historical class data");
    place.state=1;place.mission=0;
    check(run().second.candidates==0&&run().first.boss.source==BossSource::Unknown,"retained state without a current task cannot reuse a mission seed");
    bool called=false;const Reader trap=[&](unsigned,void*,unsigned){called=true;return true;};unsigned value=0;
    check(!boss::readAt(trap,0xfffffffeull,&value,4)&&!called,"address arithmetic cannot wrap into an unrelated low memory read");
}

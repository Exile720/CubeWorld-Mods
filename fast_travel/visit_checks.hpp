#pragma once
#include "visit.hpp"
#include <map>

struct VisitFixture {
    static constexpr unsigned gc=0x01000000,region=0x11000000,zone=0x12000000,spawn=0x13000000,vector=0x14000000,head=0x15000000,node=0x16000000,entity=0x17000000,chunk=0x18000000;
    const travel::Point point;
    unsigned zx=unsigned(point.x/256),zy=unsigned(point.y/256),bx=zx/8,by=zy/8;
    std::uint64_t id=((std::uint64_t(zy)<<16)+zx)*256;
    std::map<unsigned,std::vector<unsigned char>> memory;
    explore::Place place;
    template<class T>void put(unsigned address,const T& value){auto& b=memory[address];b.resize(sizeof(value));std::memcpy(b.data(),&value,sizeof(value));}
    template<class T>void field(unsigned address,unsigned offset,const T& value){std::memcpy(memory.at(address).data()+offset,&value,sizeof(value));}
    bool read(unsigned address,void* out,unsigned bytes)const{for(const auto& v:memory)if(address>=v.first&&std::uint64_t(address)-v.first+bytes<=v.second.size()){std::memcpy(out,v.second.data()+address-v.first,bytes);return true;}return false;}
    unsigned liveRecord()const{return region+0x14018+((bx%8)*8+by%8)*explore::recordSize;}
    unsigned coarseRecord()const{return chunk+explore::chunkPlaces+((bx%8)*8+by%8)*explore::recordSize;}
    void word(unsigned address,const std::wstring& text){std::array<unsigned char,24> b{};const unsigned size=unsigned(text.size()),cap=7;std::memcpy(b.data(),text.c_str(),(size+1)*2);std::memcpy(b.data()+0x10,&size,4);std::memcpy(b.data()+0x14,&cap,4);put(address,b);}
    explicit VisitFixture(travel::Point at={8395264.375,8395264.25,80.125}):point(at){
        std::array<unsigned char,explore::recordSize> record{};const auto position=travel::fixed(point);
        const auto f=[&](unsigned off,const auto& value){std::memcpy(record.data()+off,&value,sizeof(value));};
        f(0,position.x);f(8,position.y);f(0x18,14u);f(0x1c,5u);f(0x20,1863u);f(0x24,18u);f(0x30,32199u);f(0x34,5u);f(0x38,2u);f(0x3c,19u);
        explore::parse(record.data(),bx,by,point.z,place);place.name="测试金字塔";
        put(gc+0x2e4+0xbc+((zx/64)*1024+zy/64)*4,region);put(region+0x10018+((zx%64)*64+zy%64)*4,zone);put(liveRecord(),record);
        put(gc+0x800d44+0xb0+((bx/8)*1024+by/8)*4,chunk);put(coarseRecord(),record);
        put(zone+0x18,std::array<unsigned,2>{vector,vector+4});put(vector,spawn);
        std::array<unsigned char,0x80> spawnData{};put(spawn,spawnData);
        field(spawn,0x10,position.x);field(spawn,0x18,position.y);field(spawn,0x20,position.z);field(spawn,0x28,1u);field(spawn,0x2c,2u);field(spawn,0x34,19u);field(spawn,0x48,id);field(spawn,0x58,std::uint8_t{1});field(spawn,0x7a,std::uint16_t{0x1200});
        put(gc+0x2e8,head);put(head+4,node);std::array<unsigned char,0x1c> links{};put(node,links);field(node,0,head);field(node,8,head);field(node,0x10,id);field(node,0x18,entity);
        std::array<unsigned char,0x1178> creature{};put(entity,creature);field(entity,8,id);field(entity,0x10,position.x);field(entity,0x18,position.y);field(entity,0x20,position.z);field(entity,0x60,std::uint8_t{1});field(entity,0x64,2u);field(entity,0x7e,std::uint16_t{0x200});field(entity,0x140,std::uint8_t{3});field(entity,0x16c,100.f);field(entity,0x190,19u);field(entity,0x1a8,std::uint8_t{1});
        for(unsigned i=0;i<10;++i){word(0x76bbd0+i*24,L"古");word(0x76bcc0+i*24,L"龙");}
        for(unsigned i=0;i<20;++i){word(0x76b808+i*24,L"石");word(0x76b9f0+i*24,L"王");}
    }
    travel::VisitInfo observe(travel::VisitInfo previous={})const{return visits::observe(place,previous,[&](unsigned a,void* b,unsigned n){return read(a,b,n);},[](unsigned a){return a;},gc,point,true);}
};

template<class Check>void visitChecks(Check check){
    VisitFixture fixture;const auto original=fixture.memory;const auto actual=fixture.observe();
    check(actual.placeId==fixture.place.id+1&&actual.bossId==fixture.id&&actual.bossClass==3&&actual.bossName=="古龙","exact native task and entity identity record actual boss name and mage class");
    check(actual.title=="金字塔 · 测试金字塔"&&actual.level==18&&fixture.memory==original,"automatic basic information is native, bounded and read-only");
    check(visits::text(actual).find("上次现场记录")!=std::string::npos,"saved boss information is explicitly a field observation");
    fixture.memory.erase(VisitFixture::entity);check(fixture.observe().bossId==0,"generated spawn prediction alone cannot become a confirmed boss record");
    check(visits::same(fixture.observe(actual),actual),"temporary entity unloading preserves previous confirmed field observation");
    fixture.memory=original;fixture.field(VisitFixture::entity,8,fixture.id+1);check(fixture.observe().bossId==0,"mismatched live entity identity cannot supply another creature's boss name");
    fixture.memory=original;fixture.field(VisitFixture::entity,0x140,std::uint8_t{0});const auto unnamedClass=fixture.observe();check(unnamedClass.bossId==fixture.id&&!unnamedClass.bossClass&&unnamedClass.bossName=="古龙"&&travel::validVisit(unnamedClass),"known boss name may be recorded while its class remains unknown");
    fixture.memory=original;fixture.field(VisitFixture::entity,0x7e,std::uint16_t{0});const auto noFlag=fixture.observe();check(noFlag.bossId==fixture.id&&noFlag.bossClass==3&&noFlag.bossName.empty(),"missing native name-generation branch does not invent a display name");
    fixture.memory=original;const char custom[16]="Grim";fixture.field(VisitFixture::entity,0x1168,custom);check(fixture.observe().bossName=="Grim","explicit creature name takes precedence over generated syllables");
    fixture.memory=original;fixture.field(VisitFixture::entity,0x1168,std::array<unsigned char,16>{0xff,0});check(fixture.observe().bossName.empty(),"unverified custom-name encoding stays unknown");
    fixture.memory=original;for(unsigned i=0;i<10;++i){fixture.word(0x76bbd0+i*24,L"甲"+std::to_wstring(i));fixture.word(0x76bcc0+i*24,L"乙"+std::to_wstring(i));}
    const std::uint64_t overflowId=0x1234ffffffffull;fixture.field(VisitFixture::entity,8,overflowId);
    const auto read=[&](unsigned a,void* b,unsigned n){return fixture.read(a,b,n);};
    check(visits::bossName(read,[](unsigned a){return a;},VisitFixture::entity,overflowId)=="甲3乙2","native name indexes preserve 32-bit overflow semantics");
    fixture.memory=original;fixture.place.missionSeed++;const auto changed=fixture.observe(actual);check(!changed.bossId&&changed.bossName.empty()&&!changed.bossClass&&changed.taskKey!=actual.taskKey,"a new task identity clears stale boss data until independently confirmed");
    fixture.place.mission=0;fixture.place.state=2;const auto completed=fixture.observe(actual);check(completed.bossId==actual.bossId&&completed.bossName==actual.bossName,"cleared task retains previously observed boss information as history");
    VisitFixture special;special.place.missionRace=115;special.field(special.liveRecord(),0x38,115u);special.field(VisitFixture::spawn,0x2c,115u);special.field(VisitFixture::entity,0x64,115u);const auto unusual=special.observe();
    check(unusual.special&&!unusual.bossClass&&unusual.bossName=="石王","special bosses retain native names without forcing a player combat class");
    VisitFixture two;auto duplicate=two.memory[VisitFixture::spawn];two.memory[VisitFixture::spawn+0x1000]=duplicate;two.field(VisitFixture::spawn+0x1000,0x48,two.id+1);two.put(VisitFixture::vector,std::array<unsigned,2>{VisitFixture::spawn,VisitFixture::spawn+0x1000});two.put(VisitFixture::zone+0x18,std::array<unsigned,2>{VisitFixture::vector,VisitFixture::vector+8});
    check(!two.observe().bossId,"multiple matching leaders do not select an arbitrary boss name");
    visits::Dwell dwell;check(!dwell.ready(4,{1,2,3},100)&&!dwell.ready(4,{1,2,3},200)&&dwell.ready(4,{1,2,3},3100),"auto-registration requires separate stationary arrival samples");
    check(!dwell.ready(4,{20,2,3},3200)&&!dwell.ready(5,{20,2,3},6200),"moving or switching places resets the arrival check");
    travel::Station saved{"我的名字",{1.375,2.25,3.125},0,"手写打法",actual};std::vector<travel::Station> decoded;
    check(travel::decode(travel::encode({saved}),decoded)&&visits::same(decoded[0].visit,actual)&&decoded[0].note==saved.note,"v4 persists automatic information separately from personal text");
    check(travel::decode("CWTRAVEL3\n1\n\"旧地点\" 1.375 2.25 3.125 0 \"Boss：法师\n自己写的打法\"\n",decoded)&&!decoded[0].visit.placeId&&decoded[0].note=="Boss：法师\n自己写的打法","v3 personal records retain multiline notes without invented automatic metadata");
    auto invalid=actual;invalid.bossClass=5;check(!travel::validVisit(invalid),"invalid automatic class values are rejected");
    VisitFixture resident;resident.field(resident.coarseRecord(),0x34,0u);resident.field(resident.liveRecord(),0x34,0u);resident.field(VisitFixture::spawn,0x7a,std::uint16_t{0x200});
    const auto collect=[&](){return visits::nearbyBosses([&](unsigned a,void* b,unsigned n){return resident.read(a,b,n);},[](unsigned a){return a;},VisitFixture::gc,resident.point);};
    const auto staticBoss=collect();check(staticBoss.size()==1&&staticBoss[0].id==resident.id&&staticBoss[0].combatClass==3&&staticBoss[0].name=="古龙","actual static giant leader is observed without mission type 5 or mission flag");
    check(travel::validVisit(visits::information(staticBoss[0]))&&visits::information(staticBoss[0]).taskKey!=0,"static encounter receives a stable native place identity and valid persistent metadata");
    resident.field(VisitFixture::entity,0x16c,0.f);check(collect().size()==1,"still-resident defeated boss retains readable name and class");
    resident.field(VisitFixture::entity,8,resident.id+1);check(collect().empty(),"static observation rejects mismatched live identity");
    resident.field(VisitFixture::entity,8,resident.id);resident.field(VisitFixture::entity,0x7e,std::uint16_t{0});check(collect().empty(),"regular nearby monster cannot qualify from a stale giant spawn flag alone");
}

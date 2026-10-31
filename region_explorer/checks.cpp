#include "explore.hpp"
#include "difficulty.hpp"
#include "nav_bridge.hpp"
#include "boss_checks.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <fstream>

int main(int argc,char** argv) {
    unsigned checks=0;auto check=[&](bool ok,const char* text){++checks;if(!ok){std::cerr<<"FAIL "<<text<<'\n';std::exit(1);}};
    bossChecks(check);
    using D=explore::Difficulty;
    check(explore::levelCurve(1)==0.0f&&explore::levelCurve(13)==.375f,"native level curve golden values match verified float calculation");
    check(explore::difficulty(8,13)==D::Lower,"level 8 is white against level 13 under native nonlinear lower threshold");
    check(explore::difficulty(9,13)==D::Similar&&explore::difficulty(13,13)==D::Similar,"level 9 and equal-level areas are cyan against level 13");
    check(explore::difficulty(19,13)==D::Similar&&explore::difficulty(20,13)==D::Higher,"native upper threshold changes from cyan at 19 to red at 20 for level 13");
    check(explore::difficulty(1,100)==D::Lower&&explore::difficulty(100,1)==D::Higher,"large relative level differences retain white/red semantics");
    check(explore::difficulty(100000,-1,true)==D::City,"city names stay neutral rather than becoming combat recommendations");
    check(explore::difficulty(20,-1)==D::Unknown&&explore::difficulty(20,100001)==D::Unknown,"missing or invalid player level never becomes a guessed recommendation");
    check(explore::difficulty(100001,13)==D::Unknown,"out-of-range area level remains unknown");
    check(explore::difficulty(0,0)==D::Similar,"valid zero native levels are not mistaken for failed reads");
    check(explore::difficultyColor(D::Lower)==0xffffffffu&&explore::difficultyColor(D::Similar)==0xffffff00u&&explore::difficultyColor(D::Higher)==0xff3333ffu,"difficulty RGBA matches the original white/cyan/red landmark colors");
    check(std::string(explore::difficultyLabel(D::Unknown))=="难度未知","unknown difficulty has an explicit player-facing label");
    using Bytes=std::array<unsigned char,explore::recordSize>;
    auto put=[](auto& bytes,unsigned offset,auto value){std::memcpy(bytes.data()+offset,&value,sizeof(value));};
    const double start=8388608;const nav::Point player{start+2560,start+2560,120};
    Bytes bytes{};put(bytes,0,std::int64_t(player.x*65536));put(bytes,8,std::int64_t(player.y*65536));
    put(bytes,0x18,1u);put(bytes,0x20,12345u);put(bytes,0x24,12u);
    explore::Place city;
    check(explore::parse(bytes.data(),4097,4097,player.z,city)&&city.city()&&!city.challenge(),"native city record is parsed with full fixed-point precision");
    check(city.position.x==player.x&&city.position.y==player.y&&city.id==((4097u<<16)|4097),"place identity and coordinates are stable");
    auto altered=bytes;put(altered,0x18,0u);explore::Place rejected=city;
    check(!explore::parse(altered.data(),4097,4097,120,rejected),"empty native landmark excluded");
    put(altered,0x18,10u);check(!explore::parse(altered.data(),4097,4097,120,rejected),"native hidden landmark category excluded");
    put(altered,0x18,255u);check(!explore::parse(altered.data(),4097,4097,120,rejected),"implausible record category refused");
    altered=bytes;put(altered,0,std::int64_t(-1));check(!explore::parse(altered.data(),4097,4097,120,rejected),"negative position refused");
    altered=bytes;put(altered,0,std::int64_t(100));check(!explore::parse(altered.data(),4097,4097,120,rejected),"record outside its coarse map block refused");
    altered=bytes;put(altered,0x24,0xffffffffu);check(!explore::parse(altered.data(),4097,4097,120,rejected),"invalid area level refused");
    auto mission=bytes;put(mission,0x18,3u);put(mission,0x34,52u);put(mission,0x38,17u);
    explore::Place challenge;
    check(explore::parse(mission.data(),4097,4097,120,challenge)&&challenge.challenge()&&!challenge.done()&&std::string(explore::kind(challenge))=="未挑战","only a native mission becomes an active challenge");
    mission[0x41]=1;check(explore::parse(mission.data(),4097,4097,120,challenge)&&std::string(explore::kind(challenge))=="挑战中","native in-progress state is retained");
    mission[0x41]=2;check(explore::parse(mission.data(),4097,4097,120,challenge)&&challenge.done(),"completed boss mission is identified");
    mission[0x41]=3;check(!explore::parse(mission.data(),4097,4097,120,rejected),"unrecognized mission state is not guessed");
    put(mission,0x34,0u);
    check(!explore::parse(mission.data(),4097,4097,120,rejected),"invalid retained state is refused even with no current mission ID");
    mission[0x41]=2;put(mission,0x18,14u);put(mission,0x1c,5u);explore::Place retained;
    check(explore::parse(mission.data(),4097,4097,120,retained)&&retained.subtype==5&&retained.challenge()&&retained.done(),"completed state without current task is retained with its native subtype");
    check(std::string(explore::kind(retained))=="留存完成"&&std::string(explore::placeType(retained))=="金字塔","retained completion and pyramid subtype have explicit separate labels");
    auto progress=retained;progress.state=1;
    check(progress.challenge()&&!progress.done()&&std::string(explore::kind(progress))=="留存进度","retained progress does not become a completed task");
    auto ordinary=retained;ordinary.state=0;
    check(!ordinary.challenge()&&!ordinary.done(),"ordinary pyramids are not invented as active or completed challenges");
    const auto retainedRows=explore::rows({retained,progress,ordinary},player,1);
    const auto retainedCounts=explore::counts(retainedRows);
    check(retainedRows.size()==2&&retainedRows.back().done()&&retainedCounts.total==2&&retainedCounts.done==1&&retainedCounts.retained==2,"challenge filtering and counts include retained states exactly once, after unfinished tasks");
    check(explore::rows({retained,progress,ordinary},player,2).size()==1,"retained challenge records do not duplicate in ordinary landmark tab");
    const char* dungeonTypes[]={"城堡","废墟","地下墓穴","宫殿","神庙","金字塔"};
    for(unsigned i=0;i<6;++i){auto p=retained;p.subtype=i;check(std::string(explore::placeType(p))==dungeonTypes[i],"same native category keeps all six verified dungeon subtypes distinct");}
    auto unknownType=retained;unknownType.subtype=6;
    check(std::string(explore::placeType(unknownType))=="地标","unverified subtype stays generic rather than guessing a pyramid");
    unknownType.category=5;unknownType.subtype=4;
    check(std::string(explore::placeType(unknownType))=="墓地","native ruins category's gravesite subtype stays distinct");
    unknownType.subtype=0;
    check(std::string(explore::placeType(unknownType))=="废墟","ordinary ruins subtype has verified ruins label");
    retained.name="泰勒莫";
    check(explore::title(retained)=="金字塔 · 泰勒莫","type is combined with existing native name without replacing it");
    auto special=retained;special.category=11;special.subtype=0;
    check(std::string(explore::placeType(special))=="巨岩","Alpha's named Rock landmark uses giant-rock translation");
    special.category=12;
    check(std::string(explore::placeType(special))=="巨树","Alpha's named Tree landmark uses giant-tree translation");
    special.category=8;
    check(std::string(explore::placeType(special))=="环形坑","Crater translation does not invent a meteor impact cause");
    check(explore::reviewedTargetLabel("树木 · 泰勒荣")=="巨树 · 泰勒荣","restored target display updates old tree type without replacing the proper name");
    check(explore::reviewedTargetLabel("岩石 · 露芶龙")=="巨岩 · 露芶龙","restored rock display retains the original uncommon Chinese character");
    check(explore::reviewedTargetLabel("陨石坑 · 泰勒莫")=="环形坑 · 泰勒莫","restored crater display updates only the old type prefix");
    check(explore::reviewedTargetLabel("地标 树木镇")=="地标 树木镇","type review does not rewrite words inside arbitrary proper names");
    mission[0x41]=0;put(mission,0x34,0xffffffffu);check(explore::parse(mission.data(),4097,4097,120,rejected)&&rejected.challenge(),"native nonzero mission identifiers are not restricted to an invented range");
    auto landmark=city;landmark.category=7;landmark.mission=0;
    check(std::string(explore::kind(landmark))=="地标","unknown landmark category is not invented as a castle or boss");
    std::vector<explore::Place> all={landmark,city,challenge};
    check(explore::rows(all,player,0).size()==1&&explore::rows(all,player,0)[0].city(),"town tab contains only confirmed towns");
    check(explore::rows(all,player,1).size()==1&&explore::rows(all,player,1)[0].challenge(),"challenge tab requires a native mission");
    check(explore::rows(all,player,2).size()==1&&!explore::rows(all,player,2)[0].challenge(),"ordinary landmarks stay outside challenge tab");
    auto near=challenge,far=challenge,done=challenge;near.state=far.state=0;near.position.x+=20;far.position.x+=500;done.position.x+=1;
    all={far,done,near};const auto sorted=explore::rows(all,player,1);
    check(sorted.size()==3&&nav::distance(player,sorted[0].position)==20&&sorted[2].done(),"unfinished challenges sort by distance, completed ones come last");
    explore::Names names;for(size_t i=0;i<40;++i)names[i]=std::to_string(i)+":";
    check(explore::name(0,names)=="0:20:"&&explore::name(1,names)=="13:27:","place-name indices follow native default naming algorithm");
    const unsigned seed=0xffffffffu;
    check(explore::name(seed,names)==names[(seed/7+seed*13)%20]+names[20+(seed*7+seed/10)%20],"name indices retain native uint32 overflow");
    names[0].clear();check(explore::name(0,names).empty(),"unreadable native syllables do not create a guessed name");
    nav::Point restored{1,2,3};std::string label="previous";
    const auto encoded=explore::encodeTarget(player,"城镇 Krolon");
    check(explore::decodeTarget(encoded,restored,label)&&restored.x==player.x&&label=="城镇 Krolon","independent target round trip preserves precision and UTF-8 label");
    const auto previous=restored;
    check(!explore::decodeTarget("CW_EXPLORE_0\ninvalid",restored,label)&&restored.x==previous.x,"old or corrupt target data preserves current output");
    check(!explore::safeLabel("bad\nlabel")&&!explore::safeLabel(std::string(96,'x'))&&!explore::safeLabel(""),"control characters and oversized labels refused");
    check(!explore::decodeTarget(encoded+'\n',restored,label),"target with trailing control data is rejected");

    std::map<unsigned,std::vector<unsigned char>> memory;const unsigned gc=0x10000,chunk=0x1800000;
    const unsigned slot=gc+0x800d44+0xb0+(512*1024+512)*4;
    memory[slot].resize(4);std::memcpy(memory[slot].data(),&chunk,4);
    memory[chunk+explore::chunkPlaces].resize(64*explore::recordSize);
    std::memcpy(memory[chunk+explore::chunkPlaces].data()+9*explore::recordSize,bytes.data(),bytes.size());
    unsigned reads=0;const explore::Reader reader=[&](unsigned address,void* output,unsigned size) {
        ++reads;const auto found=memory.find(address);if(found==memory.end()||size>found->second.size())return false;
        std::memcpy(output,found->second.data(),size);return true;
    };
    const auto before=memory;const auto snapshot=explore::scan(reader,gc,player);
    check(snapshot.chunks==1&&snapshot.places.size()==1&&snapshot.places[0].city(),"resident coarse map is read without requiring NPC entities or exploration flags");
    check(reads<=50&&memory==before,"scan is bounded and changes no game or discovery bytes");
    memory[chunk+explore::chunkPlaces].resize(16);
    check(explore::scan(reader,gc,player).places.empty(),"unloading or truncated chunk is safely skipped");
    check(explore::scan(reader,gc,{-1,1,1}).places.empty(),"out-of-world player position does not scan arbitrary memory");
    navbridge::Request request;request.point=player;std::snprintf(request.scope,sizeof(request.scope),"%s","world_10_ab");std::snprintf(request.label,sizeof(request.label),"%s","城镇 Krolon");
    check(navbridge::valid(request,"world_10_ab"),"navigation handoff accepts matching world and finite target");
    check(!navbridge::valid(request,"world_20_cd"),"navigation handoff rejects a previous world's target");
    request.version=2;check(!navbridge::valid(request,"world_10_ab"),"unknown navigation protocol version refused");request.version=1;
    request.point.x=std::numeric_limits<double>::quiet_NaN();check(!navbridge::valid(request,"world_10_ab"),"invalid navigation coordinate refused");request.point=player;
    request.label[95]='x';check(!navbridge::valid(request,"world_10_ab"),"unterminated navigation payload refused");
    if(argc>1) {
        std::ifstream input(argv[1]);std::string magic,hex;size_t count=0;input>>magic>>count;
        check(magic=="CW_EXPLORE_RECORDS_1"&&count>0&&count<100000,"read-only saved-map replay fixture has verified header");
        size_t towns=0;std::vector<explore::Place> replayed;
        for(size_t i=0;i<count;++i) {
            unsigned x=0,y=0;input>>x>>y>>hex;
            check(!input.fail()&&hex.size()==explore::recordSize*2,"saved record has complete native replay layout");
            Bytes record{};
            for(size_t j=0;j<record.size();++j)record[j]=static_cast<unsigned char>(std::stoul(hex.substr(j*2,2),nullptr,16));
            explore::Place p;
            check(explore::parse(record.data(),x,y,100,p),"actual saved landmark passes native position and category validation");
            if(p.city())++towns;
            replayed.push_back(p);
            if(argc<3)check(!p.challenge(),"no live boss state is invented from saved map metadata");
        }
        check(towns>0,"saved replay contains confirmed city records");
        if(argc>=3)check(std::string(argv[2])=="live","explicit live replay mode only");
        const auto tasks=explore::counts(explore::rows(replayed,player,1));
        std::cout<<"Replayed "<<count<<" places including "<<towns<<" towns; challenge records="<<tasks.total<<" done="<<tasks.done<<" retained="<<tasks.retained<<"\n";
    }
    std::cout<<"PASS "<<checks<<" region exploration checks\n";
}

// Compile the production translation unit unchanged against an isolated SDK.
// No game DLL is loaded and no real player, storage or input is modified.
#include "fast_travel.cpp"
#include "visit_checks.hpp"
#include <map>
#include <cstdio>
#include <cstdlib>
#include <fstream>
namespace fixture {
unsigned checks=0,teleports=0,stats=0,menus=0;bool supportedBuild=true,failSave=false,failTeleport=false,failEvents=false,failOverlay=false;
bool failStationsOnce=false;
bool visible=false;CubePlayer player{};CubeSession session{};CubeUi ui{};
std::string storageScope;std::map<std::string,std::string> files;
std::map<unsigned,std::vector<unsigned char>> memory;
travel::Fixed rawPosition{};unsigned seed=123;
template<class T>void memoryPut(unsigned address,const T& value){auto& bytes=memory[address];bytes.resize(sizeof(value));std::memcpy(bytes.data(),&value,sizeof(value));}
travel::Fixed positionWords(){const auto units=travel::units(rawPosition);if(float(units.x)!=player.x||float(units.y)!=player.y||float(units.z)!=player.z)rawPosition=travel::fixed(travel::position(player));return rawPosition;}
void check(bool value,const char* name){++checks;if(!value){std::printf("FAIL: %s\n",name);std::exit(1);}}
template<class T>int copy(void* out,unsigned bytes,const T& value){if(bytes!=sizeof(value))return 0;std::memcpy(out,&value,bytes);return 1;}
int32_t CUBE_CALL read(const CubeApi*,uint32_t a,void* out,uint32_t bytes){
    for(const auto& v:memory)if(a>=v.first&&std::uint64_t(a)-v.first+bytes<=v.second.size()){std::memcpy(out,v.second.data()+(a-v.first),bytes);return 1;}
    if(player.address&&a==player.address+0x10)return copy(out,bytes,positionWords());
    const auto base=reinterpret_cast<unsigned>(GetModuleHandleW(nullptr));
    if(a==base){IMAGE_DOS_HEADER d{};d.e_magic=supportedBuild?IMAGE_DOS_SIGNATURE:0;d.e_lfanew=0x80;return copy(out,bytes,d);}
    if(a==base+0x80){IMAGE_NT_HEADERS32 n{};n.Signature=IMAGE_NT_SIGNATURE;n.FileHeader.Machine=IMAGE_FILE_MACHINE_I386;n.FileHeader.TimeDateStamp=0x51ea955e;n.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC;n.OptionalHeader.SizeOfImage=0x3bc000;n.OptionalHeader.AddressOfEntryPoint=0x28e1e0;return copy(out,bytes,n);}
    const unsigned str=session.address+0x2e4+0x94;
    if(a==str+0x10){unsigned n=3;return copy(out,bytes,n);}if(a==str+0x14){unsigned cap=15;return copy(out,bytes,cap);}
    if(a==str&&bytes==3){std::memcpy(out,"TST",3);return 1;}if(a==str+3&&bytes==1){*static_cast<char*>(out)=0;return 1;}return 0;
}
int32_t CUBE_CALL getPlayer(const CubeApi*,CubePlayer* p){*p=player;return 1;}
int32_t CUBE_CALL getSession(const CubeApi*,CubeSession* s){*s=session;return 1;}
int32_t CUBE_CALL getWorld(const CubeApi*,CubeWorld* w){*w={};w->hasSeed=1;w->seed=seed;return 1;}
int32_t CUBE_CALL getUi(const CubeApi*,CubeUi* u){*u=ui;return 1;}
int32_t CUBE_CALL teleport(const CubeApi*,float,float,float){check(false,"float teleport API must not lose coordinate precision");return 0;}
int32_t CUBE_CALL write(const CubeApi*,uint32_t address,const void* data,uint32_t bytes){
    check(address==player.address+0x10&&bytes==sizeof(travel::Fixed),"guarded write owns only full player position triplet");++teleports;
    if(failTeleport){failTeleport=false;return 0;}std::memcpy(&rawPosition,data,bytes);const auto point=travel::units(rawPosition);player.x=float(point.x);player.y=float(point.y);player.z=float(point.z);return 1;
}
int32_t CUBE_CALL readable(const CubeApi* a,uint32_t address,uint32_t bytes){std::vector<unsigned char> buffer(bytes);return read(a,address,buffer.data(),bytes);}
uint32_t CUBE_CALL rebase(const CubeApi*,uint32_t address){return address;}
int32_t CUBE_CALL setStat(const CubeApi*,int32_t field,double value){++stats;check(value==0&&(field==CUBE_STAT_VEL_X||field==CUBE_STAT_VEL_Y||field==CUBE_STAT_VEL_Z),"only velocity setters used");return 1;}
void CUBE_CALL log(const CubeApi*,CubeLogLevel,const char*){}
uint32_t CUBE_CALL subscribe(const CubeApi*,CubeEvent,CubeEventFn,void*){return failEvents?0:1;}
int32_t CUBE_CALL unsubscribe(const CubeApi*,uint32_t){return 1;}
uint32_t CUBE_CALL menu(const CubeApi*,CubeOverlayDrawFn,void*,uint32_t key,int32_t open){++menus;check(key==VK_F8&&!open,"F8 menu starts closed");return failOverlay?0:1;}
int32_t CUBE_CALL setVisible(const CubeApi*,uint32_t,int32_t open){visible=open!=0;return 1;}
int32_t CUBE_CALL setScope(const CubeApi*,const char* s){storageScope=s;return 1;}
int32_t CUBE_CALL has(const CubeApi*,const char* k){return files.count(storageScope+'/'+k)?1:0;}
int32_t CUBE_CALL get(const CubeApi*,const char* k,void* out,int32_t bytes){const auto found=files.find(storageScope+'/'+k);if(found==files.end())return 0;if(!out)return int(found->second.size());const auto n=std::min(bytes,int(found->second.size()));std::memcpy(out,found->second.data(),n);return n;}
int32_t CUBE_CALL put(const CubeApi*,const char* k,const void* data,int32_t bytes){if(failSave)return 0;if(failStationsOnce&&std::strcmp(k,"stations")==0){failStationsOnce=false;files[storageScope+'/'+k]=std::string(static_cast<const char*>(data),std::min(5,bytes));return 0;}files[storageScope+'/'+k]=std::string(static_cast<const char*>(data),bytes);return 1;}
int32_t CUBE_CALL remove(const CubeApi*,const char* k){return files.erase(storageScope+'/'+k)?1:0;}
}
int main(int argc,char** argv){
    using fixture::check;
    if(argc==3&&std::string(argv[1])=="records"){
        std::ifstream file(argv[2],std::ios::binary);check(file.is_open(),"saved station file opens read-only");const std::string bytes{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
        std::vector<travel::Station> old,next;check(travel::decode(bytes,old),"existing saved format decodes");check(travel::decode(travel::encode(old),next)&&old.size()==next.size(),"saved records upgrade and reload in memory");
        for(size_t i=0;i<old.size();++i){const auto a=travel::fixed(old[i].point),b=travel::fixed(next[i].point);check(old[i].name==next[i].name&&old[i].note==next[i].note&&old[i].cityId==next[i].cityId&&visits::same(old[i].visit,next[i].visit)&&std::memcmp(&a,&b,sizeof(a))==0,"saved name, note, automatic info, town identity and exact coordinates preserved");}
        std::printf("PASS %u saved stations migrated in memory; source file never written\n",unsigned(old.size()));return 0;
    }
    visitChecks(check);
    std::vector<travel::Station> decoded{{"preserve",{1,2,3}}};
    check(!travel::decode("garbage",decoded)&&decoded[0].name=="preserve","corrupt data preserves caller state");
    const std::vector<travel::Station> sample{{"城镇 A",{8391168,8391168,80.25f}},{"野外驿站",{-12,256,32}}};
    check(travel::decode(travel::encode(sample),decoded)&&decoded.size()==2&&decoded[0].point.z==80.25f,"UTF8 names and real coordinate magnitude roundtrip");
    check(!travel::decode(travel::encode(sample)+"bad",decoded),"trailing bytes rejected");
    check(!travel::decode("CWTRAVEL1\n33\n",decoded),"unbounded station count rejected");
    check(!travel::decode("CWTRAVEL1\n1\n\"bad\" nan 1 2",decoded),"NaN coordinate rejected");
    check(!travel::decode("CWTRAVEL1\n2\n\"x\" 1 2 3\n\"x\" 4 5 6\n",decoded),"duplicate names rejected");
    check(!travel::label("\n")&&!travel::label("  ")&&!travel::label(std::string(65,'x')),"invalid names rejected");
    check(!travel::finite({1e20f,0,0}),"coordinates beyond useful float precision rejected");
    check(travel::decode("CWTRAVEL2\n1\n\"旧城镇\" 8391168.375 8391168.25 80.125 42\n",decoded)&&decoded[0].note.empty()&&decoded[0].cityId==42&&decoded[0].point.x==8391168.375,"v2 records retain exact coordinates and town id with empty note");
    check(travel::decode("CWTRAVEL1\n1\n\"旧野外\" 1 2 3\n",decoded)&&decoded[0].note.empty()&&decoded[0].cityId==0,"v1 records remain readable");
    auto annotated=sample;annotated[0].note="Boss：法师\n先清小怪，带药。\n含 \"引号\" 与 \\ 路径";
    check(travel::decode(travel::encode(annotated),decoded)&&decoded[0].note==annotated[0].note,"multiline Chinese notes, quotes and backslashes roundtrip");
    std::vector<travel::Station> full;for(unsigned i=0;i<32;++i)full.push_back({"地点"+std::to_string(i),{double(i),2,3},0,std::string(travel::maxNoteBytes,'x')});
    const auto fullText=travel::encode(full);check(fullText.size()>8192&&fullText.size()<travel::maxDataBytes&&travel::decode(fullText,decoded)&&decoded.size()==32,"all 32 maximum-size notes load within bounded record file");
    check(!travel::note(std::string(1025,'x'))&&!travel::note(std::string(1,'\0'))&&!travel::note(std::string(1,'\1'))&&travel::note("\n\t"),"note limits reject controls and permit line breaks");
    check(!travel::decode("CWTRAVEL3\n1\n\"x\" 1 2 3 0\n",decoded),"truncated annotation field rejected");
    check(!travel::decode(std::string(travel::maxDataBytes+1,'x'),decoded),"oversized record file rejected");
    static CubeApi api{};api.abiVersion=CUBE_ABI_VERSION;api.structSize=sizeof(api);api.environment=CUBE_ENV_CLIENT;
    api.log.write=&fixture::log;api.mem.read=&fixture::read;
    api.mem.write=&fixture::write;api.mem.readable=&fixture::readable;api.mem.rebase=&fixture::rebase;
    api.events.subscribe=&fixture::subscribe;
    api.events.unsubscribe=&fixture::unsubscribe;
    api.player.get=&fixture::getPlayer;api.player.teleport=&fixture::teleport;api.player.setStat=&fixture::setStat;
    api.session.get=&fixture::getSession;api.world.get=&fixture::getWorld;api.ui.get=&fixture::getUi;
    api.overlay.registerMenu=&fixture::menu;api.overlay.setVisible=&fixture::setVisible;
    api.storage.setScope=&fixture::setScope;api.storage.has=&fixture::has;api.storage.get=&fixture::get;api.storage.put=&fixture::put;
    api.storage.remove=&fixture::remove;
    const std::string mode=argc>1?argv[1]:"";
    fixture::supportedBuild=mode!="unsupported";fixture::failEvents=mode=="events";fixture::failOverlay=mode=="overlay";
    const auto info=CubeMod_Init(&api);
    check(info&&std::string(info->id)=="local.cubeworld.fast_travel"&&info->deps==nullptr,"independent manifest has no other Mod dependencies");
    check(fixture::teleports==0&&fixture::stats==0&&fixture::files.empty(),"initialization never moves player or writes storage");
    if(argc>1){check(!readyToTravel,"failed build, event or overlay checks keep teleport disabled");check(fixture::menus==(mode=="overlay"?1u:0u),"failed build or events register no controls");std::printf("%u %s checks passed\n",fixture::checks,mode.c_str());return 0;}
    check(fixture::menus==1,"supported build registers a single menu");
    fixture::session.gameState=CUBE_STATE_IN_WORLD;fixture::session.address=0x1000000;fixture::session.hasNetwork=1;fixture::session.networkMode=CUBE_NET_SINGLEPLAYER;fixture::session.playerCount=1;
    auto& p=fixture::player;p.address=0x3000000;p.alive=1;p.health=100;p.hasPosition=p.hasState=p.onGround=p.hasName=1;p.action=CUBE_ACTION_IDLE;p.x=8391168;p.y=8391168;p.z=80;strcpy_s(p.name,"Tester");fixture::ui.valid=1;
    check(travel::single(fixture::session)&&travel::standing(p),"single player grounded idle is eligible");
    auto altered=p;altered.onGround=0;check(!travel::standing(altered),"airborne registration rejected");altered=p;altered.velX=1;check(!travel::standing(altered),"moving registration rejected");altered=p;altered.action=CUBE_ACTION_CASTING;check(!travel::standing(altered),"casting rejected");
    fixture::session.hasNetwork=0;check(!travel::single(fixture::session),"unknown network state rejected");fixture::session.hasNetwork=1;
    CubePlayer current{};CubeSession session{};check(context(current,session),"production world scope resolves from existing game fields");
    const auto originalScope=scope;strcpy_s(stationName,"城镇驿站");registerStation();check(stations.size()==1&&fixture::files.size()==1,"production registration persists actual standing location");
    const auto saved=fixture::files;registerStation();check(stations.size()==1&&fixture::files==saved,"near duplicate does not overwrite file");
    p.x+=256;fixture::failSave=true;registerStation();check(stations.size()==1&&fixture::files==saved,"failed save retains original stations");fixture::failSave=false;
    strcpy_s(stationName,"第二城镇");registerStation();check(stations.size()==2,"second visited station can be registered");
    strcpy_s(p.name,"Other");check(context(current,session)&&stations.empty()&&scope!=originalScope,"character switch isolates stations");
    strcpy_s(p.name,"Tester");check(context(current,session)&&stations.size()==2,"returning character restores stations");
    fixture::session.networkMode=CUBE_NET_MULTIPLAYER;beginTravel(0,GetTickCount64());check(fixture::teleports==0&&!journey.active,"multiplayer cannot teleport");fixture::session.networkMode=CUBE_NET_SINGLEPLAYER;
    fixture::ui.anyOpen=1;beginTravel(0,GetTickCount64());check(fixture::teleports==0,"native open menu blocks teleport");fixture::ui.anyOpen=0;
    const auto origin=travel::position(p);beginTravel(0,GetTickCount64());check(journey.active&&fixture::teleports==1&&!fixture::visible,"explicit confirmation writes guarded full coordinates and closes panel");
    tick();check(journey.active&&fixture::teleports==2,"landing holds at recorded position while terrain loads");
    journey.started=GetTickCount64()-2000;tick();check(journey.active,"arrival requires two ground samples");tick();check(!journey.active,"grounded arrival completes without additional move");
    p.x=float(origin.x);p.y=float(origin.y);p.z=float(origin.z);beginTravel(0,GetTickCount64());journey.started=GetTickCount64()-2000;p.z=float(journey.destination.z-3);tick();check(!journey.active&&p.x==origin.x&&p.z==origin.z&&fixture::visible,"falling landing returns to original safe point");
    beginTravel(0,GetTickCount64());journey.started=GetTickCount64()-9000;tick();check(!journey.active&&p.x==origin.x,"landing timeout returns to origin");
    beginTravel(0,GetTickCount64());const auto beforeSwitch=fixture::teleports;p.address++;tick();check(!journey.active&&fixture::teleports==beforeSwitch,"character address change cancels without writing new entity");p.address--;
    p.x=float(origin.x);p.z=float(origin.z);fixture::failTeleport=true;beginTravel(0,GetTickCount64());check(!journey.active&&p.x==origin.x,"guarded position failure rolls back immediately");
    // Reuse the upstream auto-discovery flow with the project's native city and
    // shop readers. A distant map label alone must never unlock a destination.
    const unsigned chunk=0x5000000,head=0x6000000,node=0x6100000,entity=0x6200000;
    const int bx=int(p.x/explore::placeBlocks),by=int(p.y/explore::placeBlocks);const unsigned cityId=(unsigned(bx)<<16)|unsigned(by);
    fixture::memoryPut(fixture::session.address+0x800d44+0xb0+unsigned((bx/8)*1024+by/8)*4,chunk);
    auto& records=fixture::memory[chunk+explore::chunkPlaces];records.resize(explore::placesPerChunk*explore::recordSize);
    auto* record=records.data()+((bx%8)*8+by%8)*explore::recordSize;
    const auto coarse=travel::fixed(travel::position(p));std::memcpy(record,&coarse.x,8);std::memcpy(record+8,&coarse.y,8);unsigned category=1;std::memcpy(record+0x18,&category,4);
    const auto stationCount=stations.size();discoverTown(p,fixture::session);check(stations.size()==stationCount,"resident city without nearby real shop is not auto-unlocked");
    fixture::memoryPut(fixture::session.address+0x2e8,head);fixture::memoryPut(head+4,node);
    auto& links=fixture::memory[node];links.resize(0x1c);std::memcpy(links.data(),&head,4);std::memcpy(links.data()+8,&head,4);std::memcpy(links.data()+0x18,&entity,4);
    fixture::rawPosition=travel::fixed({double(p.x)+.375,double(p.y)+.25,80.125});p.z=80.125f;
    auto& shop=fixture::memory[entity];shop.resize(0x1d8);shop[0x60]=3;shop[0x140]=128;std::memcpy(shop.data()+0x10,&fixture::rawPosition,24);std::memcpy(shop.data()+0x1c0,&fixture::rawPosition,24);
    for(auto address:explore::nameRvas){const wchar_t name[2]={L'镇',0};auto& bytes=fixture::memory[0x400000+address];bytes.resize(sizeof(name));std::memcpy(bytes.data(),name,sizeof(name));}
    namesLoaded=false;discoverTown(p,fixture::session);
    check(stations.size()==stationCount+1&&stations.back().cityId==cityId+1&&lastTown==cityId+1,"grounded real shop visit auto-registers city and last-town shortcut");
    const auto exact=stations.back().point;check(exact.x==double(p.x)+.375&&exact.y==double(p.y)+.25&&exact.z==80.125,"upstream fixed-point precision is preserved at world-scale coordinates");
    discoverTown(p,fixture::session);check(stations.size()==stationCount+1,"revisiting city does not duplicate it");
    p.x+=80;fixture::rawPosition=travel::fixed(travel::position(p));std::memcpy(shop.data()+0x10,&fixture::rawPosition,24);std::memcpy(shop.data()+0x1c0,&fixture::rawPosition,24);lastTown=0;
    discoverTown(p,fixture::session);check(lastTown==cityId+1&&stations.size()==stationCount+1&&stations.back().point.x==exact.x,"another shop in a known city refreshes last town and preserves original anchor");
    const auto last=lastTown;reset();check(context(current,session)&&lastTown==last&&stations.back().cityId==last,"last-town reference restores with station data");
    fixture::seed=456;check(context(current,session)&&stations.empty()&&lastTown==0,"another world seed has isolated destinations");fixture::seed=123;check(context(current,session)&&lastTown==last,"returning world restores last town");
    beginTravel(int(stations.size())-1,GetTickCount64());check(journey.active&&travel::units(fixture::rawPosition).x==exact.x,"travel preserves recorded sub-block X precision");journey.active=false;
    travel::Point restored{};check(exactPosition(p,restored)&&restored.x==exact.x,"position readback verifies full native coordinates");
    const auto beforeEdits=fixture::teleports,beforeEditStats=fixture::stats;const auto untouched=stations[0];
    startEdit(0);check(editing==0&&std::string(editName)==untouched.name,"editing loads selected record into a draft");
    strcpy_s(editName,"北方森林 · 法师Boss");strcpy_s(editNote,"Boss：法师\n红名，先清小怪。\n下次带药；已打过一次。");const std::string expectedNote=editNote;saveEdit();
    const auto beforePoint=travel::fixed(untouched.point),afterPoint=travel::fixed(stations[0].point);
    check(editing==-1&&stations[0].name=="北方森林 · 法师Boss"&&stations[0].note==expectedNote,"production edit saves name and multiline note");
    check(std::memcmp(&beforePoint,&afterPoint,sizeof(beforePoint))==0&&stations[0].cityId==untouched.cityId,"editing leaves exact destination and town id unchanged");
    check(fixture::teleports==beforeEdits&&fixture::stats==beforeEditStats,"metadata edits never move player or alter game stats");
    reset();check(context(current,session)&&stations[0].note==expectedNote&&stations[0].name=="北方森林 · 法师Boss","edited fields persist after a fresh world load");
    const auto beforeCancel=fixture::files;startEdit(0);strcpy_s(editName,"不保存的草稿");strcpy_s(editNote,"草稿");cancelEdit();check(fixture::files==beforeCancel&&stations[0].note==expectedNote,"cancel does not write draft changes");
    startEdit(0);std::snprintf(editName,sizeof(editName),"%s",stations[1].name.c_str());saveEdit();check(editing==0&&fixture::files==beforeCancel,"duplicate rename is rejected with draft retained");
    editName[0]=0;saveEdit();check(editing==0&&fixture::files==beforeCancel,"blank rename preserves existing record");
    strcpy_s(editName,"保存失败测试");fixture::failSave=true;saveEdit();fixture::failSave=false;check(editing==0&&fixture::files==beforeCancel,"backup failure prevents edit from touching saved records");
    const auto stationKey=fixture::storageScope+"/stations",beforePartial=fixture::files[stationKey];fixture::failStationsOnce=true;saveEdit();
    check(editing==0&&dataOk&&fixture::files[stationKey]==beforePartial&&stations[0].note==expectedNote,"partial station write restores previous bytes and retains edit draft");
    check(fixture::files[fixture::storageScope+"/stations_backup"]==beforePartial,"verified previous record copy is retained");cancelEdit();
    const auto beforeWorldSwitch=fixture::files;startEdit(0);strcpy_s(editName,"不能写入另一个世界");fixture::seed=789;saveEdit();check(editing==-1&&fixture::files==beforeWorldSwitch,"world switch cancels edit without writing either world's data");
    fixture::seed=123;check(context(current,session)&&stations[0].note==expectedNote,"original annotated world still restores after cancelled cross-world edit");
    startEdit(int(stations.size())-1);strcpy_s(editName,"城镇 · 我的补给点");strcpy_s(editNote,"卖装备，补药。");saveEdit();discoverTown(p,fixture::session);
    check(stations.back().cityId==last&&stations.back().name=="城镇 · 我的补给点"&&stations.back().note=="卖装备，补药。","auto-discovered town identity and edited metadata survive revisit");
    VisitFixture live;fixture::memory=live.memory;
    p.x=float(live.point.x);p.y=float(live.point.y);p.z=float(live.point.z);fixture::rawPosition=travel::fixed(live.point);
    auto withManual=stations;withManual.push_back({"我登记的Boss点",live.point,0,"自己写的打法"});check(persist(std::move(withManual)),"existing manual boss destination is prepared through production storage");
    const auto own=stations.size()-1,beforeAutoCount=stations.size();const auto beforeAutoFiles=fixture::files;const auto beforeAutoMoves=fixture::teleports,beforeAutoStats=fixture::stats;
    discoverPlace(p,fixture::session,1000);check(fixture::files==beforeAutoFiles&&!stations[own].visit.placeId,"a single arrival sample does not auto-register a place");
    p.x+=400;discoverPlace(p,fixture::session,4000);discoverPlace(p,fixture::session,7000);check(fixture::files==beforeAutoFiles,"distant resident map record without a nearby matched boss is not treated as a visit");
    p.x=float(live.point.x);fixture::rawPosition=travel::fixed(live.point);p.onGround=0;discoverPlace(p,fixture::session,7000);check(fixture::files==beforeAutoFiles,"airborne arrival never supplies a teleport anchor");p.onGround=1;
    discoverPlace(p,fixture::session,10000);discoverPlace(p,fixture::session,13000);
    check(stations.size()==beforeAutoCount&&stations[own].name=="我登记的Boss点"&&stations[own].note=="自己写的打法"&&stations[own].point.x==live.point.x,"automatic discovery enriches nearby manual record without replacing personal text or exact anchor");
    check(stations[own].visit.placeId==live.place.id+1&&stations[own].visit.bossName=="古龙"&&stations[own].visit.bossClass==3,"production auto-registration stores the linked boss name and actual class");
    const auto afterVisit=fixture::files;discoverPlace(p,fixture::session,16000);check(stations.size()==beforeAutoCount&&fixture::files==afterVisit,"unchanged revisit does not duplicate record or rewrite files");
    startEdit(int(own));strcpy_s(editNote,"我正在写的新打法");
    unsigned newSeed=live.place.missionSeed+1;std::memcpy(fixture::memory[live.coarseRecord()].data()+0x30,&newSeed,4);std::memcpy(fixture::memory[live.liveRecord()].data()+0x30,&newSeed,4);fixture::memory[VisitFixture::entity][0x140]=4;
    discoverPlace(p,fixture::session,19000);check(stations[own].visit.bossClass==4&&stations[own].note=="自己写的打法"&&std::string(editNote)=="我正在写的新打法","new live boss information updates separately while a personal edit draft remains intact");
    saveEdit();check(stations[own].visit.bossClass==4&&stations[own].note=="我正在写的新打法","saving personal draft retains freshly observed boss information");
    VisitFixture nextPlace({live.point.x+4096,live.point.y+4096,live.point.z});fixture::memory=nextPlace.memory;const travel::Point bossAnchor{nextPlace.point.x+400,nextPlace.point.y,nextPlace.point.z};const auto bossPosition=travel::fixed(bossAnchor);std::memcpy(fixture::memory[VisitFixture::entity].data()+0x10,&bossPosition,24);p.x=float(bossAnchor.x);p.y=float(bossAnchor.y);p.z=float(bossAnchor.z);fixture::rawPosition=bossPosition;visitDwell={};nextDiscover=0;
    tick();check(stations.size()==beforeAutoCount,"frame-driven automatic registration waits for stable arrival");visitDwell.since=GetTickCount64()-3000;nextDiscover=0;tick();
    check(stations.size()==beforeAutoCount+1&&stations.back().visit.bossId==nextPlace.id&&stations.back().visit.bossName=="古龙"&&stations.back().point.x==bossAnchor.x,"normal frame callback registers a nearby matched boss beyond the landmark centre using actual standing anchor");
    check(fixture::teleports==beforeAutoMoves&&fixture::stats==beforeAutoStats,"background registration never moves player or writes game stats");
    const auto newVisit=stations.back().visit;reset();check(context(current,session)&&visits::same(stations.back().visit,newVisit)&&stations[own].note=="我正在写的新打法","automatic boss information and personal notes persist together after reloading");
    VisitFixture combat({live.point.x+8192,live.point.y+8192,live.point.z});combat.field(combat.coarseRecord(),0x34,0u);combat.field(combat.liveRecord(),0x34,0u);combat.field(VisitFixture::spawn,0x7a,std::uint16_t{0x200});
    fixture::seed=1001;fixture::memory=combat.memory;p.x=float(combat.point.x);p.y=float(combat.point.y);p.z=float(combat.point.z);fixture::rawPosition=travel::fixed(combat.point);check(context(current,session)&&stations.empty(),"combat-observation regression uses a separate character/world storage scope");
    check(persist({{"我已登记的地标",combat.point,0,"自己的打法"}}),"existing custom station is available before seeing a static boss");
    const auto combatMoves=fixture::teleports,combatStats=fixture::stats;p.onGround=0;p.action=CUBE_ACTION_CASTING;p.velX=2;
    observeEncounters(p,fixture::session,100000);
    check(stations[0].visit.bossName=="古龙"&&stations[0].visit.bossClass==3&&stations[0].note=="自己的打法"&&stations[0].point.x==combat.point.x,"boss seen while casting, moving and airborne updates existing F8 information without changing custom text or anchor");
    check(encounters.size()==1&&fixture::files.count(fixture::storageScope+"/encounters")&&fixture::teleports==combatMoves&&fixture::stats==combatStats,"combat observation persists independently and never moves or changes player stats");
    check(persist({}),"removing teleport anchors does not discard separate encounter history");fixture::memory.erase(VisitFixture::entity);reset();
    check(context(current,session)&&stations.empty()&&encounters.size()==1&&encounters[0].visit.bossName=="古龙","observed boss survives world reload after the actual entity is gone");
    nextDiscover=0;tick();check(stations.empty(),"moving in combat never creates a teleport landing anchor from observed coordinates");
    p.onGround=1;p.action=CUBE_ACTION_IDLE;p.velX=0;p.x+=500;fixture::rawPosition=travel::fixed(travel::position(p));
    discoverPlace(p,fixture::session,100000);discoverPlace(p,fixture::session,103000);
    check(stations.size()==1&&stations[0].visit.bossName=="古龙"&&stations[0].point.x==p.x&&stations[0].point.x!=combat.point.x,"standing later outside landmark centre attaches saved encounter to current safe anchor after boss unloads");
    fixture::memory=combat.memory;fixture::memory[VisitFixture::entity][0x140]=4;p.x=float(combat.point.x);fixture::rawPosition=travel::fixed(combat.point);const auto beforeHistoryFailure=fixture::files;fixture::failSave=true;nextEncounter=0;
    observeEncounters(p,fixture::session,110000);check(encountersDirty&&fixture::files==beforeHistoryFailure,"failed encounter backup retains original files and marks observation for retry");
    fixture::failSave=false;fixture::memory.erase(VisitFixture::entity);observeEncounters(p,fixture::session,112000);check(!encountersDirty,"encounter persistence retries after entity unload instead of losing the observation");
    const auto goodHistory=fixture::files[fixture::storageScope+"/encounters"];fixture::files[fixture::storageScope+"/encounters"]="corrupt history";reset();check(context(current,session)&&!encountersOk&&dataOk&&fixture::files[fixture::storageScope+"/encounters"]=="corrupt history","corrupt encounter history is retained without locking unrelated valid teleport records");fixture::files[fixture::storageScope+"/encounters"]=goodHistory;reset();check(context(current,session)&&encounters[0].visit.bossClass==4,"retried encounter class restores on next load");
    const auto key=fixture::storageScope+"/stations";fixture::files[key]="corrupt retained bytes";reset();check(context(current,session)&&!dataOk,"corrupt stored file locks mutation");registerStation();check(fixture::files[key]=="corrupt retained bytes","corrupt original file never overwritten");
    char dllPath[MAX_PATH]{};GetModuleFileNameA(nullptr,dllPath,MAX_PATH);auto slash=std::strrchr(dllPath,'\\');check(slash!=nullptr,"checks executable has an absolute directory");slash[1]=0;strcat_s(dllPath,"fast_travel.dll");
    const auto dll=LoadLibraryA(dllPath);check(dll!=nullptr,"actual built DLL loads in isolated SDK host");
    const auto initialize=reinterpret_cast<CubeModInitFn>(GetProcAddress(dll,"CubeMod_Init"));check(initialize!=nullptr,"actual DLL exports loader initializer");
    const auto beforeTeleports=fixture::teleports,beforeMenus=fixture::menus;const auto manifest=initialize(&api);
    check(manifest&&std::string(manifest->id)=="local.cubeworld.fast_travel"&&manifest->deps==nullptr&&manifest->capabilities==(CUBE_CAP_OVERLAY|CUBE_CAP_RAW_MEM|CUBE_CAP_WRITES),"actual DLL manifest is independent with expected permissions");
    check(fixture::teleports==beforeTeleports&&fixture::menus==beforeMenus+1,"actual DLL initializes controls without teleporting");FreeLibrary(dll);
    std::printf("%u travel production-path and DLL checks passed\n",fixture::checks);
    return 0;
}

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "cube_mod.hpp"
#include "travel.hpp"
#include "nav_math.hpp"
#include "explore_font.hpp"
#include "explore.hpp"
#include "town_scan.hpp"
#include "visit.hpp"
#include <array>

namespace {
std::vector<travel::Station> stations;
std::string scope,notice;
char stationName[65]="";
int selected=-1,confirmTravel=-1,confirmDelete=-1;
int editing=-1;
char editName[65]{},editNote[travel::maxNoteBytes+1]{};
std::string editScope;
bool dataOk=true,readyToTravel=false;
travel::Journey journey;
exploreui::TextFont font;
unsigned lastTown=0;
ULONGLONG nextDiscover=0;
explore::Names nativeNames;
bool namesLoaded=false;
visits::Dwell visitDwell;
unsigned bossReadPlace=0;
std::uint64_t bossReadTaskKey=0;
ULONGLONG nextBossRead=0;
std::vector<travel::Station> encounters;
bool encountersOk=true;
bool encountersDirty=false;
ULONGLONG nextEncounter=0;

bool supported(){
    auto& m=cube::mod();const unsigned base=reinterpret_cast<unsigned>(GetModuleHandleW(nullptr));
    const auto dos=m.read<IMAGE_DOS_HEADER>(base);
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0x40||dos.e_lfanew>0x1000)return false;
    const auto nt=m.read<IMAGE_NT_HEADERS32>(base+dos.e_lfanew);
    return nt.Signature==IMAGE_NT_SIGNATURE&&nt.FileHeader.Machine==IMAGE_FILE_MACHINE_I386&&nt.FileHeader.TimeDateStamp==0x51ea955e&&nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC&&nt.OptionalHeader.SizeOfImage==0x3bc000&&nt.OptionalHeader.AddressOfEntryPoint==0x28e1e0;
}
std::string worldName(unsigned gc){
    auto& m=cube::mod();const unsigned str=gc+0x2e4+0x94;
    const auto n=m.read<unsigned>(str+0x10,0xffffffffu),cap=m.read<unsigned>(str+0x14,0xffffffffu);
    if(!n||n>128||cap<n||cap>1024)return {};
    const auto address=cap<16?str:m.read<unsigned>(str);std::string name(n,'\0');
    if(!m.raw()->mem.read(m.raw(),address,name.data(),n)||m.read<char>(address+n)!=0||std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32;}))return {};
    return name;
}
void cancelEdit(){editing=-1;editScope.clear();editName[0]=editNote[0]=0;}
void reset(){scope.clear();stations.clear();journey={};selected=confirmTravel=confirmDelete=-1;cancelEdit();dataOk=true;stationName[0]=0;notice.clear();lastTown=0;nextDiscover=0;visitDwell={};bossReadPlace=0;bossReadTaskKey=0;nextBossRead=0;encounters.clear();encountersOk=true;encountersDirty=false;nextEncounter=0;}
bool context(CubePlayer& p,CubeSession& s){
    auto& m=cube::mod();const auto api=m.raw();p={};s={};
    if(!api->player.get(api,&p)||!api->session.get(api,&s)||s.gameState!=CUBE_STATE_IN_WORLD||!p.address||!p.hasName||!p.name[0]||!std::memchr(p.name,0,sizeof(p.name)))return false;
    const auto w=m.world();const auto name=worldName(s.address);
    if(!w.valid()||!w.hasSeed()||name.empty())return false;
    const auto next=nav::worldScope(w.getSeed(),name)+"_player_"+std::to_string(nav::hashName(p.name));
    if(next!=scope){
        reset();scope=next;m.storage().setScope(scope.c_str());
        if(m.storage().has("stations"))dataOk=m.storage().size("stations")>0&&m.storage().size("stations")<=travel::maxDataBytes&&travel::decode(m.storage().getString("stations"),stations);
        if(m.storage().has("encounters")){encountersOk=m.storage().size("encounters")>0&&m.storage().size("encounters")<=travel::maxDataBytes&&travel::decode(m.storage().getString("encounters"),encounters);if(!encountersOk)m.log.error("fast travel: encounter history invalid; original retained, station data unaffected");}
        if(dataOk&&m.storage().size("last_city")>0&&m.storage().size("last_city")<=16){std::istringstream in(m.storage().getString("last_city"));unsigned id=0;std::string extra;if((in>>id)&&!(in>>extra)&&std::any_of(stations.begin(),stations.end(),[&](const auto& v){return v.cityId==id;}))lastTown=id;}
        notice=dataOk?"附近Boss信息自动记住；停稳登记落点，也可手动登记。":"驿站文件损坏，已锁定；原文件保留，请先核对备份。";
        m.log.info("fast travel: scope=%s stations=%u valid=%d",scope.c_str(),unsigned(stations.size()),dataOk?1:0);
    }
    return true;
}
bool persist(std::vector<travel::Station> next){
    if(!dataOk)return false;
    const auto storage=cube::mod().storage();const bool existed=storage.has("stations");
    const auto previous=existed?storage.getString("stations"):std::string{};const auto encoded=travel::encode(next);
    std::vector<travel::Station> verified;
    if(!travel::decode(encoded,verified)){notice="记录内容无效或超出容量，未保存。";return false;}
    // The shared SDK truncates the destination on write. Keep and verify a
    // module-local previous copy before a migration or edit touches that file.
    if(existed&&(!storage.putString("stations_backup",previous)||storage.getString("stations_backup")!=previous)){notice="备份失败，未修改原驿站，请重试。";return false;}
    if(!storage.putString("stations",encoded)||storage.getString("stations")!=encoded){
        const bool restored=existed?(storage.putString("stations",previous)&&storage.getString("stations")==previous):(!storage.has("stations")||storage.remove("stations"));
        if(!restored)dataOk=false;
        notice=restored?"保存失败，原驿站已保留，请重试。":"保存失败，已锁定，请检查 stations_backup 备份。";return false;
    }
    stations=std::move(next);return true;
}
void startEdit(int index){
    if(!readyToTravel||!dataOk||journey.active||index<0||index>=int(stations.size()))return;
    editing=index;editScope=scope;confirmTravel=confirmDelete=-1;
    std::snprintf(editName,sizeof(editName),"%s",stations[index].name.c_str());std::snprintf(editNote,sizeof(editNote),"%s",stations[index].note.c_str());notice.clear();
}
void saveEdit(){
    const auto originalScope=editScope;CubePlayer p{};CubeSession s{};
    if(!readyToTravel||!context(p,s)||scope!=originalScope||!dataOk||journey.active||editing<0||editing>=int(stations.size())){notice="地点或世界已变化，未保存，请重新选择。";return;}
    const std::string name=editName,note=editNote;
    if(!travel::label(name)){notice="名称不能为空或过长，请调整后保存。";return;}
    if(!travel::note(note)){notice="备注过长或包含无效字符，请调整后保存。";return;}
    for(size_t i=0;i<stations.size();++i)if(int(i)!=editing&&stations[i].name==name){notice="这个名称已被其他地点使用，请换一个名称。";return;}
    auto next=stations;next[editing].name=name;next[editing].note=note;
    if(persist(std::move(next))){cancelEdit();notice="名称和备注已保存。";}
}
bool zeroVelocity(){
    const auto a=cube::mod().raw();
    return a->player.setStat(a,CUBE_STAT_VEL_X,0)&&a->player.setStat(a,CUBE_STAT_VEL_Y,0)&&a->player.setStat(a,CUBE_STAT_VEL_Z,0);
}
bool exactPosition(const CubePlayer& p,travel::Point& result){
    travel::Fixed raw{};const auto a=cube::mod().raw();
    if(!a->mem.read(a,p.address+0x10,&raw,sizeof(raw)))return false;
    result=travel::units(raw);return travel::finite(result);
}
bool move(travel::Point p){
    // Trader Fast Travel preserves the native 64-bit position triplet. The typed
    // float teleport API loses sub-block precision at this world's ~8M coordinates.
    // Use the existing loader's guarded write, then verify the complete triplet.
    const auto a=cube::mod().raw();CubePlayer current{};
    if(!travel::finite(p)||!a->player.get(a,&current)||current.address!=journey.player||!current.alive)return false;
    const auto raw=travel::fixed(p);travel::Fixed verified{};
    return zeroVelocity()&&a->mem.write(a,current.address+0x10,&raw,sizeof(raw))&&a->mem.read(a,current.address+0x10,&verified,sizeof(verified))&&std::memcmp(&raw,&verified,sizeof(raw))==0;
}
bool nearbyCity(unsigned gc,travel::Point position,explore::Place& found,bool townOnly=true,double placeRadius=256){
    if(position.x<0||position.y<0)return false;
    auto& m=cube::mod();const int bx=int(position.x/explore::placeBlocks),by=int(position.y/explore::placeBlocks);const double radius=townOnly?1024:placeRadius;double best=radius,second=1e30;
    for(int x=std::max(0,bx-1);x<=std::min(8191,bx+1);++x)for(int y=std::max(0,by-1);y<=std::min(8191,by+1);++y){
        const auto pointer=gc+0x800d44+0xb0+unsigned((x/8)*1024+y/8)*4;const auto chunk=m.read<unsigned>(pointer);if(!chunk)continue;
        std::array<unsigned char,explore::recordSize> bytes{};explore::Place p;
        if(!m.raw()->mem.read(m.raw(),chunk+explore::chunkPlaces+unsigned((x%8)*8+y%8)*explore::recordSize,bytes.data(),unsigned(bytes.size()))||!explore::parse(bytes.data(),x,y,position.z,p)||p.city()!=townOnly||(!townOnly&&!std::strcmp(explore::placeType(p),"地标")))continue;
        const auto distance=std::hypot(position.x-p.position.x,position.y-p.position.y);if(distance<best){if(best<radius)second=best;best=distance;found=p;}else second=std::min(second,distance);
    }
    return best<radius&&(townOnly||second-best>=16);
}
void loadNames(){
    if(namesLoaded)return;auto& m=cube::mod();
    for(size_t i=0;i<nativeNames.size();++i){wchar_t text[32]{};unsigned n=0;for(;n<31;++n){if(!m.raw()->mem.read(m.raw(),m.rebase(0x400000+explore::nameRvas[i])+n*2,text+n,2))break;if(!text[n])break;}if(!n||n>=31||text[n])continue;const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,n,nullptr,0,nullptr,nullptr);if(count>0){nativeNames[i].resize(count);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,n,nativeNames[i].data(),count,nullptr,nullptr);}}
    namesLoaded=true;
}
travel::VisitInfo observePlace(explore::Place place,const travel::VisitInfo& previous,const CubeSession& session,travel::Point point,bool force=false,bool* bossNear=nullptr){
    loadNames();place.name=explore::name(place.seed,nativeNames);
    if(place.name.empty())place.name="#"+std::to_string(place.id+1);
    const auto key=visits::taskKey(place);const auto now=GetTickCount64();const bool changed=key&&key!=previous.taskKey;
    const bool needBoss=place.mission==5&&!place.done()&&(bossNear||changed||!previous.bossId||previous.bossName.empty()||(!previous.bossClass&&!previous.special));
    const bool query=needBoss&&(force||bossReadPlace!=place.id+1||bossReadTaskKey!=key||now>=nextBossRead);
    if(query){bossReadPlace=place.id+1;bossReadTaskKey=key;nextBossRead=now+10000;}
    auto& m=cube::mod();return visits::observe(place,previous,[&](unsigned address,void* out,unsigned bytes){return m.raw()->mem.read(m.raw(),address,out,bytes)!=0;},[&](unsigned address){return m.rebase(address);},session.address,point,query,bossNear);
}
std::string autoName(const std::string& suggested,unsigned id){
    const auto available=[](const std::string& name){return travel::label(name)&&std::none_of(stations.begin(),stations.end(),[&](const auto& s){return s.name==name;});};
    if(available(suggested))return suggested;
    for(unsigned i=0;i<=travel::maxStations;++i){const auto value=suggested+" #"+std::to_string(id)+(i?"-"+std::to_string(i):"");if(available(value))return value;}
    return {};
}
bool saveEncounters(){
    if(!encountersOk)return false;auto storage=cube::mod().storage();const auto text=travel::encode(encounters);std::vector<travel::Station> verified;
    if(!travel::decode(text,verified))return false;
    const bool existed=storage.has("encounters");const auto previous=existed?storage.getString("encounters"):std::string{};
    if(existed&&(!storage.putString("encounters_backup",previous)||storage.getString("encounters_backup")!=previous))return false;
    if(storage.putString("encounters",text)&&storage.getString("encounters")==text)return true;
    const bool restored=existed?(storage.putString("encounters",previous)&&storage.getString("encounters")==previous):(!storage.has("encounters")||storage.remove("encounters"));
    if(!restored)encountersOk=false;return false;
}
travel::VisitInfo rememberedInfo(const explore::Place& place,const travel::VisitInfo& previous){
    const auto stored=std::find_if(encounters.begin(),encounters.end(),[&](const auto& v){return visits::matches(v.visit,place);});
    if(stored!=encounters.end())return stored->visit;return previous;
}
void observeEncounters(const CubePlayer& p,const CubeSession& s,ULONGLONG now){
    if(now<nextEncounter||!readyToTravel||!dataOk||journey.active||!travel::single(s)||!p.alive||!p.hasPosition)return;
    nextEncounter=now+1000;travel::Point point;if(!exactPosition(p,point))return;auto& m=cube::mod();
    const auto seen=visits::nearbyBosses([&](unsigned a,void* out,unsigned n){return m.raw()->mem.read(m.raw(),a,out,n)!=0;},[&](unsigned a){return m.rebase(a);},s.address,point);
    if(!seen.empty())loadNames();bool changedHistory=false,changedStations=false;auto next=stations;
    for(auto v:seen){
        v.place.name=explore::name(v.place.seed,nativeNames);if(v.place.name.empty())v.place.name="#"+std::to_string(v.place.id+1);
        auto info=visits::information(v);auto known=std::find_if(encounters.begin(),encounters.end(),[&](const auto& e){return e.visit.placeId==info.placeId;});
        if(known!=encounters.end()&&known->visit.bossId==info.bossId&&known->visit.taskKey==info.taskKey){if(info.bossName.empty())info.bossName=known->visit.bossName;if(!info.bossClass&&!info.special)info.bossClass=known->visit.bossClass;}
        if(known==encounters.end()){if(encounters.size()<travel::maxStations){encounters.push_back({std::to_string(info.placeId),v.origin,0,"",info});changedHistory=true;}}
        else if(!visits::same(known->visit,info)){known->visit=info;changedHistory=true;}
        for(auto& station:next){
            const auto bx=int(station.point.x/2048),by=int(station.point.y/2048);
            const bool manual=!station.cityId&&!station.visit.placeId&&bx>=0&&by>=0&&unsigned((bx<<16)|by)==v.place.id;
            if((station.visit.placeId==info.placeId||manual)&&!visits::same(station.visit,info)){station.visit=info;changedStations=true;}
        }
    }
    if(changedHistory)encountersDirty=true;
    if(encountersDirty&&encountersOk){const bool saved=saveEncounters();if(saved)encountersDirty=false;m.log.info("fast travel: nearby boss observation retained during movement/combat; history=%d places=%u",saved?1:0,unsigned(encounters.size()));}
    if(changedStations&&persist(std::move(next)))notice="已记录现场Boss名字和职业；个人备注与原落点保留。";
    else if(changedHistory)notice=encountersDirty?"已观察到Boss，但历史保存失败，正在保留现场信息重试。":"已观察到Boss；信息已记住，停稳后可登记传送落点。";
}
void discoverPlace(const CubePlayer& p,const CubeSession& s,ULONGLONG now){
    if(!readyToTravel||!dataOk||journey.active||!travel::single(s)||!travel::standing(p)){visitDwell={};return;}
    travel::Point point;explore::Place place;if(!exactPosition(p,point)){visitDwell={};return;}
    auto& m=cube::mod();const auto read=[&](unsigned a,void* out,unsigned n){return m.raw()->mem.read(m.raw(),a,out,n)!=0;};
    const bool atCentre=nearbyCity(s.address,point,place,false);bool visited=false;
    if(!atCentre){
        explore::Place home;if(point.x>=0&&point.y>=0&&visits::placeAt(read,s.address,unsigned(point.x/2048),unsigned(point.y/2048),point.z,home)&&
            std::any_of(encounters.begin(),encounters.end(),[&](const auto& e){return visits::matches(e.visit,home);})){place=home;visited=true;}
        else if(!nearbyCity(s.address,point,place,false,1024)||place.mission!=5||place.done()){visitDwell={};return;}
    }
    if(!visitDwell.ready(place.id+1,point,now))return;
    size_t index=stations.size();double nearest=1e30;
    for(size_t i=0;i<stations.size();++i)if(stations[i].visit.placeId==place.id+1){const auto distance=travel::horizontal(stations[i].point,point);if(distance<nearest){nearest=distance;index=i;}}
    if(index==stations.size()){
        nearest=32;for(size_t i=0;i<stations.size();++i)if(!stations[i].cityId&&!stations[i].visit.placeId&&std::abs(stations[i].point.z-point.z)<4){const auto distance=travel::horizontal(stations[i].point,point);if(distance<nearest){nearest=distance;index=i;}}
    }
    if(index==stations.size()&&stations.size()>=travel::maxStations){notice="驿站已满，未自动登记新地点；已有记录保留。";return;}
    const auto previous=index<stations.size()?stations[index].visit:travel::VisitInfo{};bool bossNear=false;
    const auto stored=rememberedInfo(place,previous);const auto info=observePlace(place,stored,s,point,atCentre&&!stored.placeId,(atCentre||visited)?nullptr:&bossNear);
    if(!atCentre&&!visited&&!bossNear)return;
    if(index<stations.size()&&visits::same(info,stations[index].visit))return;
    auto next=stations;const bool added=index==stations.size();
    if(added){const auto name=autoName(info.title,place.id+1);if(name.empty())return;next.push_back({name,point,0,{ },info});}
    else next[index].visit=info;
    if(persist(std::move(next))){notice=added?"已自动登记到访地点："+info.title:"地点和Boss的自动信息已更新，个人备注保留。";cube::mod().log.info("fast travel: %s observed place=%u boss=%llu class=%u; player anchor and personal metadata retained",added?"registered":"updated",info.placeId,info.bossId,info.bossClass);}
}
void rememberTown(unsigned id){
    if(id!=lastTown&&cube::mod().storage().putString("last_city",std::to_string(id)))lastTown=id;
}
void discoverTown(const CubePlayer& p,const CubeSession& s){
    if(!readyToTravel||!dataOk||journey.active||!travel::single(s)||!travel::standing(p))return;
    travel::Point point;if(!exactPosition(p,point))return;explore::Place city;
    if(!nearbyCity(s.address,point,city))return;
    const unsigned id=city.id+1;
    const auto known=std::find_if(stations.begin(),stations.end(),[&](const auto& v){return v.cityId==id;});
    if(known!=stations.end()&&travel::horizontal(known->point,point)<48){rememberTown(id);return;}
    if(known==stations.end()&&stations.size()>=travel::maxStations)return;
    const auto shops=town::scan(cube::mod(),s.address,{point.x,point.y,point.z});
    if(std::none_of(shops.begin(),shops.end(),[&](const auto& v){return std::hypot(v.position.x-point.x,v.position.y-point.y)<32&&std::abs(v.position.z-point.z)<4;}))return;
    if(known!=stations.end()){rememberTown(id);return;}
    // Adapt the upstream grounded merchant-anchor workflow to native categories
    // and the already verified name table; English "City" matching fails in Chinese.
    loadNames();
    auto name=explore::name(city.seed,nativeNames);name="城镇 · "+(name.empty()?std::to_string(id):name);
    if(std::any_of(stations.begin(),stations.end(),[&](const auto& v){return v.name==name;}))name+=" "+std::to_string(id);
    if(!travel::label(name))return;auto next=stations;next.push_back({name,point,id});
    if(persist(std::move(next))){rememberTown(id);notice="已自动登记城镇："+name;cube::mod().log.info("fast travel: auto discovered grounded shop anchor city=%u",id);}
}
void discoverNearby(const CubePlayer& p,const CubeSession& s){discoverTown(p,s);discoverPlace(p,s,GetTickCount64());}
void registerStation(){
    CubePlayer p{};CubeSession s{};if(!readyToTravel||!context(p,s)||!travel::single(s)||!travel::standing(p)||journey.active||!dataOk){notice="请在单人世界的平整空地站稳，再登记。";return;}
    travel::Point point;if(!exactPosition(p,point)){notice="无法核实完整落点，未登记。";return;}
    explore::Place place;travel::VisitInfo info;
    if(nearbyCity(s.address,point,place,false))info=observePlace(place,rememberedInfo(place,{}),s,point,true);
    std::string name=stationName;if(name.empty())name=info.placeId?autoName(info.title,info.placeId):"驿站 "+std::to_string(stations.size()+1);
    if(!travel::label(name)||stations.size()>=travel::maxStations){notice="名称不能为空或超过64字节，每个角色世界最多32个驿站。";return;}
    for(const auto& v:stations)if(v.name==name||(travel::horizontal(v.point,travel::position(p))<3&&std::abs(v.point.z-p.z)<2)){notice="名称重复或附近已有驿站，请选择已有驿站。";return;}
    auto next=stations;next.push_back({name,point,0,"",info});
    if(persist(std::move(next))){selected=int(stations.size())-1;stationName[0]=0;notice="已登记当前位置；以后按 F8 选择这里传送。";}
}
void beginTravel(int index,unsigned long long now){
    CubePlayer p{};CubeSession s{};
    if(!readyToTravel||!context(p,s)||!travel::single(s)||!travel::standing(p)||journey.active||!dataOk||index<0||index>=int(stations.size())){notice="请在单人世界站稳，关闭其他菜单后重试。";return;}
    const auto ui=cube::mod().ui();if(!ui.valid()||ui.isMenuOpen()){notice="请先关闭地图、背包和其他游戏菜单，再按 F8。";return;}
    journey.begin(p,stations[index].point,now);
    if(!exactPosition(p,journey.source)){journey.active=false;notice="无法核实出发点，未传送。";return;}
    auto landing=journey.destination;landing.z+=.2f;
    if(!move(landing)){
        const bool restored=move(journey.source);journey.active=false;
        notice=restored?"传送接口拒绝操作，已返回出发点。":"传送接口异常，请退出重开并检查日志。";
        cube::mod().log.error("fast travel: initial move failed, restore=%d",restored?1:0);return;
    }
    cube::mod().log.info("fast travel: start to %s (%.3f %.3f %.3f)",stations[index].name.c_str(),landing.x,landing.y,landing.z);
    cube::mod().menu().setOpen(false);confirmTravel=confirmDelete=-1;
}
void tick(){
    if(!journey.active){const auto now=GetTickCount64();if(now<nextDiscover&&now<nextEncounter)return;CubePlayer p{};CubeSession s{};if(context(p,s)){observeEncounters(p,s,now);if(now>=nextDiscover){nextDiscover=now+3000;discoverNearby(p,s);}}return;}
    CubePlayer p{};CubeSession s{};
    if(!context(p,s)||!travel::single(s)){journey.active=false;return;}
    const auto step=journey.step(p,GetTickCount64());
    if(step==travel::Step::Hold){auto target=journey.destination;target.z+=.2f;if(!move(target)){const bool restored=move(journey.source);journey.active=false;notice=restored?"传送中断，已返回出发点。":"返回失败，请检查日志。";cube::mod().menu().setOpen(true);}}
    else if(step==travel::Step::Arrived){notice="已到达驿站。";cube::mod().log.info("fast travel: grounded at destination");}
    else if(step==travel::Step::Return){const bool restored=move(journey.source);notice=restored?"落点未能稳定加载，已返回出发点。":"落点异常且返回失败，请检查日志。";cube::mod().log.info("fast travel: landing not confirmed; returned=%d",restored?1:0);cube::mod().menu().setOpen(true);}
}
void CUBE_CALL event(CubeEventArgs* e){
    if(!readyToTravel||!e)return;
    if(e->event==CUBE_EVENT_FRAME)tick();
    else if(e->event==CUBE_EVENT_WORLD_EXIT){reset();cube::mod().menu().setOpen(false);}
}
void draw(){
    CubePlayer p{};CubeSession s{};const bool ready=context(p,s);auto& m=cube::mod();
    if(ready){const auto now=GetTickCount64();observeEncounters(p,s,now);if(now>=nextDiscover){nextDiscover=now+3000;discoverNearby(p,s);}}
    exploreui::ScopedTextFont ownFont(font.ensure(ImGui::GetFont()));
    const auto size=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({size.x*.5f,size.y*.5f},ImGuiCond_Appearing,{.5f,.5f});
    ImGui::SetNextWindowSize({std::min(620.f,size.x-24),std::min(600.f,size.y-24)},ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0);ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,{.07f,.13f,.17f,1});ImGui::PushStyleColor(ImGuiCol_Header,{.21f,.32f,.35f,1});
    bool open=true;
    if(ImGui::Begin("旅行驿站 · F8###fast_travel",&open,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoResize)){
        ImGui::TextWrapped("附近Boss名字和职业自动记录，交战时也能观察；传送落点需要停稳。F8关闭。");
        ImGui::Separator();
        if(!ready)ImGui::TextWrapped("请先进入角色和世界。");
        else if(editing>=0&&editing<int(stations.size())){
            ImGui::TextUnformatted("编辑地点");
            const auto automatic=visits::text(stations[editing].visit);
            if(!automatic.empty()){ImGui::BeginChild("edit_auto_info",{0,58},ImGuiChildFlags_Borders);ImGui::TextWrapped("%s",automatic.c_str());ImGui::EndChild();}
            ImGui::TextUnformatted("名称");ImGui::SetNextItemWidth(-1);ImGui::InputText("##edit_name",editName,sizeof(editName));
            ImGui::TextUnformatted("备注（支持换行）");
            ImGui::InputTextMultiline("##edit_note",editNote,sizeof(editNote),{-1,std::max(80.f,ImGui::GetContentRegionAvail().y-98)});
            ImGui::TextDisabled("可写难度、是否打过、打法和下次准备等。");
            ImGui::BeginDisabled(!dataOk||journey.active);
            if(ImGui::Button("保存修改",{140,32}))saveEdit();ImGui::EndDisabled();ImGui::SameLine();
            if(ImGui::Button("取消编辑",{140,32})){cancelEdit();notice="已取消，原记录保留。";}
        }else {
            ImGui::TextWrapped("请在平整、开阔的地面登记，避开屋顶、水面、门边和坐骑。");
            const bool canAct=dataOk&&travel::single(s)&&travel::standing(p)&&!journey.active;
            ImGui::BeginDisabled(!canAct);
            ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##station_name","驿站名称（留空使用地点名或自动编号）",stationName,sizeof(stationName));
            if(ImGui::Button("登记当前位置",{-1,32}))registerStation();
            ImGui::EndDisabled();
            const auto last=std::find_if(stations.begin(),stations.end(),[](const auto& v){return lastTown&&v.cityId==lastTown;});
            ImGui::BeginDisabled(!canAct||last==stations.end());if(ImGui::Button("返回最近城镇",{-1,30})&&last!=stations.end()){selected=confirmTravel=int(last-stations.begin());confirmDelete=-1;}ImGui::EndDisabled();
            ImGui::Text("已登记 %u / 32",unsigned(stations.size()));
            ImGui::BeginChild("station_list",{0,std::max(60.f,ImGui::GetContentRegionAvail().y-260)},ImGuiChildFlags_Borders);
            if(stations.empty())ImGui::TextWrapped("还没有驿站。到城镇商店、地标中心或可确认的Boss附近停稳，也可手动登记野外落点。");
            for(size_t i=0;i<stations.size();++i){
                ImGui::PushID(int(i));const auto& v=stations[i];char distance[64];std::snprintf(distance,sizeof(distance),"  %.0f m",travel::horizontal(travel::position(p),v.point));
                if(ImGui::Selectable((v.name+distance).c_str(),selected==int(i))){selected=int(i);confirmTravel=confirmDelete=-1;}
                if(v.visit.bossId)ImGui::TextDisabled("Boss：%s · %s",v.visit.bossName.empty()?"名字未知":v.visit.bossName.c_str(),visits::classLabel(v.visit).c_str());
                if(!v.note.empty())ImGui::TextDisabled("%s",v.note.substr(0,v.note.find_first_of("\r\n")).c_str());ImGui::PopID();
            }
            ImGui::EndChild();
            const bool hasSelection=selected>=0&&selected<int(stations.size());
            ImGui::BeginChild("selected_note",{0,96},ImGuiChildFlags_Borders);
            if(hasSelection){const auto automatic=visits::text(stations[selected].visit);if(!automatic.empty()){ImGui::TextWrapped("%s",automatic.c_str());ImGui::Separator();}}
            ImGui::TextWrapped("%s",hasSelection?(stations[selected].note.empty()?"暂无备注，可以点击“编辑名称/备注”补充。":stations[selected].note.c_str()):"选中地点后可查看备注。");ImGui::EndChild();
            ImGui::BeginDisabled(!canAct||!hasSelection);
            if(ImGui::Button("传送到选中驿站",{200,32})){confirmTravel=selected;confirmDelete=-1;}ImGui::EndDisabled();ImGui::SameLine();
            ImGui::BeginDisabled(!dataOk||journey.active||!hasSelection);
            if(ImGui::Button("编辑名称/备注",{200,32}))startEdit(selected);ImGui::SameLine();
            if(ImGui::Button("移除地点",{160,32})){confirmDelete=selected;confirmTravel=-1;}ImGui::EndDisabled();
            if(confirmTravel>=0&&confirmTravel<int(stations.size())){
                ImGui::Text("前往：%s",stations[confirmTravel].name.c_str());ImGui::BeginDisabled(!canAct);
                if(ImGui::Button("确认传送"))beginTravel(confirmTravel,GetTickCount64());ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("取消"))confirmTravel=-1;
            } else if(confirmDelete>=0&&confirmDelete<int(stations.size())){
                ImGui::Text("移除：%s",stations[confirmDelete].name.c_str());ImGui::BeginDisabled(!dataOk||journey.active);
                if(ImGui::Button("确认移除")){auto next=stations;next.erase(next.begin()+confirmDelete);if(persist(std::move(next))){selected=confirmDelete=-1;notice="已移除驿站，游戏中的地点保持原状。";}}ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("取消"))confirmDelete=-1;
            }
            if(!travel::single(s))ImGui::TextWrapped("当前只支持可核实的单人世界，联机或网络状态未知时锁定。");
            else if(!canAct)ImGui::TextWrapped(journey.active?"正在等待落点稳定…":"请停下、站稳，并结束当前动作后操作。");
        }
        if(!notice.empty())ImGui::TextWrapped("%s",notice.c_str());
    }
    ImGui::End();ImGui::PopStyleColor(2);ImGui::PopStyleVar(2);
    if(!open)m.menu().setOpen(false);
}
}
CUBE_MOD("Fast Travel","0.1.4","CubeWorld Local Mods"){
    mod.setId("local.cubeworld.fast_travel");mod.setCapabilities(cube::Capability::Overlay|cube::Capability::RawMem|cube::Capability::Writes);
    if(!supported()||!mod.raw()->mem.write||!mod.raw()->player.setStat||!mod.raw()->events.subscribe||!mod.raw()->events.unsubscribe){mod.log.error("fast travel disabled: unsupported Alpha build or missing guarded position/event API");return;}
    const auto a=mod.raw();
    const auto frame=a->events.subscribe(a,CUBE_EVENT_FRAME,&event,nullptr),exit=a->events.subscribe(a,CUBE_EVENT_WORLD_EXIT,&event,nullptr);
    if(!frame||!exit){if(frame)a->events.unsubscribe(a,frame);if(exit)a->events.unsubscribe(a,exit);mod.log.error("fast travel disabled: landing protection event subscription failed");return;}
    mod.menu().setToggleKey(VK_F8).setOpen(false).onDraw(&draw);
    if(!mod.menu().handle()){a->events.unsubscribe(a,frame);a->events.unsubscribe(a,exit);mod.log.error("fast travel disabled: menu registration failed");return;}
    readyToTravel=true;
    mod.log.info("fast travel 0.1.4 ready: movement/combat boss observations persist independently of safe teleport anchors; native static leaders supported; personal notes retained; single player only");
}

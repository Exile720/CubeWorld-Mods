#include "town.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
int main() {
    int count=0;
    auto check=[&](bool ok,const char* label){++count;if(!ok){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}};
    using namespace town;
    check(std::string(name(33))=="旅馆"&&std::string(name(35))=="武器店"&&std::string(name(37))=="鉴定店","verified native sign mapping");
    check(std::string(name(38))=="铁匠铺"&&std::string(name(39))=="木工坊"&&std::string(name(40))=="纺织坊","workshop signs");
    check(!name(71)[0]&&!name(77)[0],"workstations are not shop signs");
    check(markerType(3,128)==34&&markerType(3,129)==35&&markerType(3,130)==36&&markerType(3,131)==37,"native merchant marker roles");
    check(markerType(3,132)==33&&markerType(3,133)==38&&markerType(3,134)==39&&markerType(3,135)==40,"native inn and workshop marker roles");
    check(!markerType(6,129)&&!markerType(1,129)&&!markerType(3,136)&&!markerType(3,4),"only native shop markers qualify");
    check(!name(21)[0]&&!name(65)[0]&&!name(0)[0],"market decorations, camps and statues are not invented shops");
    const nav::Point player{8398000.125,8399000.25,80};
    std::vector<Place> places;
    check(merge(places,{35,{player.x+20,player.y,90}}),"new shop discovered");
    check(!merge(places,{35,{player.x+20.5,player.y+.5,91}})&&places.size()==1,"duplicate sign merged");
    check(merge(places,{35,{player.x+60,player.y,90}}),"distinct shop retained");
    check(merge(places,{38,{player.x+30,player.y+40,85}}),"workshop discovered");
    check(!merge(places,{65,player}),"unknown types excluded");
    const auto rows=nearby(places,player);
    check(rows.size()==2&&rows[0].type==35&&rows[1].type==38,"one stable row per type");
    check(nav::distance(rows[0].position,player)==20,"nearest duplicate shop selected");
    check(nearby(places,{player.x+3000,player.y,80}).empty(),"distant cities excluded from nearby list");
    check(std::string(targetName(places,rows[1].position))=="铁匠铺","navigation label resolves exact facility");
    check(std::string(targetName(places,player))=="目的地","manual waypoint remains generic");
    std::vector<Place> restored;
    check(decode(encode(places),restored)&&restored.size()==3,"town index round trip");
    check(restored[0].position.x==places[0].position.x,"large world coordinate precision retained");
    check(!decode("CW_TOWN_1 0",restored),"old furniture-based cache rejected");
    check(!decode("CW_TOWN_2 513",restored),"oversized count rejected");
    check(!decode("CW_TOWN_2 1 35 1 2",restored),"truncated index rejected");
    check(!decode("CW_TOWN_2 1 71 1 2 3",restored),"workstation cache rejected");
    check(!decode("CW_TOWN_2 1 35 1e99 2 3",restored),"invalid position rejected");
    check(!decode("CW_TOWN_2 0 garbage",restored),"trailing index contents rejected");
    check(restored.size()==3,"failed decode preserves current cache");
    // These three nearby inn positions came from the new world's legacy cache.
    // The fixed anchor below is a replay fixture, not a captured live home position.
    const Place fixedInn{33,{8397748,8398107,117.08027648925781}};
    const Place otherInn{33,{8398107,8397799,102.08001708984375}};
    const std::vector<Place> movingInnCache={
        {33,{8397746.9870605469,8398107.0526428223,117.08027648925781}},
        {33,{8397748.9717254639,8398109.5191040039,117.08027648925781}},
        {33,{8397749.2422332764,8398107.2322387695,117.08024597167969}},otherInn};
    auto repaired=movingInnCache;size_t added=99;
    check(!reconcile(repaired,{},added)&&added==0&&encode(repaired)==encode(movingInnCache),
          "unloaded inn without verified anchor is not guessed or removed");
    check(reconcile(repaired,{fixedInn},added)&&added==0&&repaired.size()==2,
          "observed moving inn cache collapses only with verified fixed anchor");
    check(repaired[0].position.x==fixedInn.position.x&&repaired[0].position.y==fixedInn.position.y&&
          repaired[0].position.z==fixedInn.position.z,"moving inn labels snap exactly to native fixed anchor");
    check(encode({repaired[1]})==encode({otherInn}),"second real distant inn is retained unchanged");
    const auto repairedData=encode(repaired);
    added=99;
    check(!reconcile(repaired,{fixedInn},added)&&added==0&&encode(repaired)==repairedData,
          "repeated fixed-anchor scan is idempotent with no new discoveries");
    const Place adjacentInn{33,{fixedInn.position.x+5,fixedInn.position.y,fixedInn.position.z}};
    std::vector<Place> twoNearbyInns={
        {33,{fixedInn.position.x+.75,fixedInn.position.y,fixedInn.position.z}},
        {33,{adjacentInn.position.x-.75,adjacentInn.position.y,adjacentInn.position.z}}};
    check(reconcile(twoNearbyInns,{fixedInn,adjacentInn},added)&&added==0&&twoNearbyInns.size()==2&&
          twoNearbyInns[0].position.x==fixedInn.position.x&&twoNearbyInns[1].position.x==adjacentInn.position.x,
          "nearest verified anchors keep separate nearby inns distinct");
    const Place newShop{34,{fixedInn.position.x+30,fixedInn.position.y,fixedInn.position.z}};
    check(reconcile(repaired,{fixedInn,newShop},added)&&added==1&&repaired.size()==3,
          "new fixed shop marks cache dirty and increments discovery count once");
    check(!reconcile(repaired,{fixedInn,newShop},added)&&added==0,
          "repeat discovery of the same fixed shop does not dirty cache");
    const Place unloadedCity{35,{fixedInn.position.x+4000,fixedInn.position.y+3000,90}};
    const Place unloadedNearby{36,{fixedInn.position.x+150,fixedInn.position.y+150,95}};
    std::vector<Place> partialScan={unloadedCity,unloadedNearby,fixedInn};
    const auto partialData=encode(partialScan);
    check(!reconcile(partialScan,{fixedInn},added)&&added==0&&encode(partialScan)==partialData,
          "partial loaded scan preserves other cities and unloaded nearby shops");
    const Place differentType{34,{fixedInn.position.x+.5,fixedInn.position.y,fixedInn.position.z}};
    const Place differentFloor{33,{fixedInn.position.x+.5,fixedInn.position.y,fixedInn.position.z+4}};
    const Place outsideSnap{33,{fixedInn.position.x+8,fixedInn.position.y,fixedInn.position.z}};
    std::vector<Place> protectedPlaces={differentType,differentFloor,outsideSnap};
    const auto protectedData=encode(protectedPlaces);
    check(reconcile(protectedPlaces,{fixedInn},added)&&added==1&&protectedPlaces.size()==4&&
          encode(std::vector<Place>(protectedPlaces.begin(),protectedPlaces.begin()+3))==protectedData,
          "different types, floors and points at snap radius are preserved");
    std::vector<Place> legacyCache;
    check(decode(encode(movingInnCache),legacyCache)&&reconcile(legacyCache,{fixedInn},added)&&added==0,
          "legacy CW_TOWN_2 moving-coordinate cache migrates without changing format");
    std::vector<Place> repairedRoundTrip;
    check(encode(legacyCache).rfind("CW_TOWN_2 ",0)==0&&decode(encode(legacyCache),repairedRoundTrip)&&
          encode(repairedRoundTrip)==repairedData,"repaired cache retains CW_TOWN_2 compatibility and precision");
    Rect a{};std::vector<Rect> occupied;
    check(labelRect({100,100},60,20,{0,0,300,300},occupied,a),"label placed above anchor");
    occupied.push_back(a);Rect b{};
    check(labelRect({100,100},60,20,{0,0,300,300},occupied,b)&&!overlaps(a,b,3),"crowded label chooses another side");
    check(!labelRect({100,100},60,20,{0,0,300,300},{{0,0,300,300}},b),"covered labels hidden rather than overlapping");
    check(!labelRect({-100,-100},60,20,{0,0,300,300},{},b),"offscreen labels excluded");
    const Rect arrow{87.25f,72.75f,26,28};Rect selectedName{};
    check(labelRect({100.25,100.75},60,20,{0,0,300,300},{},selectedName,&arrow)&&
          !overlaps(selectedName,arrow,3),"selected name remains visible clear of downward arrow at fractional anchor");
    check(contains({10,20,100,30},{20,30})&&!contains({10,20,100,30},{110,30}),"click bounds have exclusive right edge");
    std::cout<<"PASS "<<count<<" town label checks\n";
}

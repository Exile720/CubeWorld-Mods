#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "cube_sdk.h"
#include "loader/core/owner_name.h"
#include "stock.hpp"
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

// Executes the actual DLL in this test process with synthetic SDK memory only.
// This is not a test of native detour installation, the game, or a running shop UI.
namespace {
unsigned checks=0,failures=0;
void check(bool result,const char* description) {
    ++checks;if(!result) {++failures;std::printf("FAIL: %s\n",description);}
}
constexpr unsigned gc=0x01000000,npc=0x03000000,widget=0x05000000,player=0x06000000;
constexpr unsigned sourceRows=0x04000000,sourceCells=0x04001000;
constexpr unsigned tradeRows=0x04010000,tradeCells=0x04011000;
constexpr unsigned recipeBook=0x06100000,recipeHead=0x06110000,recipeNodes=0x06120000;
constexpr unsigned copyFunction=0x0044ad30,priceFunction=0x004c76e0;
std::map<unsigned,std::vector<unsigned char>> memory;
struct Hook {CubeHookFn fn=nullptr;CubeCallConv cc=CUBE_CC_CDECL;int count=-1;};
std::map<unsigned,Hook> hooks;
modloader::ModContext context{};
unsigned removed=0,writes=0;

void putBytes(unsigned address,const void* bytes,unsigned size) {
    auto& value=memory[address];value.resize(size);
    if(size) std::memcpy(value.data(),bytes,size);
}
template<class T> void put(unsigned address,const T& value) {
    putBytes(address,&value,static_cast<unsigned>(sizeof(value)));
}
void setRecipes(int level,const std::vector<int>& levels) {
    put(player+0x190,level);put(player+0x1d28,recipeBook);put(recipeBook+0x14,recipeHead);
    const unsigned count=static_cast<unsigned>(levels.size());put(recipeBook+0x18,count);
    put(recipeHead,std::array<unsigned,2>{count?recipeNodes:recipeHead,
                                        count?recipeNodes+(count-1)*0x200:recipeHead});
    for(unsigned i=0;i<count;++i)
        put(recipeNodes+i*0x200,potions::RecipeNode{
            i+1<count?recipeNodes+(i+1)*0x200:recipeHead,
            i?recipeNodes+(i-1)*0x200:recipeHead,potions::lifePotion(levels[i])});
}
int32_t CUBE_CALL read(const CubeApi*,uint32_t address,void* output,uint32_t size) {
    for(const auto& segment:memory) {
        if(address<segment.first) continue;
        const auto offset=std::uint64_t(address)-segment.first;
        if(offset+size<=segment.second.size()) {
            std::memcpy(output,segment.second.data()+static_cast<std::size_t>(offset),size);return 1;
        }
    }
    return 0;
}
int32_t CUBE_CALL readable(const CubeApi* api,uint32_t address,uint32_t size) {
    std::vector<unsigned char> bytes(size);return read(api,address,bytes.data(),size);
}
uint32_t CUBE_CALL rebase(const CubeApi*,uint32_t address) {return address;}
int32_t CUBE_CALL write(const CubeApi*,uint32_t,const void*,uint32_t) {++writes;return 0;}
void CUBE_CALL log(const CubeApi*,CubeLogLevel,const char* message) {
    std::printf("DLL: %s\n",message);
}
int32_t CUBE_CALL onRaw(const CubeApi*,uint32_t address,CubeCallConv cc,int32_t count,
                       CubeHookFn fn,void*) {
    hooks[address]={fn,cc,count};return 1;
}
int32_t CUBE_CALL removeRaw(const CubeApi*,uint32_t address) {
    if(hooks.erase(address)) {++removed;return 1;}return 0;
}
CubeHookCall call(unsigned address,unsigned self,unsigned source=0) {
    CubeHookCall value{};value.structSize=sizeof(value);value.hook=CUBE_HOOK_RAW;
    value.address=address;value.self=self;value.argCount=hooks.at(address).count;
    value.argi[0]=static_cast<int>(source);
    // The real raw pool exposes each incoming argument through both bit-identical views.
    std::memcpy(&value.argf[0],&source,sizeof(source));return value;
}
void dispatch(CubeHookCall& value) {
    const auto original=value;
    hooks.at(value.address).fn(&value);
    // Use the framework's actual capability policy. This one-subscriber simulation
    // mirrors dispatchMatched's restoration when an observing mod attempts writes.
    if(!modloader::ownerMayWrite(&context.api)) value=original;
}
unsigned marshalledArg(const CubeHookCall& value,unsigned original) {
    unsigned floatBits=0;std::memcpy(&floatBits,&value.argf[0],sizeof(floatBits));
    return floatBits!=original?floatBits:static_cast<unsigned>(value.argi[0]);
}
template<class T> bool ownDllObject(unsigned address,T& output) {
    // The only actual dereferences are the DLL-owned scratch pointer returned by
    // its handler, followed by bounded row/cell pointers inside that scratch.
    MEMORY_BASIC_INFORMATION region{};
    if(!VirtualQuery(reinterpret_cast<const void*>(address),&region,sizeof(region)) ||
       region.State!=MEM_COMMIT || (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
    const auto end=reinterpret_cast<std::uintptr_t>(region.BaseAddress)+region.RegionSize;
    if(std::uint64_t(address)+sizeof(T)>end) return false;
    std::memcpy(&output,reinterpret_cast<const void*>(address),sizeof(T));return true;
}
void fixture() {
    context.api.abiVersion=CUBE_ABI_VERSION;context.api.structSize=sizeof(CubeApi);
    context.api.environment=CUBE_ENV_CLIENT;context.api.log.write=&log;
    context.api.mem={&read,&readable,&rebase,&write};
    context.api.hooks.onRaw=&onRaw;context.api.hooks.removeRaw=&removeRaw;
    const unsigned base=reinterpret_cast<unsigned>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=0x80;put(base,dos);
    IMAGE_NT_HEADERS32 nt{};nt.Signature=IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine=IMAGE_FILE_MACHINE_I386;nt.FileHeader.TimeDateStamp=0x51ea955e;
    nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt.OptionalHeader.SizeOfImage=0x3bc000;nt.OptionalHeader.AddressOfEntryPoint=0x28e1e0;
    put(base+0x80,nt);
    const unsigned char copyBytes[]={0x55,0x8b,0xec,0x6a,0xff,0x68};
    const unsigned char priceBytes[]={0x55,0x8b,0xec,0x83,0xec,0x08,0xf3,0x0f};
    putBytes(copyFunction,copyBytes,sizeof(copyBytes));putBytes(priceFunction,priceBytes,sizeof(priceBytes));
    put(0x76b1c8,gc);put(gc+0x8006d0,player);put(gc+0x8008d8,npc);
    put(gc+0x80095c,widget);put(gc+0x8009b0,std::uint8_t{0});
    put(npc+0x140,std::uint8_t{0x80});put(widget+0x1b4,0);
    setRecipes(13,{1,5,9,13});
    potions::Cell originals[2]{};originals[0].count=1;originals[0].item.bytes[0]=10;
    originals[1].count=3;originals[1].item.bytes[0]=12;
    putBytes(sourceCells,originals,sizeof(originals));
    put(sourceRows,potions::Vector{sourceCells,sourceCells+sizeof(originals),sourceCells+sizeof(originals)});
    put(npc+0x11dc,potions::Vector{sourceRows,sourceRows+12,sourceRows+12});
}
void priceExcluded(unsigned item,const char* description) {
    auto value=call(priceFunction,item);dispatch(value);
    check(!value.cancel && !value.overrideReturn,description);
}
void exercise() {
    const auto merchantBefore=memory.at(sourceCells);
    auto copy=call(copyFunction,gc+0x800c0c,npc+0x11dc);dispatch(copy);
    const unsigned rewritten=marshalledArg(copy,npc+0x11dc);
    check(rewritten!=npc+0x11dc,"stock source rewrite survives capability gate and int/float marshalling");
    check(!copy.cancel,"native stock assignment remains enabled");
    check(memory.at(sourceCells)==merchantBefore,"merchant stock remains unchanged");
    if(rewritten==npc+0x11dc) return;
    potions::Vector outer{},row{};potions::Cell cells[3]{};
    const bool valid=ownDllObject(rewritten,outer) && outer.end-outer.begin==sizeof(row) &&
                     ownDllObject(outer.begin,row) && row.end-row.begin==sizeof(cells) &&
                     ownDllObject(row.begin,cells);
    check(valid,"rewritten source contains one bounded row and three cells");if(!valid) return;
    check(std::memcmp(cells,merchantBefore.data(),merchantBefore.size())==0,
          "actual DLL preserves both original item cells");
    check(cells[2].count==1 && potions::isLifePotion(cells[2].item,9),
          "actual DLL appends level nine below unlocked level thirteen");
    // Simulate the native deep copy into separate temporary-stock storage.
    putBytes(tradeCells,cells,sizeof(cells));
    put(tradeRows,potions::Vector{tradeCells,tradeCells+sizeof(cells),tradeCells+sizeof(cells)});
    put(gc+0x800c0c,potions::Vector{tradeRows,tradeRows+12,tradeRows+12});
    const unsigned potion=tradeCells+2*sizeof(potions::Cell)+4;
    auto price=call(priceFunction,potion);dispatch(price);
    check(price.cancel && price.overrideReturn && price.returnI==15,
          "actual DLL returns exactly fifteen copper for temporary-stock level nine potion");
    priceExcluded(tradeCells+4,"unrelated shop item retains native price");
    put(0x07000000,potions::lifePotion(9));
    priceExcluded(0x07000000,"player inventory or resale pointer retains native price");
    auto upgraded=cells[2].item;upgraded.bytes[0x10]=13;
    // Replace the owned synthetic cell segment with a higher-level potion.
    cells[2].item=upgraded;putBytes(tradeCells,cells,sizeof(cells));
    priceExcluded(potion,"higher-level LifePotion retains native price");
    cells[2].item=potions::lifePotion(9);putBytes(tradeCells,cells,sizeof(cells));
    put(npc+0x140,std::uint8_t{0x81});priceExcluded(potion,"weapon shop retains native prices");
    auto excluded=call(copyFunction,gc+0x800c0c,npc+0x11dc);dispatch(excluded);
    check(marshalledArg(excluded,npc+0x11dc)==npc+0x11dc,"weapon shop stock is not rewritten");
    put(npc+0x140,std::uint8_t{0x80});put(gc+0x8009b0,std::uint8_t{1});
    priceExcluded(potion,"network session retains native prices");
    excluded=call(copyFunction,gc+0x800c0c,npc+0x11dc);dispatch(excluded);
    check(marshalledArg(excluded,npc+0x11dc)==npc+0x11dc,"network stock is not rewritten");
    put(gc+0x8009b0,std::uint8_t{0});
    auto cancelled=call(priceFunction,potion);cancelled.cancel=1;dispatch(cancelled);
    check(cancelled.cancel && !cancelled.overrideReturn,"previous hook cancellation is respected");

    // Reopen the real DLL's shop hook after progression and after changing characters.
    auto reopen=[&](int expectedLevel,int expectedPrice) {
        auto value=call(copyFunction,gc+0x800c0c,npc+0x11dc);dispatch(value);
        const unsigned input=marshalledArg(value,npc+0x11dc);
        if(!expectedLevel) {
            check(input==npc+0x11dc,"no lower tier or invalid recipe list leaves native stock alone");
            priceExcluded(potion,"failed or empty offer clears previous shop price");return;
        }
        potions::Vector nextOuter{},nextRow{};potions::Cell nextCells[3]{};
        const bool nextValid=input!=npc+0x11dc && ownDllObject(input,nextOuter) &&
            ownDllObject(nextOuter.begin,nextRow) && nextRow.end-nextRow.begin==sizeof(nextCells) &&
            ownDllObject(nextRow.begin,nextCells);
        check(nextValid,"reopening shop reconstructs bounded native stock");if(!nextValid) return;
        check(potions::isLifePotion(nextCells[2].item,expectedLevel),"reopened DLL offer follows actual unlocked recipe");
        putBytes(tradeCells,nextCells,sizeof(nextCells));
        auto nextPrice=call(priceFunction,potion);dispatch(nextPrice);
        check(nextPrice.cancel && nextPrice.overrideReturn && nextPrice.returnI==expectedPrice,
              "reopened DLL computes accepted graded price");
    };
    put(player+0x190,17);
    price=call(priceFunction,potion);dispatch(price);
    check(price.cancel && price.returnI==15,"level-up cannot reprice the already opened level nine offer");
    reopen(9,15); // Level 17 without its new recipe still has the level-13 crafting ceiling.
    setRecipes(17,{17,1,9,5,13});reopen(13,16);
    setRecipes(21,{1,5,9,13,17,21});reopen(17,18);
    setRecipes(101,{101,97,1});reopen(97,33);
    setRecipes(9,{1,5,9});reopen(5,13);
    setRecipes(5,{1,5});reopen(1,10);
    setRecipes(4,{1});reopen(0,0);
    setRecipes(14,{1,5,9,13});reopen(9,15);
    put(recipeBook+0x18,5u);reopen(0,0); // Count exceeds the complete four-node list.
    setRecipes(14,{1,5,9,13});reopen(9,15);
    put(gc+0x8006d0,player+0x1000);
    priceExcluded(potion,"switching character invalidates old offer immediately");
    put(gc+0x8006d0,player);
    check(memory.at(sourceCells)==merchantBefore,"all progression reopens preserve native merchant stock");
    check(writes==0,"DLL performs no direct SDK memory writes");
}
}

int wmain(int argc,wchar_t** argv) {
    static_assert(sizeof(void*)==4,"module checks must be built Win32");
    if(argc!=2) {std::fprintf(stderr,"Usage: potion_module_checks <absolute potion_shop.dll path>\n");return 2;}
    fixture();
    HMODULE module=LoadLibraryW(argv[1]);
    if(!module) {std::printf("FAIL: LoadLibraryW (%lu)\n",GetLastError());return 2;}
    auto init=reinterpret_cast<CubeModInitFn>(GetProcAddress(module,"CubeMod_Init"));
    auto shutdown=reinterpret_cast<CubeModShutdownFn>(GetProcAddress(module,"CubeMod_Shutdown"));
    check(init && shutdown,"actual DLL exports init and shutdown");
    if(init && shutdown) {
        const CubeModInfo* info=init(&context.api);
        check(info!=nullptr,"actual DLL initializes against synthetic supported Alpha SDK");
        if(info) {
            context.capabilities=info->capabilities;
            std::printf("DLL version: %s; capabilities: %u\n",info->version,info->capabilities);
            constexpr unsigned required=CUBE_CAP_RAW_MEM|CUBE_CAP_RAW_HOOKS|CUBE_CAP_WRITES;
            check((info->capabilities&required)==required,"actual manifest declares RawMem, RawHooks and Writes");
            check(modloader::ownerMayWrite(&context.api),"actual loader policy permits hook modifications");
            check(hooks.size()==2,"actual DLL registers exactly two raw hooks");
            check(hooks.count(copyFunction) && hooks.at(copyFunction).cc==CUBE_CC_THISCALL &&
                  hooks.at(copyFunction).count==1,"stock-copy hook uses thiscall with one argument");
            check(hooks.count(priceFunction) && hooks.at(priceFunction).cc==CUBE_CC_THISCALL &&
                  hooks.at(priceFunction).count==0,"price hook uses thiscall with no arguments");
            if(hooks.count(copyFunction) && hooks.count(priceFunction)) exercise();
        }
        shutdown();check(hooks.empty() && removed==2,"shutdown removes both registered hooks");
    }
    FreeLibrary(module);
    std::printf("%u checks, %u failures (SDK simulation only; no game process accessed)\n",checks,failures);
    return failures?1:0;
}

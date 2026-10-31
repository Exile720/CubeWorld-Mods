#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include "sections_ui.hpp"
#include <map>
#include <filesystem>
#include <cstdio>
#include <vector>

using Microsoft::WRL::ComPtr;
static void require(HRESULT result) {if(FAILED(result))std::exit(3);}
// Software rendering of the real ImGui triangles and font atlas. No GPU/window/game access.
static std::vector<unsigned char> raster(ImDrawData* data,int width,int height,
                                        const unsigned char* atlas,int tw,int th) {
    std::vector<unsigned char> pixels(static_cast<size_t>(width)*height*4,255);
    for(int list=0;list<data->CmdListsCount;++list) {
        const auto* draw=data->CmdLists[list];
        for(const auto& cmd:draw->CmdBuffer) {
            if(cmd.UserCallback)continue;
            const int left=std::max(0,static_cast<int>(std::floor(cmd.ClipRect.x))),top=std::max(0,static_cast<int>(std::floor(cmd.ClipRect.y)));
            const int right=std::min(width,static_cast<int>(std::ceil(cmd.ClipRect.z))),bottom=std::min(height,static_cast<int>(std::ceil(cmd.ClipRect.w)));
            for(unsigned i=0;i<cmd.ElemCount;i+=3) {
                const auto& a=draw->VtxBuffer[cmd.VtxOffset+draw->IdxBuffer[cmd.IdxOffset+i]];
                const auto& b=draw->VtxBuffer[cmd.VtxOffset+draw->IdxBuffer[cmd.IdxOffset+i+1]];
                const auto& c=draw->VtxBuffer[cmd.VtxOffset+draw->IdxBuffer[cmd.IdxOffset+i+2]];
                const float denom=(b.pos.y-c.pos.y)*(a.pos.x-c.pos.x)+(c.pos.x-b.pos.x)*(a.pos.y-c.pos.y);
                if(std::abs(denom)<1e-8f)continue;
                const int x0=std::max(left,static_cast<int>(std::floor(std::min({a.pos.x,b.pos.x,c.pos.x}))));
                const int y0=std::max(top,static_cast<int>(std::floor(std::min({a.pos.y,b.pos.y,c.pos.y}))));
                const int x1=std::min(right,static_cast<int>(std::ceil(std::max({a.pos.x,b.pos.x,c.pos.x}))));
                const int y1=std::min(bottom,static_cast<int>(std::ceil(std::max({a.pos.y,b.pos.y,c.pos.y}))));
                for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x) {
                    const float px=x+.5f,py=y+.5f;
                    const float wa=((b.pos.y-c.pos.y)*(px-c.pos.x)+(c.pos.x-b.pos.x)*(py-c.pos.y))/denom;
                    const float wb=((c.pos.y-a.pos.y)*(px-c.pos.x)+(a.pos.x-c.pos.x)*(py-c.pos.y))/denom,wc=1-wa-wb;
                    if(wa<0||wb<0||wc<0)continue;
                    const float u=wa*a.uv.x+wb*b.uv.x+wc*c.uv.x,v=wa*a.uv.y+wb*b.uv.y+wc*c.uv.y;
                    const int tx=std::clamp(static_cast<int>(u*tw),0,tw-1),ty=std::clamp(static_cast<int>(v*th),0,th-1);
                    const auto* tex=atlas+(ty*tw+tx)*4;float rgba[4]{};
                    for(unsigned channel=0;channel<4;++channel)rgba[channel]=wa*((a.col>>(channel*8))&255)+wb*((b.col>>(channel*8))&255)+wc*((c.col>>(channel*8))&255);
                    const float alpha=rgba[3]*tex[3]/65025.0f;auto* dst=pixels.data()+(static_cast<size_t>(y)*width+x)*4;
                    for(int channel=0;channel<3;++channel)dst[2-channel]=static_cast<unsigned char>(std::clamp(rgba[channel]*tex[channel]/255*alpha+dst[2-channel]*(1-alpha),0.0f,255.0f));
                }
            }
        }
    }return pixels;
}
namespace {
std::map<unsigned,std::vector<unsigned char>> storage;
template<class T>void put(unsigned a,const T& v){auto& b=storage[a];b.resize(sizeof(v));std::memcpy(b.data(),&v,sizeof(v));}
bool read(unsigned a,void* out,unsigned n){for(const auto& e:storage)if(a>=e.first&&std::uint64_t(a)-e.first+n<=e.second.size()){std::memcpy(out,e.second.data()+a-e.first,n);return true;}return false;}
}
int wmain(int argc,wchar_t** argv) {
    if(argc<3)return 2;
    const int width=argc>3?_wtoi(argv[3]):880,height=argc>4?_wtoi(argv[4]):900;
    const int chest=argc>5?_wtoi(argv[5]):10,scroll=argc>6?_wtoi(argv[6]):0;
    const std::wstring popup=argc>7?argv[7]:L"none";
    const bool captured=popup==L"captured-local"||popup==L"captured-screen";
    if(width<640||height<480||chest<1||chest>30||scroll<0)return 2;
    inventoryui::Frame f;f.x=25;f.y=40;f.width=std::min(650.0f,width-80.0f);f.height=height-115.0f;f.cellW=f.cellH=75;f.firstRow=scroll;f.valid=true;
    if(captured){f.x=531;f.y=136;f.width=400;f.height=285;f.cellW=f.cellH=40;}
    if(!inventoryui::geometry(f))return 3;
    constexpr unsigned gc=0x10000000,player=0x20000000,widget=0x30000000,outer=0x40000000,items=0x50000000;
    put(gc+inventory::widgetOffset,widget);put(gc+inventory::playerOffset,player);put(player,inventory::creatureVtable);put(widget+inventory::controllerOffset,gc);put(widget+inventory::itemsOffset,player+inventory::inventoryOffset);put(widget+inventory::modeOffset,0);put(widget+inventory::pageOffset,0);
    std::vector<inventory::Cell> bag;
    const auto groups=captured?std::vector<std::pair<int,int>>{{2,23},{1,2},{14,2},{18,6}}:popup==L"detail"?std::vector<std::pair<int,int>>{{2,21},{1,4}}:std::vector<std::pair<int,int>>{{4,chest},{5,3},{6,4},{3,6}};
    for(auto group:groups)for(int i=0;i<group.second;++i){inventory::Cell c;c.count=1;c.item[0]=static_cast<unsigned char>(group.first);bag.push_back(c);}
    auto& bytes=storage[items];bytes.resize(bag.size()*sizeof(inventory::Cell));std::memcpy(bytes.data(),bag.data(),bytes.size());
    put(outer,inventory::Vector{items,items+static_cast<unsigned>(bytes.size()),items+static_cast<unsigned>(bytes.size())});
    std::array<unsigned char,inventory::prefixSize> prefix{};const inventory::Vector root{outer,outer+12,outer+12};std::memcpy(prefix.data(),&root,12);put(player+inventory::inventoryOffset,prefix);
    inventory::View view;if(!view.prepare(&read,gc,widget,inventory::creatureVtable,f.columns))return 4;f.sections=view.sections;
    std::vector<inventoryui::Rect> panels;
    if(captured) {
        // Native hover record captured 2026-10-25: local mesh bounds and parent matrix.
        const std::array<float,4> local{45.1754074f,-119.824219f,353.175873f,188.175797f};
        std::array<float,16> matrix{};matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;matrix[12]=793.824707f;matrix[13]=471.824280f;
        inventoryui::Rect screen{};if(!inventoryui::screenBounds(local,matrix,screen))return 7;
        panels.push_back({screen.x+3,screen.y+3,screen.w-6,screen.h-6});
        if(popup==L"captured-screen")f.occlusions.push_back(screen);
        else f.occlusions.push_back({local[0]-3,local[1]-3,local[2]-local[0]+6,local[3]-local[1]+6});
    }
    else if(popup==L"detail")panels.push_back({430,365,500,380});
    else if(popup==L"compare"){panels.push_back({430,245,430,395});panels.push_back({160,300,230,340});}
    else if(popup==L"edge")panels.push_back({25,95,450,610});
    else if(popup!=L"none")return 2;
    if(!captured)for(const auto& r:panels)f.occlusions.push_back({r.x-3,r.y-3,r.w+6,r.h+6});
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={static_cast<float>(width),static_cast<float>(height)};io.DeltaTime=1.0f/60;
    ImFontGlyphRangesBuilder glyphs;glyphs.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());glyphs.AddText("卷轴 / 配方");ImVector<ImWchar> ranges;glyphs.BuildRanges(&ranges);
    const auto font=std::filesystem::path(argv[2]).string();if(!io.Fonts->AddFontFromFileTTF(font.c_str(),20,nullptr,ranges.Data))return 5;
    unsigned char* atlas=nullptr;int tw=0,th=0;io.Fonts->GetTexDataAsRGBA32(&atlas,&tw,&th);io.Fonts->SetTexID(1);
    for(int pass=0;pass<2;++pass){
        ImGui::NewFrame();auto* d=ImGui::GetForegroundDrawList();d->Flags&=~ImDrawListFlags_AntiAliasedFill;
        d->AddRectFilled({0,0},io.DisplaySize,IM_COL32(36,46,24,255));
        navui::text(d,{35,20},"背包分区 · 布局预览",IM_COL32(246,228,176,255),20);
        d->PushClipRect({f.x+10,f.y+40},{f.x+10+f.columns*(f.cellW+5.0f)-5,f.y+40+f.visibleRows*(f.cellH+5.0f)-5},true);
        for(unsigned i=0;i<view.cells.size();++i)if(view.toNative[i]>=0&&view.cells[i].occupied()){
            const float x=f.x+10+(i%f.columns)*(f.cellW+5.0f),y=f.y+40+(static_cast<int>(i)/f.columns-f.firstRow)*(f.cellH+5.0f);
            d->AddRectFilled({x,y},{x+f.cellW,y+f.cellH},IM_COL32(62,75,39,255));d->AddRect({x,y},{x+f.cellW,y+f.cellH},IM_COL32(102,113,64,255));
            const int type=view.cells[i].item[0];const ImU32 color=type==4?IM_COL32(179,185,169,255):type==5?IM_COL32(147,132,89,255):type==6?IM_COL32(133,146,159,255):IM_COL32(205,181,105,255);
            const float icon=f.cellW/75.0f;
            if(type==3){d->AddRectFilled({x+30*icon,y+10*icon},{x+35*icon,y+45*icon},color);d->AddRectFilled({x+21*icon,y+40*icon},{x+44*icon,y+45*icon},color);}
            else {d->AddRectFilled({x+21*icon,y+14*icon},{x+44*icon,y+49*icon},color);if(type==4){d->AddRectFilled({x+11*icon,y+15*icon},{x+23*icon,y+29*icon},color);d->AddRectFilled({x+42*icon,y+15*icon},{x+54*icon,y+29*icon},color);}}
        }
        d->PopClipRect();
        // Match the actual draw order: native detail first, category overlay last.
        for(std::size_t i=0;i<panels.size();++i) {
            const auto& r=panels[i];
            d->AddRectFilled({r.x,r.y},{r.x+r.w,r.y+r.h},IM_COL32(3,4,3,255));
            d->AddRect({r.x,r.y},{r.x+r.w,r.y+r.h},IM_COL32(245,245,231,255),0.0f,3.0f);
            navui::text(d,{r.x+18,r.y+22},i?"当前装备 · 属性对比":"原版物品详情 · 示意",IM_COL32(132,211,111,255),19);
            navui::text(d,{r.x+18,r.y+60},"名称、属性与职业要求",IM_COL32(245,245,231,255),16);
            navui::text(d,{r.x+18,r.y+92},"详情文字保持完整可读",IM_COL32(245,245,231,255),16);
            navui::text(d,{r.x+18,r.y+124},"分类条在此区域停止绘制",IM_COL32(230,199,121,255),16);
        }
        if(!inventoryui::draw(d,f)&&panels.empty())return 6;
        navui::text(d,{35,height-55.0f},"分类与避让使用实际代码，详情及物品图标为示意；不是游戏截图。",navui::muted,14);
        ImGui::Render();
    }
    const auto pixels=raster(ImGui::GetDrawData(),width,height,atlas,tw,th);
    if(captured) {
        const auto pixel=(360*width+850)*4;
        const bool untouched=pixels[pixel]==3&&pixels[pixel+1]==4&&pixels[pixel+2]==3;
        if(untouched!=(popup==L"captured-screen"))return 8;
        std::puts("PASS recorded hover pixel: local-only mask reproduces overpaint, screen-space mask preserves the native detail");
    }
    require(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED));
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> image;
    require(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));require(factory->CreateStream(&stream));
    require(stream->InitializeFromFilename(argv[1],GENERIC_WRITE));require(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));
    require(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));require(encoder->CreateNewFrame(&image,nullptr));require(image->Initialize(nullptr));
    require(image->SetSize(width,height));WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;require(image->SetPixelFormat(&format));
    require(image->WritePixels(height,width*4,static_cast<UINT>(pixels.size()),const_cast<BYTE*>(pixels.data())));require(image->Commit());require(encoder->Commit());
    ImGui::DestroyContext();std::printf("PASS %dx%d software inventory-section layout preview; no game/GPU access\n",width,height);
}

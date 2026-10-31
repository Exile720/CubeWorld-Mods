#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "panel.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
using Microsoft::WRL::ComPtr;
static void require(HRESULT result) {if(FAILED(result))std::exit(3);}
// Software rendering of the real ImGui triangles and font atlas. No GPU/window/game access.
static std::vector<unsigned char> raster(ImDrawData* data,int width,int height) {
    std::vector<unsigned char> pixels(static_cast<size_t>(width)*height*4,255);
    for(int list=0;list<data->CmdListsCount;++list) {
        const auto* draw=data->CmdLists[list];
        for(const auto& cmd:draw->CmdBuffer) {
            if(cmd.UserCallback)continue;
            const auto* texture=cmd.TexRef._TexData;
            if(!texture||!texture->Pixels)std::exit(6);
            const auto* atlas=texture->Pixels;const int tw=texture->Width,th=texture->Height;
            const bool alphaOnly=texture->Format==ImTextureFormat_Alpha8;

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
                    const auto* tex=atlas+(ty*tw+tx)*(alphaOnly?1:4);float rgba[4]{};
                    for(unsigned channel=0;channel<4;++channel)rgba[channel]=wa*((a.col>>(channel*8))&255)+wb*((b.col>>(channel*8))&255)+wc*((c.col>>(channel*8))&255);
                    const float alpha=rgba[3]*tex[alphaOnly?0:3]/65025.0f;auto* dst=pixels.data()+(static_cast<size_t>(y)*width+x)*4;
                    for(int channel=0;channel<3;++channel)dst[2-channel]=static_cast<unsigned char>(std::clamp(rgba[channel]*(alphaOnly?255:tex[channel])/255*alpha+dst[2-channel]*(1-alpha),0.0f,255.0f));
                }
            }
        }
    }return pixels;
}

int wmain(int argc,wchar_t** argv){
    if(argc<2)return 2;
    const int width=argc>5?int(std::wcstol(argv[5],nullptr,10)):820,height=argc>6?int(std::wcstol(argv[6],nullptr,10)):720;
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={float(width),float(height)};io.DeltaTime=1.f/60;io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;
    ImFontConfig clearFont;clearFont.FontNo=0;clearFont.PixelSnapH=true;clearFont.RasterizerMultiply=1.f;
    if(!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc",20,&clearFont,io.Fonts->GetGlyphRangesChineseFull()))return 4;
    loot::State state;loot::Ticket ticket;ticket.cl=3;ticket.bossId=1234;ticket.bossLevel=40;ticket.rank=3;ticket.bossName="首领 · 瓦丽雅";loot::credit(state,ticket);lootui::View view{&state,"原版掉落已保留，额外开奖一次。",true,false,-10};
    if(argc>2&&std::wcscmp(argv[2],L"compensation")==0){auto& t=state.tickets.front();t.cl=4;t.bossLevel=45;t.rank=4;t.bossName="漏记补偿 · 示例地标";view.notice="这次补偿已保存，站稳后可以翻牌；结果不保证金色。";}
    if(argc>2&&std::wcscmp(argv[2],L"revealed")==0)loot::reveal(state);
    if(argc>2&&std::wcscmp(argv[2],L"pity")==0){state.misses=3;loot::reveal(state);}
    if(argc>2&&std::wcscmp(argv[2],L"locked")==0){loot::reveal(state);state.tickets.front().stage=loot::Stage::Claiming;view.notice="上次领奖中断，已锁定这张卡，防止重复发放。";}
    if(argc>2&&std::wcscmp(argv[2],L"empty")==0){state.tickets.clear();view.notice.clear();}
    if(argc>2&&std::wcscmp(argv[2],L"unavailable")==0){view.available=false;view.notice="请在单人世界站稳、关闭原生菜单并放下手中物品后操作。";}
    if(argc>2&&std::wcscmp(argv[2],L"corrupt")==0){state.tickets.clear();view.available=false;view.recordsValid=false;view.notice="抽奖记录损坏，已锁定；请保留数据与日志，停止领取。";}
    if(argc>2&&std::wcscmp(argv[2],L"longname")==0){state.tickets.front().bossName="地下墓穴 · 示例地标远征的古代守卫者";view.notice="请在单人世界站稳、关闭原生菜单并放下手中物品后操作。";view.available=false;}
    if(argc>7)ImGui::GetStyle().FontScaleMain=static_cast<float>(std::wcstod(argv[7],nullptr));
    const bool animation=argc>2&&std::wcscmp(argv[2],L"animation")==0;
    const double age=argc>3?std::wcstod(argv[3],nullptr):.8;
    if(animation){loot::reveal(state);state.tickets.front().reward.b[12]=static_cast<unsigned char>(argc>4?(std::min)(4ul,std::wcstoul(argv[4],nullptr,10)):3ul);view.notice="已保存这次结果，准备好后领取装备。";}
    // Match the installed dynamic-texture backend: each draw command can own a
    // different atlas while real-size fonts are baked. Never sample all commands
    // against the final atlas (that tears enlarged and DPI-scaled glyphs).
    for(int frame=0;frame<5;++frame){ImGui::NewFrame();if(animation)view.revealedAt=ImGui::GetTime()-age;lootui::draw(view);ImGui::Render();
        for(auto* texture:io.Fonts->TexList){
            if(texture->Status==ImTextureStatus_WantCreate||texture->Status==ImTextureStatus_WantUpdates){
                texture->SetTexID(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture)));texture->SetStatus(ImTextureStatus_OK);}}
    }
    const auto pixels=raster(ImGui::GetDrawData(),width,height);
    require(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED));
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> image;
    require(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));require(factory->CreateStream(&stream));
    require(stream->InitializeFromFilename(argv[1],GENERIC_WRITE));require(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));
    require(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));require(encoder->CreateNewFrame(&image,nullptr));require(image->Initialize(nullptr));
    require(image->SetSize(width,height));WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;require(image->SetPixelFormat(&format));
    require(image->WritePixels(height,width*4,static_cast<UINT>(pixels.size()),const_cast<BYTE*>(pixels.data())));require(image->Commit());require(encoder->Commit());
    ImGui::DestroyContext();std::printf("PASS %dx%d Boss Draw panel software preview\n",width,height);
}

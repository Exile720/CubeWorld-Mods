#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "nav_ui.hpp"
#include "explore_font.hpp"
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
    if(argc<3)return 2;const int width=1000,height=620;
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={float(width),float(height)};io.DeltaTime=1.f/60;io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;
    const auto path=std::filesystem::path(argv[2]).string();ImFontConfig original;original.PixelSnapH=true;original.OversampleH=1;original.OversampleV=1;
    auto* old=io.Fonts->AddFontFromFileTTF(path.c_str(),20,&original,io.Fonts->GetGlyphRangesChineseSimplifiedCommon());if(!old)return 4;
    exploreui::TextFont improved;auto* clear=improved.ensure(old);if(!clear)return 5;
    for(int frame=0;frame<5;++frame){ImGui::NewFrame();auto* d=ImGui::GetForegroundDrawList();
        d->AddRectFilled({20,20},{980,600},IM_COL32(24,44,53,255));d->AddLine({500,40},{500,580},IM_COL32(105,133,142,255));
        for(int side=0;side<2;++side){const float x=48.f+side*490;ImGui::PushFont(side?clear:old,20);
            d->AddText({x,48},IM_COL32(255,239,194,255),side?"轻量调整后":"原有绘制方式");
            const char* samples[]={"北  东北  东  东南  南","店铺指南  旅馆  道具店","地下墓穴 · 瑟莫  首领职业","城镇  挑战  地标  首领","打开地图标点  选择目的地"};
            const float sizes[]={14.f,18.5f,20.f,20.25f,20.f};
            for(int row=0;row<5;++row){const ImVec2 p{x,110.f+row*74};
                if(side)navui::text(d,p,samples[row],navui::cream,sizes[row]);
                else{float stroke=(std::max)(1.f,std::floor(sizes[row]/12));for(int y=-1;y<=1;++y)for(int xx=-1;xx<=1;++xx)if(xx||y)d->AddText(old,sizes[row],{p.x+xx*stroke,p.y+y*stroke},navui::ink,samples[row]);d->AddText(old,sizes[row],p,navui::cream,samples[row]);}}
            d->AddText({x,530},IM_COL32(193,210,210,255),"保留游戏原来的方正粗圆字形");ImGui::PopFont();}
        ImGui::Render();for(auto* texture:io.Fonts->TexList)if(texture->Status==ImTextureStatus_WantCreate||texture->Status==ImTextureStatus_WantUpdates){texture->SetTexID(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture)));texture->SetStatus(ImTextureStatus_OK);}}
    const auto pixels=raster(ImGui::GetDrawData(),width,height);
    require(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED));
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> image;
    require(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));require(factory->CreateStream(&stream));
    require(stream->InitializeFromFilename(argv[1],GENERIC_WRITE));require(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));
    require(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));require(encoder->CreateNewFrame(&image,nullptr));require(image->Initialize(nullptr));
    require(image->SetSize(width,height));WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;require(image->SetPixelFormat(&format));
    require(image->WritePixels(height,width*4,static_cast<UINT>(pixels.size()),const_cast<BYTE*>(pixels.data())));require(image->Commit());require(encoder->Commit());
    ImGui::DestroyContext();std::printf("PASS original-face comparison, no game accessed\n");
}

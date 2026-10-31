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

int wmain(int argc,wchar_t** argv){
    if(argc<2)return 2;
    const int width=760,height=480;
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={float(width),float(height)};io.DeltaTime=1.f/60;
    if(!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc",18,nullptr,io.Fonts->GetGlyphRangesChineseFull()))return 4;
    batchui::View view;view.price=2;view.coins=100;view.quantity.fit(50);
    if(argc>2&&std::wcscmp(argv[2],L"full")==0){view.owned=50;view.quantity.fit(0);}
    if(argc>2&&std::wcscmp(argv[2],L"life")==0){view.purchase=false;view.title="生命药水 · 等级 37";view.owned=15;view.quantity.fit(7);view.materials="材料：心之花 7 / 水瓶 10（每份各 1 个）";}
    unsigned char* atlas=nullptr;int tw=0,th=0;io.Fonts->GetTexDataAsRGBA32(&atlas,&tw,&th);
    for(int frame=0;frame<2;++frame){ImGui::NewFrame();batchui::draw(view);ImGui::Render();}
    io.Fonts->GetTexDataAsRGBA32(&atlas,&tw,&th);
    const auto pixels=raster(ImGui::GetDrawData(),width,height,atlas,tw,th);
    require(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED));
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> image;
    require(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));require(factory->CreateStream(&stream));
    require(stream->InitializeFromFilename(argv[1],GENERIC_WRITE));require(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));
    require(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));require(encoder->CreateNewFrame(&image,nullptr));require(image->Initialize(nullptr));
    require(image->SetSize(width,height));WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;require(image->SetPixelFormat(&format));
    require(image->WritePixels(height,width*4,static_cast<UINT>(pixels.size()),const_cast<BYTE*>(pixels.data())));require(image->Commit());require(encoder->Commit());
    ImGui::DestroyContext();std::printf("PASS %dx%d batch quantity panel software preview\n",width,height);
}

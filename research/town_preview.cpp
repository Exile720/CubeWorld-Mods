#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <wincodec.h>
#include <wrl/client.h>
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "town_ui.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

using Microsoft::WRL::ComPtr;
static void require(HRESULT hr,const char* action) {
    if(FAILED(hr)){std::fprintf(stderr,"%s: 0x%08lX\n",action,static_cast<unsigned long>(hr));std::exit(1);}
}
int wmain(int argc,wchar_t** argv) {
    if(argc<3)return 2;
    const int width=argc>3?_wtoi(argv[3]):1280,height=argc>4?_wtoi(argv[4]):800;
    if(width<640||width>2560||height<480||height>1600)return 3;
    SetProcessDPIAware();
    const auto instance=GetModuleHandleW(nullptr);
    WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=instance;cls.lpszClassName=L"TownLayoutPreview";
    if(!RegisterClassW(&cls))return 4;
    // This is an owned, hidden render surface. Never show/activate it or interact with another app.
    const HWND window=CreateWindowW(cls.lpszClassName,L"",WS_POPUP,0,0,width,height,nullptr,nullptr,instance,nullptr);
    if(!window)return 5;
    ComPtr<IDirect3D9> d3d;d3d.Attach(Direct3DCreate9(D3D_SDK_VERSION));if(!d3d)return 6;
    D3DPRESENT_PARAMETERS params{};params.Windowed=TRUE;params.SwapEffect=D3DSWAPEFFECT_DISCARD;
    params.hDeviceWindow=window;params.BackBufferWidth=width;params.BackBufferHeight=height;
    ComPtr<IDirect3DDevice9> device;
    require(d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&params,&device),"CreateDevice");
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.DisplaySize=ImVec2(static_cast<float>(width),static_cast<float>(height));io.DeltaTime=1.0f/60;
    const auto font=std::filesystem::path(argv[2]).string();
    if(!io.Fonts->AddFontFromFileTTF(font.c_str(),20,nullptr,io.Fonts->GetGlyphRangesChineseSimplifiedCommon()))return 7;
    if(!ImGui_ImplDX9_Init(device.Get()))return 8;
    const nav::Point player{8398000.125,8399000.25,80};
    std::vector<town::Place> places;
    if(argc<6) {
        const int types[]={33,34,35,36,37,38,39,40};
        const double offsets[][2]={{-205,-160},{110,-150},{-70,-190},{90,-65},{-210,-30},
            {-190,130},{-10,145},{150,130}};
        for(size_t i=0;i<8;++i)town::merge(places,{types[i],{player.x+offsets[i][0],player.y+offsets[i][1],80}});
    }
    nav::Projection projection;projection.width=width;projection.height=height;projection.origin=player;
    projection.matrix={1,0,0,0,0,.65,0,0,0,-.4,1,0,width*.43,height*.51,0,1};
    const float s=std::clamp(height/1080.0f,.70f,1.45f);
    const ImVec2 size(static_cast<float>(width),static_cast<float>(height));
    for(int frame=0;frame<2;++frame) {
        ImGui_ImplDX9_NewFrame();ImGui::NewFrame();
        auto* draw=ImGui::GetForegroundDrawList();draw->Flags&=~ImDrawListFlags_AntiAliasedFill;
        draw->AddRectFilled({0,0},size,IM_COL32(56,89,69,255));
        for(int x=0;x<width;x+=32)for(int y=0;y<height;y+=32)
            draw->AddRectFilled(ImVec2(static_cast<float>(x),static_cast<float>(y)),ImVec2(static_cast<float>(x+31),static_cast<float>(y+31)),((x/32+y/32)%3)?IM_COL32(71,111,76,255):IM_COL32(81,122,79,255));
        draw->AddRectFilled({0,size.y*.50f},{size.x,size.y*.50f+18},IM_COL32(170,155,108,255));
        draw->AddRectFilled({size.x*.42f,130},{size.x*.42f+18,size.y-110},IM_COL32(170,155,108,255));
        for(const auto& place:places) {
            nav::Screen p;nav::project(projection,place.position,p);
            const float x=static_cast<float>(p.x),y=static_cast<float>(p.y);
            if(town::isShop(place.type)) {
                draw->AddRectFilled({x-19,y-15},{x+23,y+19},IM_COL32(61,67,62,255));
                draw->AddRectFilled({x-23,y-21},{x+19,y+10},IM_COL32(183,124,76,255));
                draw->AddRectFilled({x-23,y-21},{x+19,y-13},IM_COL32(218,165,98,255));
            }
        }
        navui::text(draw,{16,18},"城镇店名排布预览（示意数据）",navui::cream,13*s);
        const auto target=places.empty()?player:places[5].position;
        navui::compass(draw,player,10,size.x,s,!places.empty(),target,places.empty()?"目的地":"铁匠铺");
        navui::mapHelp(draw,size,s,true,"滚轮缩放  拖动查看",false);
        nav::Screen marker;town::Rect arrowBounds{};
        if(!places.empty()) {
            if(!nav::project(projection,target,marker))return 12;
            const auto bounds=navui::mapMarkerBounds({static_cast<float>(marker.x),static_cast<float>(marker.y)},s);
            arrowBounds={bounds.x,bounds.y,bounds.z,bounds.w};
        }
        const nav::Screen testMouse{size.x-180*s,112*s+(places.empty()?15:46+62*5)*s};
        const auto hit=townui::draw(draw,size,s,projection,places,player,testMouse,true,places.empty()?nullptr:&arrowBounds);
        if(!places.empty()) {
            navui::mapMarker(draw,{static_cast<float>(marker.x),static_cast<float>(marker.y)},s);
        }
        if(!hit.panel||(!places.empty()&&(!hit.place||hit.target.type!=38))||(places.empty()&&hit.place)) {
            std::fprintf(stderr,"Facility sidebar hit test failed\n");return 10;
        }
        for(int type=33;type<=40;++type) {
            const auto guide=town::guide(type);
            if(navui::measure(guide.purpose,12.5f*s).x>304*s||navui::measure(guide.detail,12.5f*s).x>304*s) {
                std::fprintf(stderr,"Shop guide text exceeds sidebar width for %d\n",type);return 11;
            }
        }
        const int beforeCursor=draw->VtxBuffer.Size;
        if(navui::handCursor(draw,size,s,testMouse,false)||draw->VtxBuffer.Size!=beforeCursor)return 13;
        if(navui::handCursor(draw,size,s,{-1,20},true)||draw->VtxBuffer.Size!=beforeCursor)return 14;
        if(!hit.panel||!navui::handCursor(draw,size,s,testMouse,true)||draw->VtxBuffer.Size<=beforeCursor)return 15;
        ImGui::Render();
        require(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff385945,1,0),"Clear");
        require(device->BeginScene(),"BeginScene");
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());require(device->EndScene(),"EndScene");
    }
    ComPtr<IDirect3DSurface9> target,cpu;require(device->GetRenderTarget(0,&target),"GetRenderTarget");
    D3DSURFACE_DESC desc{};require(target->GetDesc(&desc),"GetDesc");
    require(device->CreateOffscreenPlainSurface(width,height,desc.Format,D3DPOOL_SYSTEMMEM,&cpu,nullptr),"CreateSurface");
    require(device->GetRenderTargetData(target.Get(),cpu.Get()),"Read render surface");
    D3DLOCKED_RECT locked{};require(cpu->LockRect(&locked,nullptr,D3DLOCK_READONLY),"LockRect");
    std::vector<BYTE> pixels(static_cast<size_t>(width)*height*4);
    for(int y=0;y<height;++y) {
        auto* row=pixels.data()+static_cast<size_t>(y)*width*4;
        std::memcpy(row,static_cast<BYTE*>(locked.pBits)+static_cast<size_t>(y)*locked.Pitch,width*4);
        for(int x=0;x<width;++x)row[x*4+3]=255;
    }
    cpu->UnlockRect();
    require(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"CoInitialize");
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> image;
    require(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"WIC factory");
    require(factory->CreateStream(&stream),"CreateStream");require(stream->InitializeFromFilename(argv[1],GENERIC_WRITE),"Output file");
    require(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"PNG encoder");require(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Encoder init");
    require(encoder->CreateNewFrame(&image,nullptr),"Create image");require(image->Initialize(nullptr),"Frame init");
    require(image->SetSize(width,height),"Image size");WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
    require(image->SetPixelFormat(&format),"Pixel format");if(format!=GUID_WICPixelFormat32bppBGRA)return 9;
    require(image->WritePixels(height,width*4,static_cast<UINT>(pixels.size()),pixels.data()),"WritePixels");
    require(image->Commit(),"Frame commit");require(encoder->Commit(),"PNG commit");
    ImGui_ImplDX9_Shutdown();ImGui::DestroyContext();DestroyWindow(window);UnregisterClassW(cls.lpszClassName,instance);
    std::printf("Rendered %dx%d native UI preview with %u facilities; no game process accessed.\n",width,height,static_cast<unsigned>(places.size()));
    return 0;
}

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include "explore_ui.hpp"
#include "explore_font.hpp"
#include "town_ui.hpp"
#include <filesystem>
#include <cstdio>
#include <vector>
#include <fstream>

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
int wmain(int argc,wchar_t** argv) {
    if(argc<3)return 2;
    const int width=argc>3?_wtoi(argv[3]):1280,height=argc>4?_wtoi(argv[4]):800;
    if(width<640||height<480||width>2560||height>1600)return 2;
    const bool empty=argc>5&&std::wcscmp(argv[5],L"empty")==0,standalone=argc>5&&std::wcscmp(argv[5],L"standalone")==0;
    const bool landmarks=argc>5&&std::wcscmp(argv[5],L"landmarks")==0,unknown=argc>5&&std::wcscmp(argv[5],L"unknown")==0;
    const bool completed=argc>5&&std::wcscmp(argv[5],L"completed")==0;
    const bool bosses=argc>5&&std::wcscmp(argv[5],L"bosses")==0;
    const bool retainedLast=argc>5&&std::wcscmp(argv[5],L"retained-last")==0;
    const bool retained=retainedLast||(argc>5&&std::wcscmp(argv[5],L"retained")==0);
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.DisplaySize={static_cast<float>(width),static_cast<float>(height)};io.DeltaTime=1.0f/60;
    const auto font=std::filesystem::path(argv[2]).string();
    if(!io.Fonts->AddFontFromFileTTF(font.c_str(),20,nullptr,io.Fonts->GetGlyphRangesChineseSimplifiedCommon()))return 4;
    auto* baseFont=io.Fonts->Fonts[0];
    exploreui::TextFont textFont;io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;
    auto* ownFont=textFont.ensure(baseFont);io.BackendFlags&=~ImGuiBackendFlags_RendererHasTextures;
    if(!ownFont||ownFont==baseFont||baseFont->Sources.Size!=1||ownFont->Sources.Size!=2)return 14;
    unsigned char* atlas=nullptr;int tw=0,th=0;io.Fonts->GetTexDataAsRGBA32(&atlas,&tw,&th);io.Fonts->SetTexID(1);
    bool missingGlyph=false;
    for(const wchar_t c:std::wstring(L"城镇山脉森林湖泊废墟遗迹墓地峡谷山谷陨石坑环形坑洞穴岩石树木山峰岛屿城堡地下墓穴宫殿神庙金字塔留存完成进度周边记录状态全部历史露芶龙首领职业已确认预测特殊未知战士游侠法师盗贼"))
        if(!ownFont->GetFontBaked(20)->FindGlyphNoFallback(c)){std::printf("Missing UI glyph U+%04X\n",static_cast<unsigned>(c));missingGlyph=true;}
    if(missingGlyph)return 13;
    if(argc>6) {
        std::ifstream input(std::filesystem::path(argv[6]),std::ios::binary);
        const std::string utf8((std::istreambuf_iterator<char>(input)),{});
        if(!input||utf8.empty()||utf8.size()>8192)return 16;
        const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),static_cast<int>(utf8.size()),nullptr,0);
        if(length<=0)return 16;
        std::wstring characters(length,L'\0');
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),static_cast<int>(utf8.size()),characters.data(),length);
        unsigned gaps=0;
        for(const auto c:characters) {
            if(!ownFont->GetFontBaked(20)->FindGlyphNoFallback(c))return 17;
            if(!baseFont->IsGlyphInFont(c))++gaps;
        }
        std::printf("PASS %d audited native-name characters, %u supplied by private fallback; shared font unchanged\n",length,gaps);
    }
    // Unlike the game's DX9 backend, this tiny rasterizer has no dynamic texture
    // upload callback. Refresh its pixels after loading the less common type glyphs.
    io.Fonts->GetTexDataAsRGBA32(&atlas,&tw,&th);io.Fonts->SetTexID(1);
    const ImVec2 size{static_cast<float>(width),static_cast<float>(height)};const float s=std::clamp(height/1080.0f,.70f,1.45f);
    const nav::Point player{8398000,8399000,80};explore::Snapshot survey;
    if(!empty)for(unsigned i=0;i<(retained?14u:15u);++i) {
        explore::Place p;p.id=i+1;p.category=i<3?1:3;p.mission=i<3?0:52;p.state=i==14?2:0;p.name=i<3?"Kurlon":"Ikorok";
        const unsigned levels[]={1,8,13,19,20,50};p.level=levels[i%6];
        if(landmarks||unknown){p.category=5;p.mission=0;p.name="远方废墟";}
        if(landmarks&&i==0){p.category=11;p.name="露芶龙";}
        if(landmarks&&i==1){p.category=12;p.name="泰勒荣";}
        if(landmarks&&i==2){p.category=8;p.name="泰勒莫";}
        if(completed)p.state=2;
        if(retained&&i>=3){p.category=14;p.subtype=(i-3)%6;p.name="泰勒莫";p.state=i==9?1:i<7?0:2;p.mission=i<9?52:0;}
        if(bosses&&i>=3) {
            p.category=14;p.subtype=(i-3)%6;p.name=i==3?"露芶龙":"泰勒莫";
            p.boss=i==3?explore::BossInfo{explore::BossSource::Confirmed,3}:
                i==4?explore::BossInfo{explore::BossSource::Predicted,1}:
                i==5?explore::BossInfo{explore::BossSource::Special,0}:explore::BossInfo{};
        }
        p.position={player.x+800+i*800,player.y+250+i*200,80};survey.places.push_back(p);
    }
    std::vector<town::Place> shops;for(int i=0;i<8;++i)shops.push_back({33+i,{player.x+(i%4)*80-100,player.y+(i/4)*120-30,80}});
    nav::Projection projection;projection.width=width;projection.height=height;projection.origin=player;
    projection.matrix={1,0,0,0,0,.65,0,0,0,-.4,1,0,width*.43,height*.51,0,1};
    size_t page=retainedLast?999:0;
    for(int frame=0;frame<2;++frame) {
        ImGui::NewFrame();auto* draw=ImGui::GetForegroundDrawList();draw->Flags&=~ImDrawListFlags_AntiAliasedFill;
        draw->AddRectFilled({0,0},size,IM_COL32(56,89,69,255));
        for(int x=0;x<width;x+=32)for(int y=0;y<height;y+=32)draw->AddRectFilled({static_cast<float>(x),static_cast<float>(y)},{static_cast<float>(x+31),static_cast<float>(y+31)},((x/32+y/32)%3)?IM_COL32(71,111,76,255):IM_COL32(81,122,79,255));
        if(standalone)exploreui::guidance(draw,size,s,player,0,survey.places[3].position,"挑战区域 Ikorok");
        else {
            navui::compass(draw,player,10,size.x,s,true,{player.x+5000,player.y+2000,80});navui::mapHelp(draw,size,s,true,"滚轮缩放  拖动查看",false);
            const int tab=empty?0:(landmarks||unknown)?2:1;
            const auto layout=exploreui::layout(size,s,explore::rows(survey.places,player,tab).size(),page,tab);
            if(retained&&(layout.pages!=2||(retainedLast&&layout.page!=1)))return 11;
            const town::Rect reserved{layout.panel.x,layout.panel.y,layout.panel.w,layout.panel.h};
            townui::draw(draw,size,s,projection,shops,player,{},false,nullptr,&reserved);
            const nav::Screen click{reserved.x+25*s,reserved.y+106*s};
            exploreui::Action action;
            {exploreui::ScopedTextFont scope(ownFont);action=exploreui::panel(draw,size,s,survey,player,tab,page,click,true,true,false,{},"",unknown?-1:13);}
            if(ImGui::GetFont()!=baseFont)return 15;
            if(!empty&&(!action.choose||action.place.city()||(!completed&&!retainedLast&&action.place.done())||((landmarks||unknown)?action.place.challenge():!action.place.challenge())))return 5;
            if(retainedLast&&(!action.place.done()||action.place.mission))return 12;
            if(empty&&action.choose)return 6;
            if(reserved.y+reserved.h>size.y-100*s)return 7;
            const exploreui::Rect panel{reserved.x,reserved.y,reserved.w,reserved.h};
            const int before=draw->VtxBuffer.Size;
            if(!exploreui::cursor(draw,size,s,panel,click,true)||draw->VtxBuffer.Size<=before)return 8;
            const int after=draw->VtxBuffer.Size;
            if(exploreui::cursor(draw,size,s,panel,click,false)||draw->VtxBuffer.Size!=after)return 9;
            if(exploreui::cursor(draw,size,s,panel,{size.x-1,1},true)||draw->VtxBuffer.Size!=after)return 10;
            navui::text(draw,{16,size.y-17*s},"独立界面预览（示意数据）",navui::muted,10*s);
        }
        ImGui::Render();
    }
    io.Fonts->GetTexDataAsRGBA32(&atlas,&tw,&th);
    const auto pixels=raster(ImGui::GetDrawData(),width,height,atlas,tw,th);
    require(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED));
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> image;
    require(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));require(factory->CreateStream(&stream));
    require(stream->InitializeFromFilename(argv[1],GENERIC_WRITE));require(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));
    require(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));require(encoder->CreateNewFrame(&image,nullptr));require(image->Initialize(nullptr));
    require(image->SetSize(width,height));WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;require(image->SetPixelFormat(&format));
    require(image->WritePixels(height,width*4,static_cast<UINT>(pixels.size()),const_cast<BYTE*>(pixels.data())));require(image->Commit());require(encoder->Commit());
    ImGui::DestroyContext();std::printf("PASS %dx%d software preview and panel hit/viewport checks; no game/GPU access\n",width,height);
}

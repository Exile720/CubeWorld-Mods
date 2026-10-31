#pragma once
#include "imgui.h"
#include <windows.h>
#include <cstdio>

namespace bossui {
// The local regular face stays separate from the loader and other Mods' fonts.
class TextFont {
    ImGuiContext* context_=nullptr;
    ImFont* font_=nullptr;
    bool attempted_=false;
public:
    ImFont* ensure(ImFont* base) {
        auto* context=ImGui::GetCurrentContext();
        if(context!=context_){context_=context;font_=nullptr;attempted_=false;}
        if(font_||attempted_)return font_;
        if(!context||!base)return nullptr;
        auto* atlas=base->OwnerAtlas;
        if(!atlas||atlas->Locked||!(ImGui::GetIO().BackendFlags&ImGuiBackendFlags_RendererHasTextures))return nullptr;
        attempted_=true;
        char directory[MAX_PATH]{},path[MAX_PATH]{};
        const auto length=GetWindowsDirectoryA(directory,MAX_PATH);
        if(!length||length>=MAX_PATH)return nullptr;
        const int count=std::snprintf(path,sizeof(path),"%s\\Fonts\\msyh.ttc",directory);
        if(count<=0||count>=static_cast<int>(sizeof(path))||GetFileAttributesA(path)==INVALID_FILE_ATTRIBUTES)return nullptr;
        ImFontConfig config;
        config.FontNo=0;config.PixelSnapH=true;config.RasterizerMultiply=1.f;
        config.Flags|=ImFontFlags_NoLoadError;
        std::snprintf(config.Name,sizeof(config.Name),"Boss draw YaHei Regular");
        font_=atlas->AddFontFromFileTTF(path,20.f,&config);
        return font_;
    }
};
struct ScopedTextFont {
    explicit ScopedTextFont(ImFont* font){ImGui::PushFont(font,20.f);}
    ~ScopedTextFont(){ImGui::PopFont();}
    ScopedTextFont(const ScopedTextFont&)=delete;
    ScopedTextFont& operator=(const ScopedTextFont&)=delete;
};
}

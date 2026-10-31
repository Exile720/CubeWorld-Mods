#pragma once
#include "imgui.h"
#include <windows.h>
#include <cstdio>

namespace exploreui {
// A separate font owned by the existing atlas. It borrows the loader font's TTF
// bytes, keeps those glyphs first, and uses the local system font for gaps only.
// Never merges into the default font or changes another Mod's font selection.
class TextFont {
    ImGuiContext* context_=nullptr;
    ImFont* font_=nullptr;
    bool attempted_=false;
public:
    bool attempted() const {return attempted_;}
    ImFont* ensure(ImFont* base) {
        auto* context=ImGui::GetCurrentContext();
        if(context!=context_){context_=context;font_=nullptr;attempted_=false;}
        if(font_||attempted_)return font_;
        if(!context||!base||base->Sources.empty())return nullptr;
        auto* atlas=base->OwnerAtlas;
        // The installed DX9 backend supports dynamic atlas updates. Older/static
        // backends must not be forced to modify an atlas locked during a frame.
        if(!atlas||atlas->Locked||!(ImGui::GetIO().BackendFlags&ImGuiBackendFlags_RendererHasTextures))return nullptr;
        attempted_=true;
        // Adding a source can relocate atlas->Sources: retain values, never a
        // pointer to its vector entry across AddFont calls.
        const auto source=*base->Sources[0];
        if(!source.FontData||source.FontDataSize<=0)return nullptr;
        char directory[MAX_PATH]{},path[MAX_PATH]{};
        const auto length=GetWindowsDirectoryA(directory,MAX_PATH);
        if(!length||length>=MAX_PATH)return nullptr;
        const int count=std::snprintf(path,sizeof(path),"%s\\Fonts\\msyh.ttc",directory);
        if(count<=0||count>=static_cast<int>(sizeof(path))||GetFileAttributesA(path)==INVALID_FILE_ATTRIBUTES)return nullptr;
        ImFontConfig native;
        native.FontDataOwnedByAtlas=false;native.PixelSnapH=true;
        native.OversampleH=2;native.OversampleV=1;
        native.FontNo=source.FontNo;native.GlyphOffset=source.GlyphOffset;
        native.RasterizerMultiply=1.f;
        std::snprintf(native.Name,sizeof(native.Name),"Explorer native + missing glyphs");
        auto* own=atlas->AddFontFromMemoryTTF(source.FontData,source.FontDataSize,
                                             base->LegacySize>0?base->LegacySize:20,&native,source.GlyphRanges);
        if(!own)return nullptr;
        ImFontConfig fallback;
        fallback.MergeMode=true;fallback.DstFont=own;fallback.PixelSnapH=true;
        fallback.OversampleH=2;fallback.OversampleV=1;fallback.RasterizerMultiply=1.f;
        fallback.Flags|=ImFontFlags_NoLoadError;
        if(!atlas->AddFontFromFileTTF(path,base->LegacySize>0?base->LegacySize:20,&fallback,source.GlyphRanges)) {
            atlas->RemoveFont(own);return nullptr;
        }
        font_=own;return font_;
    }
};
struct ScopedTextFont {
    bool pushed;
    explicit ScopedTextFont(ImFont* font):pushed(font!=nullptr){if(pushed)ImGui::PushFont(font);}
    ~ScopedTextFont(){if(pushed)ImGui::PopFont();}
    ScopedTextFont(const ScopedTextFont&)=delete;
    ScopedTextFont& operator=(const ScopedTextFont&)=delete;
};
}

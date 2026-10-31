#pragma once
#include "batch.hpp"
#include "imgui.h"
#include <string>

namespace batchui {
struct View {
    bool purchase=true;
    std::string title="空瓶",materials,notice;
    batch::Quantity quantity;
    int owned=0,price=0,coins=0;
    bool working=false;
};
struct Action {bool confirm=false,close=false;};
inline Action draw(View& v) {
    Action action;const auto display=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({display.x*.5f,display.y*.5f},ImGuiCond_Appearing,{.5f,.5f});
    ImGui::SetNextWindowSize({(std::min)(540.f,display.x-24),(std::min)(340.f,display.y-24)},ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0);ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,{.07f,.13f,.17f,1});
    ImGui::PushStyleColor(ImGuiCol_Button,{.19f,.30f,.34f,1});
    bool open=true;
    if(ImGui::Begin(v.purchase?"选择购买数量###batch_supply":"选择制作数量###batch_supply",&open,
        ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextUnformatted(v.title.c_str());
        ImGui::Text("已有 %d / 50     本次最多 %d 个",v.owned,v.quantity.maximum);
        ImGui::Separator();ImGui::Spacing();
        ImGui::BeginDisabled(v.working||!v.quantity.maximum);
        const bool keys=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)&&!ImGui::GetIO().KeyCtrl&&!ImGui::GetIO().KeyAlt;
        bool less=ImGui::Button("A  -10",{120,44});ImGui::SameLine();
        ImGui::BeginChild("batch_number",{210,44});ImGui::SetCursorPos({86,10});ImGui::Text("%d",v.quantity.value);ImGui::EndChild();ImGui::SameLine();
        bool more=ImGui::Button("D  +10",{120,44});
        if(keys){less|=ImGui::IsKeyPressed(ImGuiKey_A);more|=ImGui::IsKeyPressed(ImGuiKey_D);}
        if(less)v.quantity.step(-1);if(more)v.quantity.step(1);
        ImGui::EndDisabled();ImGui::Spacing();
        if(v.purchase)ImGui::Text("每个 %d 铜钱   合计 %lld 铜钱   持有 %d 铜钱",v.price,1LL*v.price*v.quantity.value,v.coins);
        else ImGui::TextUnformatted(v.materials.c_str());
        if(!v.quantity.maximum)ImGui::TextWrapped(v.owned>=50?"已经达到原版 50 个携带上限，请先使用或制作消耗。":
            (v.purchase?"铜钱不足，暂时无法购买。":"所需材料不足，暂时无法制作。"));
        else ImGui::TextDisabled("A / D 每次减 / 加 10；到数量边界时自动停止。");
        if(!v.notice.empty())ImGui::TextWrapped("%s",v.notice.c_str());
        ImGui::SetCursorPosY((std::max)(ImGui::GetCursorPosY(),ImGui::GetWindowHeight()-58));
        ImGui::BeginDisabled(v.working||v.quantity.value<=0);
        if(ImGui::Button(v.working?"正在处理…":(v.purchase?"确认购买  [Enter]":"确认制作  [Enter]"),{300,36}))action.confirm=true;
        if(!v.working&&v.quantity.value>0&&keys&&ImGui::IsKeyPressed(ImGuiKey_Enter,false))action.confirm=true;
        ImGui::EndDisabled();ImGui::SameLine();
        if(ImGui::Button("关闭  [Esc]",{150,36})||(keys&&ImGui::IsKeyPressed(ImGuiKey_Escape,false)))action.close=true;
    }
    ImGui::End();ImGui::PopStyleColor(2);ImGui::PopStyleVar(2);
    action.close|=!open;return action;
}
}

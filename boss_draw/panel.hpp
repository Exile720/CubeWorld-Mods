#pragma once
#include "loot.hpp"
#include "imgui.h"
#include <cstdio>

namespace lootui {
inline ImVec4 color(unsigned q){constexpr float v[5][3]={{.8f,.84f,.87f},{.4f,.85f,.48f},{.35f,.66f,1.f},{.78f,.47f,1.f},{1.f,.78f,.3f}};q=(std::min)(q,4u);return {v[q][0],v[q][1],v[q][2],1};}
constexpr double revealDuration=1.1;
namespace detail {
constexpr ImU32 text=IM_COL32(238,230,211,255),muted=IM_COL32(184,201,200,255);
constexpr ImU32 edge=IM_COL32(52,81,90,255),gold=IM_COL32(200,173,112,255);
inline ImVec2 pixel(ImVec2 p){return {std::round(p.x),std::round(p.y)};}
inline void label(ImDrawList* d,ImVec2 p,ImU32 col,const char* value,float wrap=0){
    d->AddText(ImGui::GetFont(),ImGui::GetFontSize(),pixel(p),col,value,nullptr,wrap);
}
inline float height(const char* value,float width){return ImGui::CalcTextSize(value,nullptr,false,(std::max)(1.f,width)).y;}
inline void centred(ImDrawList* d,float x,float y,ImU32 col,const char* value){
    label(d,{x-ImGui::CalcTextSize(value).x*.5f,y},col,value);
}
// The perspective denominator stays greater than the card width, including edge-on frames.
inline void card(ImDrawList* d,ImVec2 centre,float halfHeight,double elapsed,bool sealed,const ImVec4& tint){
    const float halfWidth=halfHeight*.69f;
    const float t=float((std::max)(0.0,elapsed));
    const float angle=sealed?0.f:(t<revealDuration?(std::clamp)((t-.18f)/.66f,0.f,1.f)*3.14159265f:
        3.14159265f+std::fmod(t-float(revealDuration),8.f)*.78539816f);
    const float cs=std::cos(angle),sn=std::sin(angle),focal=(std::max)(280.f,halfWidth*4.f);
    const bool known=!sealed&&t>=.51f;
    auto ink=[&](float alpha){return ImGui::ColorConvertFloat4ToU32({tint.x,tint.y,tint.z,alpha});};
    auto point=[&](float x,float y){const float perspective=focal/(focal+x*sn);return pixel({centre.x+x*cs*perspective,centre.y+y*perspective});};
    ImVec2 corners[]{point(-halfWidth,-halfHeight),point(halfWidth,-halfHeight),point(halfWidth,halfHeight),point(-halfWidth,halfHeight)};
    if(cs<0)std::reverse(std::begin(corners),std::end(corners));
    if(std::abs(cs)>.015f){
        const float shade=known?(cs<0?.79f:.64f):.35f;
        d->AddConvexPolyFilled(corners,4,ImGui::ColorConvertFloat4ToU32({tint.x*shade,tint.y*shade,tint.z*shade,1}));
        for(int i=0;i<4;++i)d->AddLine(corners[i],corners[(i+1)%4],ink(1),2);
        const float inset=(std::min)(7.f,halfHeight*.14f);
        ImVec2 inner[]{point(-halfWidth+inset,-halfHeight+inset),point(halfWidth-inset,-halfHeight+inset),point(halfWidth-inset,halfHeight-inset),point(-halfWidth+inset,halfHeight-inset)};
        for(int i=0;i<4;++i)d->AddLine(inner[i],inner[(i+1)%4],IM_COL32(238,230,211,110));
        const float rune=(std::min)(20.f,halfHeight*.35f);
        ImVec2 mark[]{point(0,-rune),point(rune*.78f,0),point(0,rune),point(-rune*.78f,0)};
        for(int i=0;i<4;++i)d->AddLine(mark[i],mark[(i+1)%4],IM_COL32(245,239,220,215),1.5f);
        d->AddLine(point(-rune*.32f,0),point(rune*.32f,0),IM_COL32(245,239,220,215),1.5f);
    }else d->AddLine(point(0,-halfHeight),point(0,halfHeight),ink(1),2);
}
inline const char* source(const loot::Ticket& t,bool compact=false){
    if(compact){
        if(t.pity)return "来源：本职业池 · 职业保底";
        if(t.bossName.rfind("漏记补偿 · ",0)==0)return "来源：漏记补偿池";
        if(t.source==loot::Source::Boss)return "来源：首领实际装备";
        return t.source==loot::Source::OwnClass?"来源：本职业池":"来源：其他职业 / 首饰";
    }
    if(t.pity)return "来源：本职业随机池 · 本次触发职业保底";
    if(t.bossName.rfind("漏记补偿 · ",0)==0)return "来源：漏记补偿随机池";
    if(t.source==loot::Source::Boss)return "来源：首领实际装备";
    return t.source==loot::Source::OwnClass?"来源：本职业随机池":"来源：其他职业或首饰池";
}
inline const char* pool(const loot::Ticket& t,unsigned misses){
    if(misses>=3)return "职业保底已就绪，本次必出当前职业专属装备。";
    if(t.bossName.rfind("漏记补偿 · ",0)==0)return "原装备记录缺失：本职业约 66.7%，其他与首饰约 33.3%。";
    if(t.pool.empty())return "无穿戴候选：本职业约 66.7%，其他与首饰约 33.3%。";
    return "本职业 40%   首领实际装备 40%   其他与首饰 20%";
}
inline void stage(const loot::Ticket* ticket,unsigned misses,double elapsed,ImVec2 p,ImVec2 size,bool recordsValid=true){
    auto* d=ImGui::GetWindowDrawList();
    d->AddRectFilled(pixel(p),pixel({p.x+size.x,p.y+size.y}),IM_COL32(23,47,57,255));
    d->AddRect(pixel(p),pixel({p.x+size.x,p.y+size.y}),edge);
    d->AddLine(pixel({p.x+1,p.y+1}),pixel({p.x+size.x-1,p.y+1}),IM_COL32(68,98,105,255));
    d->PushClipRect(pixel({p.x+1,p.y+1}),pixel({p.x+size.x-1,p.y+size.y-1}),true);
    const float line=ImGui::GetFontSize(),cx=p.x+size.x*.5f,innerWidth=(std::max)(1.f,size.x-24.f);
    const bool sealed=!ticket||ticket->stage==loot::Stage::Sealed,known=!sealed&&elapsed>=.51;
    const auto tint=known?color(ticket->reward.b[12]):ImVec4{.48f,.70f,.72f,1};
    const ImU32 quality=ImGui::ColorConvertFloat4ToU32(tint);
    if(!recordsValid){
        const char* message="请保留现有记录，核对后再继续。";
        const float messageHeight=height(message,innerWidth),blockHeight=line+8+messageHeight;
        const float y=p.y+(std::max)(4.f,(size.y-blockHeight)*.5f);
        centred(d,cx,y,IM_COL32(255,227,200,255),"记录暂不可用");
        label(d,{p.x+12,y+line+8},text,message,innerWidth);
    }else if(!ticket){
        const char* message="参与击败首领后，这里会留下抽奖机会。";
        const float messageHeight=height(message,innerWidth),headingY=p.y+12;
        centred(d,cx,headingY,text,"暂时没有待领取的装备");
        const float bodyTop=headingY+line+9,bodyBottom=p.y+size.y-messageHeight-12;
        card(d,{cx,(bodyTop+bodyBottom)*.5f},(std::max)(4.f,(std::min)(44.f,(bodyBottom-bodyTop)*.43f)),0,true,tint);
        label(d,{p.x+12,p.y+size.y-messageHeight-9},muted,message,innerWidth);
    }else if(sealed){
        const char* info=pool(*ticket,misses);const float infoHeight=height(info,innerWidth);
        centred(d,cx,p.y+10,text,"待翻开的装备");
        const float bodyTop=p.y+line+18,bodyBottom=p.y+size.y-infoHeight-13;
        const float halfHeight=(std::max)(4.f,(std::min)(67.f,(bodyBottom-bodyTop)*.43f));
        const float baseY=(bodyTop+bodyBottom)*.5f+halfHeight+5;
        d->AddRectFilled(pixel({cx-halfHeight*.9f,baseY}),pixel({cx+halfHeight*.9f,baseY+4}),IM_COL32(42,68,76,255));
        card(d,{cx,(bodyTop+bodyBottom)*.5f},halfHeight,0,true,tint);
        label(d,{p.x+12,p.y+size.y-infoHeight-9},misses>=3?gold:muted,info,innerWidth);
    }else{
        const auto name=known?loot::name(ticket->reward):std::string("正在翻开装备卡");
        const bool compact=size.y<line*6.f+42.f;
        const float titleY=p.y+(compact?4.f:9.f);
        if(size.y>=205.f){ImGui::PushFont(nullptr,24.f);centred(d,cx,titleY,known?quality:text,name.c_str());ImGui::PopFont();}
        else centred(d,cx,titleY,known?quality:text,name.c_str());
        const char* from=known?source(*ticket,compact):(compact?"翻开后可领取":"结果揭晓后可领取，关闭窗口也会保留。");
        const float sourceHeight=height(from,innerWidth),bodyTop=titleY+(size.y>=205.f?ImGui::GetFontSize()*1.2f:line)+(compact?3.f:9.f);
        const float bodyBottom=p.y+size.y-sourceHeight-(compact?6.f:12.f),bodyHeight=(std::max)(1.f,bodyBottom-bodyTop);
        const float halfHeight=(std::max)(4.f,(std::min)(86.f,bodyHeight*.43f));
        const float cy=(bodyTop+bodyBottom)*.5f;
        const float baseY=cy+halfHeight+5;
        d->AddRectFilled(pixel({cx-halfHeight*.9f,baseY}),pixel({cx+halfHeight*.9f,baseY+4}),IM_COL32(42,68,76,255));
        card(d,{cx,cy},halfHeight,elapsed,false,tint);
        if(known){
            char value[32];const float left=p.x+16,right=p.x+size.x-(std::min)(144.f,size.x*.31f);
            if(compact){
                const float y=(std::max)(bodyTop,cy-line);
                std::snprintf(value,sizeof(value),"等级 %d",loot::level(ticket->reward));label(d,{left,y},text,value);
                std::snprintf(value,sizeof(value),"Power %d",loot::power(ticket->reward));label(d,{left,y+line},text,value);
                label(d,{right,y},text,loot::className(loot::required(ticket->reward)));
                label(d,{right,y+line},quality,loot::grade(ticket->reward.b[12]));
            }else{
            const float rowGap=(std::max)(2.f,(std::min)(16.f,(bodyHeight-line*4.f)*.5f));
            const float y=(std::max)(bodyTop,cy-(line*4.f+rowGap)*.5f);
            label(d,{left,y},muted,"装备等级");std::snprintf(value,sizeof(value),"%d",loot::level(ticket->reward));label(d,{left,y+line},text,value);
            label(d,{left,y+line*2+rowGap},muted,"Power");std::snprintf(value,sizeof(value),"%d",loot::power(ticket->reward));label(d,{left,y+line*3+rowGap},text,value);
            label(d,{right,y},muted,"适用职业");label(d,{right,y+line},text,loot::className(loot::required(ticket->reward)));
            label(d,{right,y+line*2+rowGap},muted,"装备品质");label(d,{right,y+line*3+rowGap},quality,loot::grade(ticket->reward.b[12]));
            }
        }
        label(d,{p.x+12,p.y+size.y-sourceHeight-(compact?3.f:8.f)},known&&ticket->pity?gold:muted,from,innerWidth);
    }
    d->PopClipRect();
}
}
// Only visible-window geometry; the reward data is never changed by this scene.
inline void flipCard(const loot::Ticket& ticket,double elapsed){
    const auto p=ImGui::GetCursorScreenPos();const ImVec2 size{ImGui::GetContentRegionAvail().x,246};
    detail::stage(&ticket,0,elapsed,p,size);ImGui::Dummy(size);
}
struct View {const loot::State* state=nullptr;std::string notice;bool available=false,busy=false;double revealedAt=-10;bool recordsValid=true;};
struct Action {bool reveal=false,claim=false,close=false;};
inline Action draw(const View& v){
    Action a;const auto screen=ImGui::GetIO().DisplaySize;const float sizeScale=ImGui::GetFontSize()/20.f;
    ImGui::SetNextWindowPos({screen.x*.5f,screen.y*.5f},ImGuiCond_Appearing,{.5f,.5f});
    ImGui::SetNextWindowSize({(std::max)(1.f,(std::min)(700.f*sizeScale,screen.x-24)),(std::max)(1.f,(std::min)(620.f*sizeScale,screen.y-24))},ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0);ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,0);ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,1);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{16,16});ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{10,8});
    ImGui::PushStyleColor(ImGuiCol_WindowBg,{17.f/255,35.f/255,43.f/255,1});
    ImGui::PushStyleColor(ImGuiCol_Text,{238.f/255,230.f/255,211.f/255,1});
    ImGui::PushStyleColor(ImGuiCol_Border,{52.f/255,81.f/255,90.f/255,1});
    if(ImGui::Begin("首领抽奖###boss_draw",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse)){
        const auto* s=v.state;const loot::Ticket* t=v.recordsValid&&s&&!s->tickets.empty()?&s->tickets.front():nullptr;
        const double elapsed=ImGui::GetTime()-v.revealedAt;
        const bool claiming=t&&t->stage==loot::Stage::Claiming;
        auto* d=ImGui::GetWindowDrawList();const auto w=ImGui::GetWindowPos(),z=ImGui::GetWindowSize();
        const float pad=16,line=ImGui::GetFontSize(),left=w.x+pad,right=w.x+z.x-pad,width=right-left;
        d->AddRect(detail::pixel({w.x+1,w.y+1}),detail::pixel({w.x+z.x-1,w.y+z.y-1}),detail::edge,0.f,2.f);
        d->AddRect(detail::pixel({w.x+4,w.y+4}),detail::pixel({w.x+z.x-4,w.y+z.y-4}),IM_COL32(29,57,66,255));
        const float headerY=w.y+pad,headerHeight=(std::max)(38.f,line+10.f);
        d->AddRectFilled(detail::pixel({left+1,headerY+8}),detail::pixel({left+17,headerY+24}),detail::gold);
        d->AddRectFilled(detail::pixel({left+6,headerY+3}),detail::pixel({left+22,headerY+19}),IM_COL32(103,124,116,255));
        d->AddRect(detail::pixel({left+6,headerY+3}),detail::pixel({left+22,headerY+19}),detail::gold);
        ImGui::PushFont(nullptr,26.f);detail::label(d,{left+36,headerY+1},detail::text,"首领抽奖");ImGui::PopFont();
        detail::label(d,{right-79,headerY+6},detail::muted,"F9");
        ImGui::SetCursorScreenPos(detail::pixel({right-30,headerY}));
        if(ImGui::InvisibleButton("##boss_draw_close",{30,30}))a.close=true;
        const auto closeInk=ImGui::IsItemHovered()?detail::text:detail::muted;
        if(ImGui::IsItemHovered())d->AddRectFilled(detail::pixel({right-30,headerY}),detail::pixel({right,headerY+30}),IM_COL32(49,76,82,255));
        d->AddLine(detail::pixel({right-21,headerY+9}),detail::pixel({right-9,headerY+21}),closeInk,1.5f);
        d->AddLine(detail::pixel({right-21,headerY+21}),detail::pixel({right-9,headerY+9}),closeInk,1.5f);
        d->AddLine(detail::pixel({left,headerY+headerHeight}),detail::pixel({right,headerY+headerHeight}),detail::edge);
        const float summaryY=headerY+headerHeight+8,summaryHeight=line+12;
        char count[48];if(v.recordsValid)std::snprintf(count,sizeof(count),"待领取 %u 次",s?unsigned(s->tickets.size()):0);else std::snprintf(count,sizeof(count),"记录暂不可用");detail::label(d,{left,summaryY+2},detail::text,count);
        const unsigned misses=v.recordsValid&&s?s->misses:0;char pity[48];if(v.recordsValid)std::snprintf(pity,sizeof(pity),"职业保底 %u/3",misses);else std::snprintf(pity,sizeof(pity),"职业保底未知");
        const float cell=12,cellGap=5,cellsWidth=cell*3+cellGap*2;
        const float pityX=right-cellsWidth-12-ImGui::CalcTextSize(pity).x;
        detail::label(d,{pityX,summaryY+2},detail::muted,pity);
        for(unsigned n=0;n<3;++n){const float x=right-cellsWidth+float(n)*(cell+cellGap),y=summaryY+line*.5f-3;
            d->AddRectFilled(detail::pixel({x,y}),detail::pixel({x+cell,y+cell}),n<misses?detail::gold:IM_COL32(31,58,66,255));
            d->AddRect(detail::pixel({x,y}),detail::pixel({x+cell,y+cell}),n<misses?detail::gold:detail::edge);}
        std::string origin;
        if(!v.recordsValid)origin="抽奖记录未能读取，开奖与领取已停用。";
        else if(t){
            char info[100];const bool compensation=t->bossName.rfind("漏记补偿 · ",0)==0;
            if(compensation)std::snprintf(info,sizeof(info),"   参考等级 %d · 五种品质随机",t->bossLevel);
            else std::snprintf(info,sizeof(info),"   首领等级 %d · 阶级 +%u",t->bossLevel,t->rank);
            origin="来自："+(t->bossName.empty()?std::string("首领"):t->bossName)+info;
        }else origin="原版掉落照常获得；额外奖励每次领取一件。";
        const float originY=summaryY+summaryHeight+4,originHeight=detail::height(origin.c_str(),width);
        detail::label(d,{left,originY},detail::muted,origin.c_str(),width);
        const bool alert=!v.recordsValid||!v.available||v.busy||claiming;
        std::string message=v.notice;
        if(claiming){message="领奖状态已锁定，避免重复发放。";if(!v.notice.empty())message+=" "+v.notice;}
        else if(message.empty()){
            if(!v.recordsValid)message="记录暂不可用，开奖与领取已停用。";
            else if(!v.available)message="当前暂不可领取，请在单人世界站稳并关闭其他菜单。";
            else if(v.busy)message="正在处理，请稍候。";
            else if(!t)message="击败首领后，按 F9 查看奖励。";
            else if(t->stage==loot::Stage::Sealed)message="翻开后保存结果，再由你领取。";
            else message=elapsed<revealDuration?"正在翻开装备卡…":"结果已保存，可以领取这件装备。";
        }
        const bool compactWindow=z.y<560.f*sizeScale;
        const char* caution=compactWindow?"保底只保职业，不保品质或提升。":"保底只保证本职业，不保证品质、部位或属性提升。";
        const float buttonHeight=(std::max)(44.f,line+18),footerY=w.y+z.y-pad-buttonHeight;
        const float cautionHeight=detail::height(caution,width),cautionY=footerY-8-cautionHeight;
        const float statusHeight=detail::height(message.c_str(),(std::max)(1.f,width-20))+12,statusY=cautionY-8-statusHeight;
        const float stageY=originY+originHeight+10,stageHeight=(std::max)(1.f,statusY-stageY-10);
        detail::stage(t,misses,claiming?revealDuration:elapsed,{left,stageY},{width,stageHeight},v.recordsValid);
        d->AddRectFilled(detail::pixel({left,statusY}),detail::pixel({right,statusY+statusHeight}),alert?IM_COL32(70,43,35,255):IM_COL32(25,49,54,255));
        d->AddRectFilled(detail::pixel({left,statusY}),detail::pixel({left+3,statusY+statusHeight}),alert?IM_COL32(218,154,104,255):IM_COL32(91,139,126,255));
        detail::label(d,{left+10,statusY+6},alert?IM_COL32(255,227,200,255):detail::text,message.c_str(),width-20);
        detail::label(d,{left,cautionY},detail::muted,caution,width);
        const float closeWidth=(std::max)(92.f,line*3.8f),gap=12,mainWidth=(std::max)(1.f,width-closeWidth-gap);
        ImGui::SetCursorScreenPos(detail::pixel({left,footerY}));
        ImGui::PushStyleColor(ImGuiCol_Button,{.22f,.44f,.38f,1});ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{.29f,.54f,.46f,1});ImGui::PushStyleColor(ImGuiCol_ButtonActive,{.18f,.37f,.32f,1});
        ImGui::BeginDisabled(!v.recordsValid||!v.available||v.busy||!t||t->stage==loot::Stage::Claiming||(t->stage==loot::Stage::Revealed&&elapsed<revealDuration));
        if(ImGui::Button(t&&t->stage==loot::Stage::Sealed?"翻开装备卡":"领取这件装备",{mainWidth,buttonHeight})){if(t&&t->stage==loot::Stage::Sealed)a.reveal=true;else a.claim=true;}
        ImGui::EndDisabled();ImGui::PopStyleColor(3);
        ImGui::SetCursorScreenPos(detail::pixel({left+mainWidth+gap,footerY}));
        ImGui::PushStyleColor(ImGuiCol_Button,{.12f,.23f,.27f,1});ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{.19f,.32f,.35f,1});ImGui::PushStyleColor(ImGuiCol_ButtonActive,{.10f,.19f,.23f,1});
        if(ImGui::Button("关闭",{closeWidth,buttonHeight}))a.close=true;ImGui::PopStyleColor(3);
        if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)&&ImGui::IsKeyPressed(ImGuiKey_Escape,false))a.close=true;
    }
    ImGui::End();ImGui::PopStyleColor(3);ImGui::PopStyleVar(6);return a;
}
}

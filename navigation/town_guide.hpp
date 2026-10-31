#pragma once

namespace town {
struct Guide {const char* purpose;const char* detail;};
// Researched for the 2013 Alpha. Per-entry web references and version exclusions:
// research/evidence/shop-guide-sources.md. These are services/common categories, not live stock.
inline Guide guide(int type) {
    switch(type) {
        case 33:return {"夜间与店主交谈，可跳到早上 7 点。",
                        "Alpha 版免费，适合避开夜间赶路。"};
        case 34:return {"购买空瓶、宠物食物与制作配方。",
                        "也可购买滑翔翼、船，或出售杂物。"};
        case 35:return {"购买各职业武器，也可出售杂物。",
                        "常见类型：剑、匕首、弓、法杖等。"};
        case 36:return {"购买各职业防具，也可出售杂物。",
                        "可买胸甲、手套、鞋、肩甲等。"};
        case 37:return {"付费鉴定掉落的未鉴定物品。",
                        "鉴定后可获得武器或防具。"};
        case 38:return {"熔炉加工金属，铁砧制作装备。",
                        "附近改造台可添加方块，改造武器。"};
        case 39:return {"锯木台加工木材，工作台制作武器。",
                        "主要制作弓、弩、法杖等木制武器。"};
        case 40:return {"纺车加工纱线，织布机制作防具。",
                        "主要用于棉、麻、丝等布质装备。"};
        default:return {"",""};
    }
}
}

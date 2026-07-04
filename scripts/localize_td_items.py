#!/usr/bin/env python3
"""
Convert TD Defence items: name/desc → Chinese, add name_en/desc_en for original English.
"""
import json, os, re

BASE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "server_content", "td", "items")

# ── Tool translations ───────────────────────────────────────────────

MAT_CN = {
    "Wooden": "木制", "Copper": "铜制", "Iron": "铁制", "Golden": "金制",
    "Diamond": "钻石", "Enegry": "能量", "Energy": "能量",
    "Titanium": "钛合金", "Void Shard": "虚空碎片", "Fortune": "幸运",
    "Flame": "烈焰",
}

TOOL_CN = {"Pickaxe": "镐", "Axe": "斧", "Sword": "剑",
           "Chestplate": "胸甲", "Helmet": "头盔", "Leggings": "护腿"}

DESC_TOOL_CN = {
    "An pickaxe made of wood.": "一把木制镐。",
    "An pickaxe made of copper.": "一把铜制镐。",
    "An pickaxe made of iron.": "一把铁制镐。",
    "An pickaxe made of gold.": "一把金制镐。",
    "An pickaxe made of diamond.": "一把钻石镐。",
    "An pickaxe made of enegry.": "一把能量镐。",
    "Energy pickaxe with mining luck": "带有采矿幸运的能量镐。",
    "An axe made of wood.": "一把木制斧。",
    "An axe made of copper.": "一把铜制斧。",
    "An axe made of iron.": "一把铁制斧。",
    "An axe made of gold.": "一把金制斧。",
    "An axe made of diamond.": "一把钻石斧。",
    "An sword made of wood.": "一把木制剑。",
    "An sword made of iron.": "一把铁制剑。",
    "An sword made of gold.": "一把金制剑。",
    "An sword made of diamond.": "一把钻石剑。",
    "An sword made of enegry.": "一把能量剑。",
    "Elemental sword that ignites targets": "能点燃目标的元素剑。",
    "A chestplate made of wood.": "木制胸甲。",
    "A chestplate made of iron.": "铁制胸甲。",
    "A chestplate made of copper.": "铜制胸甲。",
    "A chestplate made of titanium alloy.": "钛合金胸甲。",
    "A chestplate made of diamond.": "钻石胸甲。",
    "A chestplate made of pure energy.": "纯能量胸甲。",
    "A chestplate forged from void shards.": "虚空碎片锻造的胸甲。",
    "A helmet made of wood.": "木制头盔。",
    "A helmet made of iron.": "铁制头盔。",
    "A helmet made of copper.": "铜制头盔。",
    "A helmet made of titanium alloy.": "钛合金头盔。",
    "A helmet made of diamond.": "钻石头盔。",
    "A helmet made of pure energy.": "纯能量头盔。",
    "A helmet forged from void shards.": "虚空碎片锻造的头盔。",
    "Leggings made of wood.": "木制护腿。",
    "Leggings made of iron.": "铁制护腿。",
    "Leggings made of copper.": "铜制护腿。",
    "Leggings made of titanium alloy.": "钛合金护腿。",
    "Leggings made of diamond.": "钻石护腿。",
    "Leggings made of pure energy.": "纯能量护腿。",
    "Leggings forged from void shards.": "虚空碎片锻造的护腿。",
}

# ── Card translations ────────────────────────────────────────────────

CARD_NAME_CN = {
    "Card: Quickly-Fire": "卡牌：速射",
    "Card: Quickly-Loading": "卡牌：快速装填",
    "Card: Damage": "卡牌：伤害",
    "Card: Explosion": "卡牌：爆炸",
    "Card: Electron": "卡牌：电子",
    "Card: Fusion": "卡牌：聚变",
    "Card: Force": "卡牌：力场",
    "Card: Manual Control": "卡牌：手动控制",
    "Card: Lifesteal": "卡牌：生命偷取",
    "Card: Armor Shred": "卡牌：破甲",
    "Card: Chain Lightning": "卡牌：连锁闪电",
    "Card: Mining Luck": "卡牌：采矿幸运",
    "Card: Haste": "卡牌：急速",
    "Card: Vitality": "卡牌：活力",
    "Card: Thorns": "卡牌：荆棘",
    "Card: Regeneration": "卡牌：再生",
    "Card: Fortification": "卡牌：加固",
    "Card: Absorption Shield": "卡牌：吸收护盾",
    "Card: Fortune": "卡牌：幸运",
    "Card: Swiftness": "卡牌：迅捷",
    "Card: Retribution": "卡牌：报应",
    "Card: Resilience": "卡牌：坚韧",
    "Card: Self-Harm Immunity": "卡牌：自伤免疫",
}

CARD_DESC_CN = {
    "Haste makes apple.(Reload Timer)": "欲速则不达。（装弹加速）",
    "Haste makes apple.(Ammo Regen)": "欲速则不达。（弹药恢复）",
    "Oh, that hurt!": "哦，那可真痛！",
    "BOOM!": "轰！",
    "Numb.": "麻痹。",
    "World War III": "第三次世界大战",
    "Fly away.": "飞走吧。",
    "Be your own.": "做自己的主人。",
    "Heal on hit": "击中回血",
    "Strip enemy armor": "剥离敌方护甲",
    "Extra lightning chains": "额外闪电链",
    "Better mining crit chance": "提高采矿暴击率",
    "General attack speed (lighter than Quickly-Fire)": "通用攻击速度（比速射更轻量）",
    "Increases max health": "增加最大生命值",
    "Reflect damage to attackers": "向攻击者反弹伤害",
    "Heal over time": "持续回血",
    "Flat damage reduction": "固定伤害减免",
    "Chance to gain shield on hit": "攻击时概率获得护盾",
    "Chance to double item pickups": "概率双倍拾取物品",
    "Increases movement speed": "提高移动速度",
    "Gain attack power after taking damage": "受伤后获得攻击力加成",
    "Reduce damage when below half health": "生命低于一半时减少伤害",
    "Reduce self-inflicted damage; full immunity on all armor": "减少自伤；穿戴全身护甲时完全免疫",
}

# ── Material extra translations ──────────────────────────────────────

MAT_EXTRA_NAME_CN = {"Titanium": "钛合金", "Void Shard": "虚空碎片"}
MAT_EXTRA_DESC_CN = {
    "Mid-game refined ore": "中后期精炼矿石",
    "Rare crystal for advanced crafts": "用于高级制作的稀有水晶",
}

# ── Part translations ────────────────────────────────────────────────

PART_NAME_CN = {"Barrel": "炮管", "Cooling": "冷却系统", "Magazine": "弹匣", "Stabilizer": "稳定器"}
PART_DESC_CN = {
    "Wider shot spread for turrets": "扩大炮塔散射范围",
    "Reduces turret fire cooldown when mounted": "降低炮塔射击冷却",
    "Extra ammo capacity when embedded": "增加弹药容量",
    "Extends turret effective range": "延长炮塔有效射程",
}

# ── Turret translations ─────────────────────────────────────────────

TURRET_NAME_CN = {
    "Beginner Turret": "初级炮塔",
    "Intermediate Turret": "中级炮塔",
    "Advanced Turret": "高级炮塔",
    "Sniper Turret": "狙击炮塔",
    "Splash Turret": "溅射炮塔",
}
TURRET_DESC_CN = {
    "The beginning.": "起点。",
    "The intermediate.": "进阶。",
    "The End.": "终点。",
    "Long range precision turret": "远程精准炮塔",
    "Area damage turret": "范围伤害炮塔",
}

# ── CK material translations ─────────────────────────────────────────

CK_NAME_CN = {"Log": "原木", "Copper": "铜矿", "Iron": "铁矿", "Coal": "煤矿",
              "Golden": "金矿", "Diamond": "钻石矿", "Enegry": "能量晶石"}
CK_DESC_CN = {
    "The most basic material.": "最基础的材料。",
    "Maybe they can make wires.": "也许可以用来做电线。",
    "That could be you first step.": "这将是你迈出的第一步。",
    "Fire! Fire! Fire!": "火！火！火！",
    "Shining and Sparkling!": "闪闪发光！",
    "DIAMOND!": "钻石！",
    "Contains enormous energy.": "蕴含着巨大的能量。",
}

# ── Zombie heart ─────────────────────────────────────────────────────

ZOMBIE_NAME_CN = "僵尸之心"
ZOMBIE_DESC_CN = "它们要去汇报血腥暴力了。"


def add_en_fields(item, name_cn, desc_cn):
    """For a single item dict, add name_en/desc_en and set name/desc to Chinese."""
    if "name" in item and item["name"]:
        item["name_en"] = item["name"]
        if name_cn:
            item["name"] = name_cn
    if "desc" in item and item["desc"]:
        item["desc_en"] = item["desc"]
        if desc_cn:
            item["desc"] = desc_cn


def process_tool_file(filename, tool_cn_suffix):
    """Process a tool file (pickaxe, axe, sword, chest, helmet, leggings)."""
    path = os.path.join(BASE, "tools", filename)
    with open(path, "r") as f:
        data = json.load(f)
    for entry in data["item"]["multiple"]:
        en_name = entry["name"]
        en_desc = entry["desc"]
        # Build Chinese name: e.g. "Wooden" → "木制", "Pickaxe" → "镐"
        parts = en_name.split()
        mat = parts[0]
        mat_cn = MAT_CN.get(mat, mat)
        name_cn = mat_cn + tool_cn_suffix
        desc_cn = DESC_TOOL_CN.get(en_desc, en_desc)  # might have exact match
        if not desc_cn or desc_cn == en_desc:
            desc_cn = mat_cn + tool_cn_suffix + "。"
        add_en_fields(entry, name_cn, desc_cn)
    with open(path, "w") as f:
        json.dump(data, f, indent=4, ensure_ascii=False)
    print(f"  ✓ {filename}")


def process_tool_files():
    print("── Tools ──")
    process_tool_file("pickaxe.json", "镐")
    process_tool_file("axe.json", "斧")
    process_tool_file("sword.json", "剑")
    process_tool_file("chest.json", "胸甲")
    process_tool_file("helmet.json", "头盔")
    process_tool_file("leggings.json", "护腿")


def process_cards():
    print("── Cards ──")
    path = os.path.join(BASE, "special", "cards.json")
    with open(path, "r") as f:
        data = json.load(f)
    for entry in data["item"]["multiple"]:
        en_name = entry["name"]
        en_desc = entry["desc"]
        name_cn = CARD_NAME_CN.get(en_name, en_name)
        desc_cn = CARD_DESC_CN.get(en_desc, en_desc)
        add_en_fields(entry, name_cn, desc_cn)
    with open(path, "w") as f:
        json.dump(data, f, indent=4, ensure_ascii=False)
    print("  ✓ cards.json")


def process_materials_extra():
    print("── Materials Extra ──")
    path = os.path.join(BASE, "special", "materials_extra.json")
    with open(path, "r") as f:
        data = json.load(f)
    for entry in data["item"]["multiple"]:
        en_name = entry["name"]
        en_desc = entry["desc"]
        name_cn = MAT_EXTRA_NAME_CN.get(en_name, en_name)
        desc_cn = MAT_EXTRA_DESC_CN.get(en_desc, en_desc)
        add_en_fields(entry, name_cn, desc_cn)
    with open(path, "w") as f:
        json.dump(data, f, indent=4, ensure_ascii=False)
    print("  ✓ materials_extra.json")


def process_parts():
    print("── Parts ──")
    path = os.path.join(BASE, "special", "parts.json")
    with open(path, "r") as f:
        data = json.load(f)
    for entry in data["item"]["multiple"]:
        en_name = entry["name"]
        en_desc = entry["desc"]
        name_cn = PART_NAME_CN.get(en_name, en_name)
        desc_cn = PART_DESC_CN.get(en_desc, en_desc)
        add_en_fields(entry, name_cn, desc_cn)
    with open(path, "w") as f:
        json.dump(data, f, indent=4, ensure_ascii=False)
    print("  ✓ parts.json")


def process_turrets():
    print("── Turrets ──")
    path = os.path.join(BASE, "special", "turret.json")
    with open(path, "r") as f:
        data = json.load(f)
    for entry in data["item"]["multiple"]:
        en_name = entry["name"]
        en_desc = entry["desc"]
        name_cn = TURRET_NAME_CN.get(en_name, en_name)
        desc_cn = TURRET_DESC_CN.get(en_desc, en_desc)
        add_en_fields(entry, name_cn, desc_cn)
    with open(path, "w") as f:
        json.dump(data, f, indent=4, ensure_ascii=False)
    print("  ✓ turret.json")


def process_zombie_heart():
    print("── Zombie Heart ──")
    path = os.path.join(BASE, "special", "zombie-heart.json")
    with open(path, "r") as f:
        data = json.load(f)
    item = data["item"]
    if "name" in item and item["name"]:
        item["name_en"] = item["name"]
        item["name"] = ZOMBIE_NAME_CN
    if "desc" in item and item["desc"]:
        item["desc_en"] = item["desc"]
        item["desc"] = ZOMBIE_DESC_CN
    with open(path, "w") as f:
        json.dump(data, f, indent=4, ensure_ascii=False)
    print("  ✓ zombie-heart.json")


def process_cks():
    print("── CK Materials ──")
    ck_dir = os.path.join(BASE, "cks")
    for filename in os.listdir(ck_dir):
        if not filename.endswith(".json"):
            continue
        path = os.path.join(ck_dir, filename)
        with open(path, "r") as f:
            data = json.load(f)
        item = data["item"]
        en_name = item.get("name", "")
        en_desc = item.get("desc", "")
        name_cn = CK_NAME_CN.get(en_name, en_name)
        desc_cn = CK_DESC_CN.get(en_desc, en_desc)
        if en_name:
            item["name_en"] = en_name
            item["name"] = name_cn
        if en_desc:
            item["desc_en"] = en_desc
            item["desc"] = desc_cn
        with open(path, "w") as f:
            json.dump(data, f, indent=4, ensure_ascii=False)
        print(f"  ✓ {filename}")


def main():
    print("=== Localizing TD Items ===")
    process_tool_files()
    process_cards()
    process_materials_extra()
    process_parts()
    process_turrets()
    process_zombie_heart()
    process_cks()
    print("=== Done! ===")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
Localize all items: replace name/description/desc fields with Chinese,
collect all translation keys, and write to zh-cn.json and en.json.
"""
import json
import os
import re
from copy import deepcopy

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MMO_ITEMS_PATH = os.path.join(BASE_DIR, "server_content/mmo/mmo_items.json")
TD_DIR = os.path.join(BASE_DIR, "server_content/td/items")
LANG_ZH = os.path.join(BASE_DIR, "server_lang/zh-cn.json")
LANG_EN = os.path.join(BASE_DIR, "server_lang/en.json")

# ============================================================
# MMO item Chinese translations (id -> (name_cn, desc_cn))
# ============================================================
MMO_TRANSLATIONS = {
    1: ("金币", "泰趣世界的标准货币"),
    2: ("灵魂碎片", "凝聚了灵魂能量的碎片，可在 specialty 商店使用"),
    10: ("小型生命药水", "恢复 30 HP"),
    11: ("生命药水", "恢复 80 HP"),
    12: ("大型生命药水", "恢复 200 HP"),
    13: ("超级生命药水", "恢复 500 HP"),
    14: ("小型魔力药水", "恢复 30 MP"),
    15: ("魔力药水", "恢复 80 MP"),
    16: ("大型魔力药水", "恢复 200 MP"),
    17: ("生命精华", "完全恢复 HP 和 MP"),
    18: ("解毒剂", "治愈中毒状态"),
    19: ("万能药", "移除所有负面状态效果"),
    20: ("速度药水", "30 秒内提升移动速度"),
    21: ("狂战士药剂", "20 秒内攻击力提升 50%"),
    22: ("铁皮药水", "20 秒内防御力提升 50%"),
    23: ("再生精华", "30 秒内缓慢恢复 HP"),
    24: ("面包", "普通面包，恢复 10 HP"),
    25: ("熟肉", "丰盛的熟肉，恢复 40 HP"),
    26: ("水果沙拉", "清爽的沙拉，恢复 20 HP 和 15 MP"),
    27: ("皇家盛宴", "丰盛的盛宴，完全恢复 HP 和 MP，并在 5 分钟内提升攻击力"),
    30: ("铜矿石", "常见的铜矿石，用于基础合成"),
    31: ("铁矿石", "铁矿石，用于武器和护甲合成"),
    32: ("银矿石", "闪亮的银矿石，蕴含光明灵魂能量"),
    33: ("金矿石", "珍贵的金矿石，用于高阶合成"),
    34: ("秘银矿石", "来自远古时代的传奇蓝色金属"),
    35: ("山铜矿石", "密度极高的金属，嗡嗡作响着力量"),
    36: ("钛锭", "轻量却极其坚固的锻造钛锭"),
    37: ("暗钢锭", "由稀有暗铁锻造的锭，闪烁着暗影能量"),
    38: ("木板", "加工过的木板，基础建筑材料"),
    39: ("远古木料", "来自原始森林的木材，蕴含自然精华"),
    40: ("治疗草药", "具有温和治疗效果的野生草药"),
    41: ("魔法草药", "散发着魔力的药草，用于药水合成"),
    42: ("布料碎片", "剩余的布料，可用于轻甲和绷带"),
    43: ("魔法布", "用附魔线织成的布料，闪烁着能量"),
    44: ("丝绸", "来自大沙漠蜘蛛的奢华丝绸，以其强度著称"),
    45: ("皮革条", "加工过的皮革条，用于装备合成"),
    46: ("宝石碎片", "魔法宝石的小碎片"),
    47: ("附魔宝石", "完整的宝石，脉动着灵魂能量"),
    48: ("净化水晶", "纯净之光的水晶，据说能净化一切黑暗"),
    49: ("世界灵魂精华", "世界本身的原始精华，极其稀有"),
    50: ("史莱姆核心", "击败史莱姆后留下的核心"),
    51: ("哥布林耳朵", "哥布林的耳朵，狩猎的证明"),
    52: ("狼牙", "森林狼的锋利尖牙"),
    53: ("蝙蝠翅膀", "洞穴蝙蝠的革质翅膀"),
    54: ("蜘蛛丝", "来自巨型蜘蛛的精细蛛丝"),
    55: ("骷髅骨头", "活化骷髅的碎片"),
    56: ("老鼠尾巴", "下水道老鼠的肮脏尾巴"),
    57: ("仙尘", "顽皮森林仙子的闪亮粉末"),
    58: ("雪人毛皮", "山地雪人的厚实温暖毛皮"),
    59: ("蝎子毒刺", "沙漠蝎子的有毒螫针"),
    60: ("幽灵精华", "游荡灵魂的灵质残留"),
    61: ("熔岩核心", "熔岩之核，仍然滚烫"),
    62: ("冰晶", "永不融化的冰冻水晶，散发着寒意"),
    63: ("树精之根", "树精的古老树根，仍然涌动着自然的脉动"),
    64: ("鹰身女妖羽毛", "鹰身女妖的锋利羽毛"),
    65: ("龟壳", "巨龟的厚重龟壳，几乎坚不可摧"),
    66: ("蛇发女妖之眼", "蛇发女妖的石化之眼，仍然闪烁着力量"),
    67: ("魔像岩石", "石魔像的碎片，蕴含着古老的能量"),
    68: ("暗影精华", "来自暗影生物的高浓度暗影能量"),
    69: ("吸血鬼尖牙", "吸血鬼领主的血迹斑斑的尖牙"),
    70: ("死灵法师头骨", "堕落死灵法师的头骨，仍在低语黑暗的秘密"),
    71: ("双足飞龙鳞片", "凶猛双足飞龙的鳞片，比钢铁更硬"),
    72: ("牛头人角", "迷宫牛头人的巨大角"),
    73: ("巨兽皮革", "传奇巨兽的厚皮，几乎无法穿透"),
    74: ("九头蛇之血", "多头九头蛇的再生之血"),
    75: ("龙鳞", "远古巨龙的光芒四射的鳞片，涌动着元素之怒"),
    76: ("远古遗物", "来自失落时代的神秘工艺品"),
    77: ("扭曲荆棘", "被黑暗魔法扭曲的荆棘，无法出售"),
    78: ("回音铃铛", "发出遥远回音的小铃铛"),
    79: ("记忆碎片", "某人失落记忆的碎片，微弱地闪烁着光芒"),
    80: ("花之信", "图书管理员封缄的信件"),
    81: ("神秘子弹", "刻有未知符文的旧子弹，摸起来有种奇异的温热"),
    82: ("灵魂水晶", "蕴含纯净灵魂能量的水晶"),
    83: ("封印卷轴", "被魔法封印的古老卷轴，或许专家能解读"),
    84: ("图书馆钥匙", "通往图书馆禁区的生锈钥匙"),
    85: ("破损指南针", "不指向北方，而是指向别处的指南针"),
    86: ("低语之石", "贴在耳边会低语微弱话语的石头"),
    87: ("封缄药剂瓶", "装有神秘液体的瓶子，蜡封完好"),
    88: ("旧地图碎片", "一张旧地图的碎片，只显示了一小片区域"),
    89: ("月光宝石", "在月光下微微发光的宝石"),
    90: ("守护者徽章", "带有远古守护者印记的徽章"),
    91: ("遗忘日记", "皮面装订的日记，写满了难以理解的文字"),
    92: ("女神之泪", "世界女神本人的结晶之泪"),
    93: ("附魔卷轴", "包含基础附魔术的卷轴"),
    94: ("保护卷轴", "保护物品在升级失败时不被摧毁的卷轴"),
    95: ("升级石", "用于升级装备的普通石头"),
    96: ("高级升级石", "可以升级高阶装备的稀有石头"),
    97: ("重铸锤", "可以重铸物品属性的神秘锤子"),
    98: ("镶嵌宝石·红", "镶嵌后提升攻击力的火红宝石"),
    99: ("镶嵌宝石·蓝", "镶嵌后提升防御力的深蓝宝石"),
    100: ("回城卷轴", "传送回最后一个安全检查点"),
    101: ("铜钥匙", "开启基础锁的简单铜钥匙"),
    102: ("银钥匙", "开启附魔锁的银钥匙"),
    103: ("金钥匙", "开启封印宝库的闪亮金钥匙"),
    104: ("暗影地牢通行证", "允许进入暗影地牢的通行证"),
    105: ("烈焰深渊通行证", "允许进入烈焰深渊的通行证"),
    106: ("基础鱼竿", "用于钓鱼的简单鱼竿"),
    107: ("生鱼", "刚捕到的鱼，可以烹饪或生吃"),
    108: ("珍珠", "来自深海的光泽珍珠"),
    109: ("泰趣徽章", "展示你泰趣精神的彩色徽章！"),
    110: ("木槌", "基础木制训练锤"),
    111: ("铁锤", "坚固的铁质战锤"),
    112: ("钢锤", "平衡性极佳的钢锤"),
    113: ("灵魂之锤", "蕴含灵魂能量的锤子，兼具攻击与防御"),
    114: ("泰坦巨锤", "传说中远古泰坦使用的巨型锤子"),
    115: ("碎界者", "传说中可以粉碎山脉的锤子"),
    116: ("旧手枪", "破旧但仍可用的手枪"),
    117: ("银之手枪", "精制的银质手枪，优雅而致命"),
    118: ("速射手枪", "经过改造、射击机制增强的枪"),
    119: ("兹万回响", "低语着风之灵的手枪，据说是兹万的遗物"),
    120: ("圣者左轮", "古代圣者使用过的被祝福的左轮手枪"),
    121: ("唤风者", "传奇手枪，射出的子弹由狂风本身携带"),
    122: ("手炮", "粗制的散弹枪，可发射大范围散射的弹丸"),
    123: ("爆裂霰弹枪", "威力强大的霰弹枪，具有毁灭性的散射范围"),
    124: ("重型散弹枪", "加固的散弹枪，发射装满弹片的弹壳"),
    125: ("毁灭者", "巨大的霰弹枪，能撕裂人群，但携带沉重"),
    126: ("雷霆之击", "每次开火都如雷鸣般咆哮的霰弹枪"),
    127: ("大灾变", "终极霰弹枪，能够释放末日般的毁灭"),
    128: ("爆破棒", "简单但有效的榴弹发射器"),
    129: ("摧毁者", "为攻城战建造的重型发射器"),
    130: ("投射手", "精度增强的榴弹发射器，射程更远"),
    131: ("掷弹兵", "大师级掷弹兵使用的精英发射器"),
    132: ("集束发射器", "发射撞击后散开的集束炸药的发射器"),
    133: ("一人军队", "传说中的发射器，让持有者成为一支军队"),
    134: ("聚焦棒", "将灵魂能量聚焦成光束的基础棒"),
    135: ("水晶长枪", "引导纯净灵魂之光并带有保护力的水晶棒"),
    136: ("棱镜光束", "将光束分裂成多道射线的激光武器"),
    137: ("星光光束", "引导遥远星光力量的武器"),
    138: ("虚空射线", "从世界之间的虚空中汲取力量的恐怖激光"),
    139: ("破晓者", "终极激光武器，带来黎明之光消灭一切黑暗"),
    140: ("皮帽", "提供最低限度防护的基础皮帽"),
    141: ("铁头盔", "提供坚实头部防护的坚固铁头盔"),
    142: ("钢头盔", "为前线战斗锻造的加固钢头盔"),
    143: ("守护者之冠", "受远古守护者祝福的冠冕式头盔"),
    144: ("龙之首", "用巨龙颅骨锻造的恐怖头盔，散发着强大力量"),
    145: ("皮背心", "简单的皮质护胸"),
    146: ("铁胸甲", "提供可靠防护的坚固铁胸甲"),
    147: ("钢板甲", "大师铁匠锻造的重型钢甲"),
    148: ("守护者板甲", "受世界灵魂祝福的远古盔甲"),
    149: ("龙鳞锁甲", "用龙鳞编织的传奇盔甲，几乎不可摧毁"),
    150: ("布裤", "提供基础腿部防护的简单布裤"),
    151: ("铁护胫", "从腰部保护到小腿的铁制护腿"),
    152: ("钢护腿", "钢加固的护腿，提供持久的腿部防护"),
    153: ("守护者腿甲", "被祝福的腿甲，如风一般敏捷"),
    154: ("泰坦腿甲", "远古泰坦穿戴的巨型腿甲"),
    155: ("皮靴", "适合日常探险的结实皮靴"),
    156: ("铁靴", "铁加固的战靴，在战斗中保护双脚"),
    157: ("钢护胫", "为战场耐久性锻造的重型钢靴"),
    158: ("迅捷靴", "轻量附魔靴子，让你步伐更轻盈"),
    159: ("泰坦践踏者", "每一步都能震撼大地的巨型靴子"),
    160: ("布手套", "提供基础手部防护的简单布手套"),
    161: ("铁护手", "既能提供防护又能握紧武器的铁护手"),
    162: ("钢手套", "增强攻防的钢制手套"),
    163: ("守护者之握", "通过双手传导守护者之力的附魔手套"),
    164: ("泰坦之拳", "泰坦战士佩戴的巨型护手，碾碎一切阻碍"),
    170: ("灵魂戒指", "注入灵魂能量的戒指，同时增强攻击和防御"),
    171: ("防护之戒", "在佩戴者周围生成防护屏障的戒指"),
    172: ("力量之戒", "噼啪作响着原始力量的戒指，增强佩戴者力量"),
    173: ("巨人之戒", "赋予远古巨人生命力的戒指"),
    174: ("风之护符", "随着微风低鸣的护符，ATK+8, DEF+8，来自一段遗失的恋情"),
    175: ("迅捷项链", "让你感觉更敏捷的轻量项链"),
    176: ("专注耳环", "在战斗中提高注意力的耳环"),
    177: ("力量手镯", "由纯力量增强合金锻造的手镯"),
    178: ("巨人腰带", "用巨人毛发编织的粗腰带，赋予巨大耐久力"),
    179: ("幽灵披风", "在月光下如幽灵般闪烁的半透明斗篷"),
    180: ("凤凰护符", "蕴含凤凰羽毛的传说护符，可以复活佩戴者一次"),
    181: ("龙骨之戒", "用龙骨锻造的戒指，散发着远古力量"),
    182: ("星辰胸针", "星形胸针，赋予佩戴者均衡的力量"),
    183: ("世界核心", "世界之心的结晶，赋予难以想象的力量"),
    190: ("天使之翼", "散发着圣光的一对光辉天使翅膀"),
    191: ("恶魔之角", "将你标记为令人恐惧存在的邪恶之角"),
    192: ("光环王冠", "将你标记为神圣祝福者的浮空光环王冠"),
    193: ("花之书签", "来自图书馆的精美书签，隐约散发着旧纸的气息"),
    194: ("迷你史莱姆宠物", "跟着你到处走的可爱迷你史莱姆，开心地弹跳着"),
    195: ("迷你龙宠物", "栖息在你肩膀上的小龙崽"),
    196: ("高顶礼帽", "适合有品味的探险家的高雅礼帽"),
    197: ("派对帽", "用于庆祝的彩色锥形帽"),
    198: ("太阳镜", "让一切看起来都很酷的墨镜"),
    199: ("黄金王冠", "镶满珠宝的华丽王冠，适合皇室"),
    200: ("基础鱼竿", "配有基础钓具的简单竹制鱼竿"),
    201: ("坚固鱼竿", "可以钓上更大鱼获的加固鱼竿"),
    202: ("魔法鱼竿", "能吸引远处稀有鱼类的附魔鱼竿"),
    203: ("生鱼", "刚捕到的鱼，可以烹饪或生吃"),
    204: ("金鱼", "闪闪发光的稀有金色鱼"),
    205: ("旧靴子", "湿漉漉的旧靴子，没什么用"),
    206: ("宝箱", "可能装有值钱物品的小型浸水箱子"),
    207: ("瓶中信", "装在瓶子里的信，从远方漂流而来"),
    208: ("鱼饵", "用于吸引普通鱼类的标准鱼饵"),
    209: ("幸运鱼饵", "附魔的特殊鱼饵，吸引稀有鱼获"),
}

# ============================================================
# TD item Chinese translations (item_id -> (name_cn, desc_cn))
# These supplement what we handle programmatically below
# ============================================================
# For items where we know the exact name_key pattern
# Most TD items will be handled programmatically based on their names

def translate_td_tool_name(name):
    """Translate tool names to Chinese"""
    mapping = {
        "Wooden Pickaxe": "木镐",
        "Copper Pickaxe": "铜镐",
        "Iron Pickaxe": "铁镐",
        "Golden Pickaxe": "金镐",
        "Diamond Pickaxe": "钻石镐",
        "Energy Pickaxe": "能量镐",
        "Fortune Pickaxe": "幸运镐",
        "Wooden Axe": "木斧",
        "Copper Axe": "铜斧",
        "Iron Axe": "铁斧",
        "Golden Axe": "金斧",
        "Diamond Axe": "钻石斧",
        "Wooden Sword": "木剑",
        "Iron Sword": "铁剑",
        "Golden Sword": "金剑",
        "Diamond Sword": "钻石剑",
        "Energy Sword": "能量剑",
        "Flame Sword": "烈焰剑",
        "Wooden Chestplate": "木制胸甲",
        "Iron Chestplate": "铁质胸甲",
        "Copper Chestplate": "铜质胸甲",
        "Titanium Chestplate": "钛合金胸甲",
        "Diamond Chestplate": "钻石胸甲",
        "Energy Chestplate": "能量胸甲",
        "Void Shard Chestplate": "虚空碎片胸甲",
        "Wooden Helmet": "木制头盔",
        "Iron Helmet": "铁质头盔",
        "Copper Helmet": "铜质头盔",
        "Titanium Helmet": "钛合金头盔",
        "Diamond Helmet": "钻石头盔",
        "Energy Helmet": "能量头盔",
        "Void Shard Helmet": "虚空碎片头盔",
        "Wooden Leggings": "木制护腿",
        "Iron Leggings": "铁质护腿",
        "Copper Leggings": "铜质护腿",
        "Titanium Leggings": "钛合金护腿",
        "Diamond Leggings": "钻石护腿",
        "Energy Leggings": "能量护腿",
        "Void Shard Leggings": "虚空碎片护腿",
    }
    return mapping.get(name, name)


def translate_td_tool_desc(name, desc):
    """Translate tool descriptions to Chinese"""
    desc_mapping = {
        "A pickaxe made of wood.": "一把木头做的镐。",
        "A pickaxe made of copper.": "一把铜做的镐。",
        "A pickaxe made of iron.": "一把铁做的镐。",
        "A pickaxe made of gold.": "一把金子做的镐。",
        "A pickaxe made of diamond.": "一把钻石做的镐。",
        "A pickaxe made of energy.": "一把能量做的镐。",
        "Energy pickaxe with mining luck.": "带有采矿运气的能量镐。",
        "An axe made of wood.": "一把木头做的斧。",
        "An axe made of copper.": "一把铜做的斧。",
        "An axe made of iron.": "一把铁做的斧。",
        "An axe made of gold.": "一把金子做的斧。",
        "An axe made of diamond.": "一把钻石做的斧。",
        "A sword made of wood.": "一把木头做的剑。",
        "An iron sword.": "一把铁做的剑。",
        "A golden sword.": "一把金子做的剑。",
        "A diamond sword.": "一把钻石做的剑。",
        "An energy-infused sword.": "一把能量做的剑。",
        "An elemental sword that sets targets ablaze.": "点燃目标的元素剑。",
        "A chestplate made of wood.": "一件木头做的胸甲。",
        "A chestplate made of iron.": "一件铁做的胸甲。",
        "A chestplate made of copper.": "一件铜做的胸甲。",
        "A chestplate made of titanium alloy.": "一件钛合金做的胸甲。",
        "A chestplate made of diamond.": "一件钻石做的胸甲。",
        "A chestplate made of pure energy.": "一件纯能量做的胸甲。",
        "A chestplate forged from void shards.": "一件由虚空碎片锻造的胸甲。",
        "A helmet made of wood.": "一顶木头做的头盔。",
        "A helmet made of iron.": "一顶铁做的头盔。",
        "A helmet made of copper.": "一顶铜做的头盔。",
        "A helmet made of titanium alloy.": "一顶钛合金做的头盔。",
        "A helmet made of diamond.": "一顶钻石做的头盔。",
        "A helmet made of pure energy.": "一顶纯能量做的头盔。",
        "A helmet forged from void shards.": "一顶由虚空碎片锻造的头盔。",
        "Leggings made of wood.": "一件木头做的护腿。",
        "Leggings made of iron.": "一件铁做的护腿。",
        "Leggings made of copper.": "一件铜做的护腿。",
        "Leggings made of titanium alloy.": "一件钛合金做的护腿。",
        "Leggings made of diamond.": "一件钻石做的护腿。",
        "Leggings made of pure energy.": "一件纯能量做的护腿。",
        "Leggings forged from void shards.": "一件由虚空碎片锻造的护腿。",
    }
    # Try exact match first
    if desc in desc_mapping:
        return desc_mapping[desc]
    return desc


def translate_td_special_item(name, desc, item_id=None):
    """Translate special TD items (cards, parts, turrets, materials, zombie-heart)"""
    # Turrets
    turret_map = {
        "Beginner Turret": "初级炮塔",
        "Intermediate Turret": "中级炮塔",
        "Advanced Turret": "高级炮塔",
        "Sniper Turret": "狙击炮塔",
        "Splash Turret": "溅射炮塔",
    }
    turret_desc_map = {
        "The beginning.": "起步。",
        "The intermediate.": "进阶。",
        "The end.": "终点。",
        "Long range precision turret": "远距离精准炮塔",
        "Area damage turret": "范围伤害炮塔",
    }

    if name in turret_map:
        return turret_map[name], turret_desc_map.get(desc, desc)

    # Cards
    card_map = {
        "Card: Quickly-Fire": "卡牌：速射",
        "Card: Quickly-Loading": "卡牌：快装",
        "Card: Damage": "卡牌：伤害",
        "Card: Explosion": "卡牌：爆炸",
        "Card: Electron": "卡牌：电击",
        "Card: Fusion": "卡牌：聚变",
        "Card: Force": "卡牌：推力",
        "Card: Manual Control": "卡牌：手动瞄准",
        "Card: Lifesteal": "卡牌：吸血",
        "Card: Armor Shred": "卡牌：破甲",
        "Card: Chain Lightning": "卡牌：连锁闪电",
        "Card: Mining Luck": "卡牌：采矿好运",
        "Card: Haste": "卡牌：迅捷",
        "Card: Vitality": "卡牌：生机",
        "Card: Thorns": "卡牌：荆棘",
        "Card: Regeneration": "卡牌：再生",
        "Card: Fortification": "卡牌：强化",
        "Card: Absorption Shield": "卡牌：护盾",
        "Card: Fortune": "卡牌：幸运",
        "Card: Swiftness": "卡牌：疾步",
        "Card: Retribution": "卡牌：复仇",
        "Card: Resilience": "卡牌：不屈",
        "Card: Self-Harm Immunity": "卡牌：免伤",
    }
    card_desc_map = {
        "Haste makes apple. (Reload Timer)": "加快武器装填速度（炮塔射速）",
        "Haste makes apple. (Ammo Regen)": "加快弹药恢复速度",
        "Oh, that hurt!": "提升攻击伤害",
        "BOOM!": "攻击附带爆炸效果",
        "Numb.": "电击敌人，降低移速",
        "World War III": "大范围聚变爆炸",
        "Fly away.": "击退敌人",
        "Be your own.": "炮塔改为手动瞄准射击",
        "Heal on hit": "攻击时恢复生命",
        "Strip enemy armor": "削减敌人护甲",
        "Extra lightning chains": "额外连锁闪电",
        "Better mining crit chance": "提高采矿暴击几率",
        "General attack speed (lighter than Quickly-Fire)": "通用攻速提升（比速射更轻量）",
        "Increases max health": "提升最大生命值",
        "Reflect damage to attackers": "受到伤害时反弹部分伤害给攻击者",
        "Heal over time": "每3秒自动恢复生命值",
        "Flat damage reduction": "每次受击减免固定伤害",
        "Chance to gain shield on hit": "受击时有概率获得临时护盾",
        "Chance to double item pickups": "拾取物品时有概率获得双倍",
        "Increases movement speed": "提升移动速度（仅护腿）",
        "Gain attack power after taking damage": "受伤后短时间内提升攻击力",
        "Reduce damage when below half health": "生命值低于50%时减少所受伤害",
        "Reduce self-inflicted damage; full immunity on all armor": "每件防具减少30%自伤，三件齐全完全免疫",
    }
    if name in card_map:
        return card_map[name], card_desc_map.get(desc, desc)

    # Parts
    parts_map = {
        "Barrel": "炮管",
        "Cooling": "散热器",
        "Magazine": "弹匣",
        "Stabilizer": "稳定器",
    }
    parts_desc_map = {
        "Wider shot spread for turrets": "炮塔散射范围更广",
        "Reduces turret fire cooldown when mounted": "降低炮塔射击冷却",
        "Extra ammo capacity when embedded": "嵌入后增加弹药容量",
        "Extends turret effective range": "延长炮塔射程",
    }
    if name in parts_map:
        return parts_map[name], parts_desc_map.get(desc, desc)

    # Materials extra
    materials_map = {
        "Titanium": "钛锭",
        "Void Shard": "虚空碎片",
    }
    materials_desc_map = {
        "Mid-game refined ore": "中期的精炼矿石",
        "Rare crystal for advanced crafts": "高级合成的稀有水晶",
    }
    if name in materials_map:
        return materials_map[name], materials_desc_map.get(desc, desc)

    # Zombie's Heart
    if name == "Zombie's Heart":
        return "僵尸之心", "它们要向血腥暴力举报了。"

    return name, desc


def translate_cks_name(name):
    """Translate cks ore names to Chinese"""
    cks_map = {
        "Log": "木材",
        "Coal": "煤炭",
        "Copper": "铜",
        "Iron": "铁",
        "Gold": "金",
        "Diamond": "钻石",
        "Energy": "能量",
    }
    return cks_map.get(name, name)


def translate_cks_desc(desc):
    """Translate cks ore descriptions to Chinese"""
    cks_desc_map = {
        "The most basic material.": "最基本的材料。",
        "Fire! Fire! Fire!": "火焰！火焰！火焰！",
        "Maybe they can make wires.": "或许可以用来做电线。",
        "That could be your first step.": "这可能是你的第一步。",
        "Shining and Sparkling!": "闪闪发亮！",
        "DIAMOND!": "钻石！！！",
        "Contains enormous energy.": "蕴含巨大的能量。",
    }
    return cks_desc_map.get(desc, desc)


# ============================================================
# Process MMO items
# ============================================================
def process_mmo_items():
    print("=" * 60)
    print("Processing MMO items...")
    print("=" * 60)

    with open(MMO_ITEMS_PATH, "r", encoding="utf-8") as f:
        data = json.load(f)

    new_keys_cn = {}  # ($key -> cn_value)
    new_keys_en = {}  # ($key -> en_value)

    for item in data["items"]:
        item_id = item["id"]
        orig_name = item["name"]
        orig_desc = item.get("description", "")
        name_key = item["name_key"]
        desc_key = item["description_key"]

        if item_id in MMO_TRANSLATIONS:
            cn_name, cn_desc = MMO_TRANSLATIONS[item_id]
        else:
            print(f"  WARNING: No translation for MMO item ID {item_id}: {orig_name}")
            cn_name = orig_name
            cn_desc = orig_desc

        # Update name/description fields with Chinese
        item["name"] = cn_name
        item["description"] = cn_desc

        # Record translation keys
        new_keys_cn[name_key] = cn_name
        new_keys_cn[desc_key] = cn_desc
        new_keys_en[name_key] = orig_name
        new_keys_en[desc_key] = orig_desc

        print(f"  ID {item_id:3d}: {orig_name:35s} → {cn_name}  |  {orig_desc[:40]:40s} → {cn_desc[:40]}")

    # Write updated MMO items
    with open(MMO_ITEMS_PATH, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)

    print(f"\nTotal MMO items processed: {len(data['items'])}")
    print(f"New translation keys: {len(new_keys_cn)}")

    return new_keys_cn, new_keys_en


# ============================================================
# Process TD items
# ============================================================
def process_td_tools():
    """Process tool-type TD items: pickaxe, axe, sword, chest, helmet, leggings"""
    print("=" * 60)
    print("Processing TD tool items...")
    print("=" * 60)

    tool_files = ["pickaxe.json", "axe.json", "sword.json", "chest.json", "helmet.json", "leggings.json"]
    tool_types = {"pickaxe", "axe", "sword", "chest", "helmet", "leggings"}

    new_keys_cn = {}
    new_keys_en = {}

    for filename in tool_files:
        filepath = os.path.join(TD_DIR, "tools", filename)
        with open(filepath, "r", encoding="utf-8") as f:
            data = json.load(f)

        items = data["item"]["multiple"]
        for item in items:
            orig_name = item["name"]
            orig_desc = item.get("desc", "")
            name_key = item["name_key"]

            # Generate desc_key if not present
            if "description_key" not in item:
                # Generate based on pattern: $td.item.<id>.desc
                item_id = item["id"]
                desc_key = f"$td.item.{item_id}.desc"
                item["description_key"] = desc_key
            else:
                desc_key = item["description_key"]

            cn_name = translate_td_tool_name(orig_name)
            cn_desc = translate_td_tool_desc(cn_name, orig_desc)

            item["name"] = cn_name
            item["desc"] = cn_desc

            new_keys_cn[name_key] = cn_name
            new_keys_cn[desc_key] = cn_desc
            new_keys_en[name_key] = orig_name
            new_keys_en[desc_key] = orig_desc

            print(f"  {filename:15s} ID {item['id']:2d}: {orig_name:25s} → {cn_name}  |  {orig_desc[:35]:35s} → {cn_desc}")

        with open(filepath, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)

    print(f"\nTotal TD tool keys added: {len(new_keys_cn)}")
    return new_keys_cn, new_keys_en


def process_td_special():
    """Process special TD items: cards, materials_extra, parts, turret, zombie-heart"""
    print("=" * 60)
    print("Processing TD special items...")
    print("=" * 60)

    new_keys_cn = {}
    new_keys_en = {}
    unchanged_keys = set()

    # Load existing lang keys to avoid overwriting
    with open(LANG_ZH, "r", encoding="utf-8") as f:
        existing_cn = json.load(f)
        existing_cn_keys = set(existing_cn.keys())

    special_files = [
        "cards.json",
        "materials_extra.json",
        "parts.json",
        "turret.json",
        "zombie-heart.json",
    ]

    for filename in special_files:
        filepath = os.path.join(TD_DIR, "special", filename)
        with open(filepath, "r", encoding="utf-8") as f:
            data = json.load(f)

        if "id" in data["item"]:
            # Single item (zombie-heart.json)
            item = data["item"]
            items = [item]
        else:
            items = data["item"]["multiple"]

        for item in items:
            orig_name = item["name"]
            orig_desc = item.get("desc", "")
            name_key = item["name_key"]

            # Generate desc_key if not present
            if "description_key" in item:
                desc_key = item["description_key"]
            elif "id" in item:
                desc_key = f"$td.item.{item['id']}.desc"
                item["description_key"] = desc_key
            else:
                desc_key = None

            cn_name, cn_desc = translate_td_special_item(orig_name, orig_desc, item.get("id"))

            item["name"] = cn_name
            item["desc"] = cn_desc

            new_keys_cn[name_key] = cn_name
            new_keys_en[name_key] = orig_name

            if desc_key:
                new_keys_cn[desc_key] = cn_desc
                new_keys_en[desc_key] = orig_desc

            print(f"  {filename:25s}: {orig_name:30s} → {cn_name}  |  {orig_desc[:35]:35s} → {cn_desc}")

        with open(filepath, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)

    print(f"\nTotal TD special keys added: {len(new_keys_cn)}")
    return new_keys_cn, new_keys_en


def process_td_cks():
    """Process cks ore items"""
    print("=" * 60)
    print("Processing TD cks items...")
    print("=" * 60)

    new_keys_cn = {}
    new_keys_en = {}

    cks_files = ["coal.json", "copper.json", "diamond.json", "enegry.json",
                  "golden.json", "iron.json", "log.json"]

    for filename in cks_files:
        filepath = os.path.join(TD_DIR, "cks", filename)
        with open(filepath, "r", encoding="utf-8") as f:
            data = json.load(f)

        item = data["item"]
        orig_name = item["name"]
        orig_desc = item.get("desc", "")
        name_key = item["name_key"]

        # Generate desc_key if not present
        if "description_key" not in item:
            item_id = item["id"]
            desc_key = f"$td.cks.{item_id}.desc"
            item["description_key"] = desc_key
        else:
            desc_key = item["description_key"]

        cn_name = translate_cks_name(orig_name)
        cn_desc = translate_cks_desc(orig_desc)

        item["name"] = cn_name
        item["desc"] = cn_desc

        new_keys_cn[name_key] = cn_name
        new_keys_cn[desc_key] = cn_desc
        new_keys_en[name_key] = orig_name
        new_keys_en[desc_key] = orig_desc

        print(f"  {filename:15s}: {orig_name:12s} → {cn_name:5s}  |  {orig_desc[:40]:40s} → {cn_desc}")

        with open(filepath, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)

    print(f"\nTotal TD cks keys added: {len(new_keys_cn)}")
    return new_keys_cn, new_keys_en


# ============================================================
# Update language files
# ============================================================
def merge_into_json(filepath, new_entries, label):
    """
    Insert new entries into the JSON file before the last closing '}'.
    Handles the // comment style used in these files.
    """
    print(f"\n{'=' * 60}")
    print(f"Updating {label} ({filepath}) with {len(new_entries)} new entries...")
    print(f"{'=' * 60}")

    # Read the file as text to preserve comments and formatting
    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()

    # Also load as JSON to verify it's valid
    with open(filepath, "r", encoding="utf-8") as f:
        existing = json.load(f)

    # Filter out entries that already exist
    existing_keys = set(existing.keys())
    to_add = {k: v for k, v in new_entries.items() if k not in existing_keys}

    if not to_add:
        print("  All keys already exist — nothing to add.")
        return

    for k, v in sorted(to_add.items()):
        print(f"  + {k} = {v}")

    # Find last '}' position, insert before it
    # Need to find the last closing brace that's not inside a string
    # Since the JSON is well-formed, we can find the last '}' after stripping trailing whitespace
    stripped = content.rstrip()
    last_brace = stripped.rfind("}")

    if last_brace == -1:
        print("  ERROR: Cannot find closing '}' in file!")
        return

    # Build new entries block
    lines = []
    for k in sorted(to_add.keys()):
        v = to_add[k]
        # Escape special characters in JSON value
        v_escaped = json.dumps(v, ensure_ascii=False)
        lines.append(f"    {json.dumps(k, ensure_ascii=False)}: {v_escaped},")

    # Get indentation before existing last entry
    # Look at the line before the last closing brace
    prefix = stripped.rfind("\n", 0, last_brace)
    # Find the last key-value line
    last_entry_line = stripped.rfind("}", 0, last_brace)
    preceding_text = stripped[prefix + 1:last_brace].strip()
    indentation = "    "  # default 4 spaces

    # Insert new entries right before the last closing brace
    new_block = "\n".join(lines)
    # Remove trailing comma from content before } and handle the comment closing
    # We need to find where the last \n} is
    # Content might have trailing whitespace then }
    last_newline = stripped.rfind("\n", 0, last_brace)
    if last_newline != -1:
        line_before_brace = stripped[last_newline + 1:last_brace]
        # Check if there's a comma on the last entry before }}
        # We'll just insert before the last closing brace with a newline

    new_content = content[:last_brace] + new_block + "\n" + content[last_brace:]

    # Validate the result is valid JSON
    try:
        # Extract just the JSON portion (strip anything before { and after })
        json_start = new_content.find("{")
        json_end = new_content.rfind("}")
        json_part = new_content[json_start:json_end + 1]
        test = json.loads(json_part)
        print(f"  ✓ Valid JSON — {len(test)} total keys (was {len(existing)}, added {len(to_add)})")
    except json.JSONDecodeError as e:
        print(f"  ✗ INVALID JSON after insertion! {e}")
        print("  Writing anyway — fix manually.")
        # Still write the file even if validation fails, but warn

    with open(filepath, "w", encoding="utf-8") as f:
        f.write(new_content)

    print(f"  ✓ Updated successfully.")


def main():
    # Step 1: Process MMO items
    mmo_cn, mmo_en = process_mmo_items()

    # Step 2: Process TD items
    td_tools_cn, td_tools_en = process_td_tools()
    td_special_cn, td_special_en = process_td_special()
    td_cks_cn, td_cks_en = process_td_cks()

    # Step 3: Combine all translation keys
    all_cn = {}
    all_cn.update(mmo_cn)
    all_cn.update(td_tools_cn)
    all_cn.update(td_special_cn)
    all_cn.update(td_cks_cn)

    all_en = {}
    all_en.update(mmo_en)
    all_en.update(td_tools_en)
    all_en.update(td_special_en)
    all_en.update(td_cks_en)

    print(f"\n{'=' * 60}")
    print(f"SUMMARY")
    print(f"{'=' * 60}")
    print(f"Total translation keys: {len(all_cn)}")
    print(f"  MMO:          {len(mmo_cn)} keys")
    print(f"  TD tools:     {len(td_tools_cn)} keys")
    print(f"  TD special:   {len(td_special_cn)} keys")
    print(f"  TD cks:       {len(td_cks_cn)} keys")

    # Step 4: Write to language files
    merge_into_json(LANG_ZH, all_cn, "zh-cn.json")
    merge_into_json(LANG_EN, all_en, "en.json")

    print(f"\n{'=' * 60}")
    print("DONE!")
    print(f"{'=' * 60}")


if __name__ == "__main__":
    main()

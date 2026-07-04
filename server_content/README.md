# server_content — 服务端 JSON 数据

运行时从工作目录下的 `server_content/` 加载（CMake 构建时会复制到 `build/`）。

## 目录结构

```
server_content/
├── README.md              本说明
├── abilities.json         通用能力定义
├── achievements.json      成就
├── craft_recipes.json     合成配方
├── duties.json            日常任务
├── npcs.json              NPC 定义（含对话）
├── portals.json           传送门
├── quests.json            任务链
├── scenarios.json         剧情场景
├── shop_items.json        商店条目
├── skills.json            技能/魔法
├── spawns.json            刷怪点（TD/通用）
├── traits.json            特性
├── worlds.json            世界元数据（与 maps/worlds.json 配合）
├── td/                    塔防模式专用
│   ├── effects.json       Content 效果
│   ├── enemies.json       敌人定义
│   ├── index.json         TD 索引（文档用）
│   └── items/             TD 物品（卡牌、资源、装备）
│       ├── index.json     物品清单 → CItemHelper 加载
│       ├── cks/           资源类
│       ├── tools/         工具/防具
│       └── special/       卡牌、炮塔等
└── mmo/                   MMO/RPG 模式专用
    ├── mmo_items.json     MMO 物品 + weapon_profile
    ├── mmo_mobs.json      怪物
    ├── zones.json         区域
    ├── npc_spawns.json    NPC 刷新
    ├── world_spawns.json  世界刷怪
    └── mini_events.json   迷你活动
```

## 代码加载入口

| 数据 | 加载位置 |
|------|----------|
| TD 物品 | `item_system.cpp` → `server_content/td/items/index.json` |
| MMO 物品/怪物/区域 | `data_center.cpp` |
| 任务 | `quest_manager.cpp` → `quests.json` |
| TD 敌人/效果 | `enemy_registry.cpp` / `effect_registry.cpp` → `td/` |
| 技能 | `skill_manager.cpp` → `skills.json` |

## 本地化

物品/技能等显示文本在 `server_lang/`（`zh-cn.json`、`en.json`），键名如 `$mmo.item.110.name`。

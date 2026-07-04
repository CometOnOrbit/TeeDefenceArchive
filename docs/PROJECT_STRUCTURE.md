# TeeDefenceArchive 项目目录说明

## 顶层结构

```
TeeDefenceArchive/
├── src/                   C++ 源码（engine / game / base）
├── server_content/        服务端 JSON 配置（见 server_content/README.md）
├── server_lang/           多语言字符串
├── maps/                  地图 .map + worlds.json（多世界入口）
├── datasrc/               数据生成脚本与地图源
├── scripts/               Python 工具与集成测试
├── docs/                  文档
├── sql/                   数据库 schema
├── tools/                 内容编辑器 HTML
├── cmake/                 CMake 工具链
├── other/                 打包资源
├── bam.lua / configure.lua  bam 构建定义
└── CMakeLists.txt
```

## 源码要点（`src/game/server/`）

| 路径 | 说明 |
|------|------|
| `core/components/mmo/` | MMO 系统（物品、投票菜单、刷怪、交易…） |
| `core/components/quests/` | 任务与剧情 |
| `core/components/content/` | TD Content 效果/敌人 |
| `entities/mmo/` | MMO 特殊武器 Entity（磁脉冲、推墙、特斯拉链…） |
| `entities/character_bot_ai.cpp` | MMO 怪物 AI |
| `data_center.cpp` | MMO JSON 数据中心 |
| `gamecontroller.cpp` | 武器开火与伤害 |
| `worldmodes/` | defence / hub / pvp / frpg (F\|RPG) |

## 构建产物（已 gitignore）

- `build/`、`build-mysql/` — 编译输出与复制的 server_content
- `config.lua` — 本地 bam 配置

## 文档索引

| 文件 | 内容 |
|------|------|
| [README.md](../README.md) | 快速开始、MySQL、玩家命令 |
| [RPG_GUIDE.md](RPG_GUIDE.md) | MMO 示例玩法 |
| [story-and-map-guide.md](story-and-map-guide.md) | 剧情与地图制作 |

## 常用命令

```bash
bam                                    # 编译
./build/x86_64/debug/ArchiveServer -f autoexec.cfg
python3 scripts/test_td.py --list      # 无 MySQL 集成测试
python3 scripts/localize_mmo_items.py  # MMO 物品本地化
```

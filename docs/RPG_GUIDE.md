# TDA F|RPG 示例指南

## 快速开始

启动服务器后，玩家进入 world 0（主城），自动加载：

### 怪物分布
| 区域 | 怪物 | 等级 | 推荐 |
|------|------|------|------|
| 主城近点 | Slime (Lv1) / Goblin Scout (Lv2) | 1-2 | 🆕 新手 |
| 主城中段 | Dire Wolf (Lv3) / Skeleton (Lv4) | 3-4 | ⭐ 入门 |
| 主城深处 | Goblin Berserker (Lv5) / Dark Elf (Lv6) | 5-6 | ⚔️ 进阶 |
| 最深处 | Goblin Chief (Lv3, Boss) | 3 | 🏆 精英挑战 |

### 掉落的物品
- Slime → Slime Core (制作材料)
- Goblin → Goblin Ear (任务物品)
- Wolf → Wolf Fang (任务物品)
- Skeleton → Skeleton Bone (任务物品)
- Bosses → Ancient Relic (稀有) / Steel Sword / Iron Armor

### 副本
| 副本 | 等级要求 | 世界 | 怪物 | Boss |
|------|---------|------|------|------|
| Goblin Cave | Lv1-5 | 22 | Goblin Scout, Goblin Berserker | Goblin Chief |
| Ancient Ruins | Lv5-10 | 23 | Skeleton, Dark Elf, Stone Golem | Skeleton Lord |

创建：`/dungeon create Goblin Cave` 或 `/dungeon create Ancient Ruins`

### 指令清单
| 指令 | 作用 |
|------|------|
| `/stats` | 查看角色状态 |
| `/addstat str` | 加力量 (+2 攻击) |
| `/addstat con` | 加体质 (+1 防御) |
| `/skills` | 查看已学技能 |
| `/learn Power Strike` | 学习/升级技能 |
| `/shop shopkeeper` | 打开商店 |
| `/buy 1 5` | 买 5 个 Health Potion |
| `/sell 0 3` | 卖背包第 0 格 3 个 |
| `/dungeon list` | 查看副本列表 |
| `/dungeon join 1` | 加入副本 #1 |

### 任务
现有任务（Quest ID 301-303, 104）
- #301 冒险起步 — 杀 10 只 Slime + 交 5 个 Slime Core
- #302 猎杀精英 — 杀 5 只 Goblin Berserker
- #303 探险者 — 杀 10 只 Skeleton + 15 只 Dire Wolf
- #104 金币日结 — 杀 30 只 Slime（日常）

### 升级流程
1. 杀 Slime (Lv1) → 升级到 Lv2 (需要 18+48=66 exp)，+3 技能点
2. `/addstat str` 点攻击力
3. Lv3 后学 Vitality 技能加 HP
4. 攒钱买 Basic Sword → 提升输出
5. Lv5 以后挑战副本

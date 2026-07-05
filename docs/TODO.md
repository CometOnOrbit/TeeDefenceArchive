# 项目 TODO

> 不含 [SOUND_TODO.md](../SOUND_TODO.md) 与 `datasrc/` 内条目。  
> 最后整理：2026-07-04

---

## P0 — 功能缺口 / 体验不一致

### 投票命令体系（收尾）

- [ ] **竞技场聊天去重**：投票侧已有 `ccv_arena_*`，聊天仍保留 `/arena`、`/arena_create`、`/arena_invite`、`/yes`、`/arena_start`、`/arena_cancel`、`/arena_status`（`arena_lobby_manager.cpp`）
- [ ] **公会成员页交互**：踢人/晋升/降级/转让会长已支持 Reason 填名，成员列表仍为只读展示；可为每位成员加 `ccv_guild_kick` 等快捷项（`RenderGuildMembersVotes`）
- [ ] **对话动作 `open_enchant`**：商店/合成/技能/任务已有 `open_*`，强化仅能通过「靠近铁匠 + 经济菜单」；可对称增加 `OPEN_ENCHANT`（`dialog_manager.cpp` / `dialog_data.h`）
- [ ] **`ccv_` 注册表校验**：加脚本或 CI，对比 `mmo_vote_menu.cpp` 等处的 `ccv_*` 与 `AddVoteCommand` 注册名，防止菜单项无处理器

### 玩法 / 系统

- [ ] **MMO 交易邀请**：`mmo_player_data.cpp` 存在 stub 注释，需确认交易全流程是否与投票/聊天设计一致

---

## P1 — 内容与文档

### 剧情 / 地图（见 [story-and-map-guide.md](story-and-map-guide.md) §7.2）

新增任务时的 checklist（每任务执行一次）：

- [ ] `quest.<id>` 标题
- [ ] `quest.<id>.desc` 描述
- [ ] 各步骤 `quest.step.<type>`（自定义类型需改代码）
- [ ] 涉及 NPC 的 `npc.<id>.talk`
- [ ] 新物品奖励的 `item.id.<n>` 本地化（`server_lang`）

### 文档过时

- [ ] **[RPG_GUIDE.md](RPG_GUIDE.md)**：仍写 `/shop`、`/buy`、`/learn` 等旧聊天指令；需改为「投票菜单 + 保留的聊天命令」
- [ ] **公会帮助文本**：`ConGuildCreate` 无参帮助仍列出已删除的 `/guild_invite` 等（若尚未更新）
- [ ] **`server_lang` 同步**：改语言文件后需同步 `build-*/server_lang/` 与 `build-*/data/server_lang/`（见 story-and-map-guide §8.2）

### 编辑器 / 工具

- [ ] 编辑器：离线模式仍可能触发下载式保存，需手动覆盖（`tools/README.md` 已说明，可改进 UX）
- [ ] `scripts/cmd5.py`：`#TODO 0.8: improve nethash creation`

---

## P2 — 代码质量 / 架构

### MMO / 游戏服务端

- [ ] **`CMMOManager` 成员可见性**：`mmo_manager.h` — `m_aGroups`、`m_pPathFinder` 等标为 `public`，TODO 改为 private + accessor
- [ ] **投票选项移除**：`gamecontext.cpp:2161` — `TODO: improve this`（移除投票项时的 heap 重建）
- [ ] **物品静态尺寸**：`gamecontext.cpp:2298` — `HACK`：0.7 物品静态 size，新物品需评估
- [ ] **钩爪距离**：`gamecore.cpp:387` — `TODO: fix tweakable variable`（`PHYS_SIZE * 1.50f`）
- [ ] **图鉴未加载提示**：compendium 在物品/僵尸数据未加载时仅显示占位文案，可补数据加载失败日志或重试

### 投票架构（可选，非必须）

- [ ] **`CFGFLAG_VOTE` 引擎接入**：当前用 `CCommandManager` + `AddVoteCommand`；若要对齐 Teeworlds/Alchemist 原生做法，需迁 `IConsole::Register(..., CFGFLAG_VOTE, ...)`
- [ ] **删除 `OnPlayerVoteCommand` 分发链**：各组件已 `return false`，可考虑移除虚函数与 `DispatchPlayerVoteCommand` 中间层（大重构）

---

## P3 — 引擎 / 上游遗留（低优先级）

来自 Teeworlds/DDNet 主干，与 MRPG 功能无直接阻塞：

| 文件 | 备注 |
|------|------|
| `engine/console.h` | 接口 rework、减少虚调用 |
| `engine/shared/console.cpp` | 某段逻辑应删除 |
| `engine/shared/network*.cpp` | 拆分函数、recvinfo、重命名 |
| `engine/shared/network.h` | connless 链表改为动态结构 |
| `engine/shared/snapshot.cpp` | 快照性能优化 |
| `engine/shared/demo.cpp` | 地图 CRC 检查 |
| `engine/shared/datafile.cpp` | header 格式 |
| `engine/server/server.h` | input 缓冲 `m_aInputs[200]` |
| `engine/server/register.cpp` | 注册失败指数退避 |
| `engine/shared/config_variables.h` | 移除废弃宏 |
| `base/vmath.h` | 向量相等用 epsilon 比较 |

---

## 已完成（本轮对话，供对照）

- [x] 投票命令 `AddVoteCommand` 迁移（经济/社交/生活/背包/队伍/邮件/活动/竞技场/Boss/公会）
- [x] 聊天重复命令移除（checkin、auction、enchant、craft、好友操作、大部分 MMO 等）
- [x] NPC 近距：商店宽松浏览 + 购买需靠近商人；强化需靠近铁匠
- [x] 无效 `ccv_*` 提示「该选项当前不可用」
- [x] `marry_accept` 投票入口

---

## 刻意保留的聊天命令（非 TODO）

只读或强交互类，设计为双通道或聊天专用：

`/menu`、`/stats`、`/addstat`、`/guild_info`、`/friend_list`、`/quest`、`/inv`、`/w`、`/whisper`、`/trade_*`、`/login`、`/register`、`/house`（查询）、`/boss`（若仍保留）、竞技场命令（待 P0 决定去留）、`/dungeon_*` 等。

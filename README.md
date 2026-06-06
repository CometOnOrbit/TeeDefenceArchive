# TeeDefense Archive

基于 Teeworlds Archive 0.7.6 的塔防 Mod：守护主塔，抵御僵尸波次。含物品、合成、炮塔与 MySQL 持久化账号。

## 依赖

- CMake 3.16+、C++ 编译器
- OpenSSL、libcurl、zlib、pnglite（见 CMake 输出）
- **MySQL 客户端（必选）**：`libmysqlclient` 或 `mariadb` 开发包
- MySQL/MariaDB 服务端（账号与物品存储）

## 构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target ArchiveServer
```

产物：`build/ArchiveServer`（或 `build-mysql/ArchiveServer`）。

> MySQL 为硬性依赖：未安装客户端时 CMake 将报错退出。

## 数据库初始化

```sql
CREATE DATABASE teedefense CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER 'teedefense'@'localhost' IDENTIFIED BY 'your_password';
GRANT ALL PRIVILEGES ON teedefense.* TO 'teedefense'@'localhost';
FLUSH PRIVILEGES;
```

首次启动时服务器自动创建表：

| 表 | 说明 |
|---|---|
| `tw_Accounts` | 账号：`Username`、`Password`（SHA256 加盐哈希）、`Language`、`Holding`（JSON：镐/斧/剑/炮塔装备） |
| `tw_Items` | 物品：`UserID`、`ItemID`、`Num`、`Extra`（卡牌/零件 JSON） |

旧版 `Sword`/`Pickaxe`/`Axe` 列会在启动时自动迁移到 `Holding` JSON。明文密码在首次成功登录时升级为哈希。

## 配置

复制并编辑 `datasrc/autoexec.cfg.example`：

```
sv_mysql_enable 1          # 必须为 1
sv_mysql_host "127.0.0.1"
sv_mysql_database "teedefense"
sv_mysql_user "teedefense"
sv_mysql_password "your_password"
sv_map "TDef-City"
sv_td_difficulty 1         # 0=简单 1=普通 2=困难
sv_max_tower_health 100
sv_zomb_warmup 10
```

启动示例：

```bash
./ArchiveServer -f autoexec.cfg
```

MySQL 连接失败时服务器将拒绝启动。

## 玩家流程

1. 进服后执行 `/register 用户名 密码` 或 `/login 用户名 密码`（密码 6–63 位）
2. 登录成功后按 **ESC** 打开投票菜单 → 玩家菜单（背包、合成、炮塔等）
3. 守护主塔，清空波次获得分数与掉落（需登录才写入数据库）

### 常用聊天命令

| 命令 | 说明 |
|------|------|
| `/register` `/login` | 注册 / 登录（必选） |
| `/menu` | 打开玩家菜单 |
| `/language zh-cn` | 切换语言 |
| `/craft` `/equip` `/inv` | 合成 / 装备 / 背包 |
| `/community` `/qq` | 社区与赞助信息 |
| `/about` | 模组版本 |

## 难度

| 档位 | 僵尸数量 | 僵尸生命 | AI 强度 | 主塔生命 |
|------|----------|----------|---------|----------|
| 简单 | ×0.75 | ×0.85 | ×0.85 | ×1.25 |
| 普通 | ×1.0 | ×1.0 | ×1.0 | ×1.0 |
| 困难 | ×1.35 | ×1.25 | ×1.15 | ×0.75 |

波次算法沿用经典 TeeDefense 公式（W1/W2 固定，之后按 `Wave%3` 轮换）。

控制台：`td_set_difficulty 0|1|2`（仅第 1 波前）、`td_set_wave`、`td_set_tower_health`。

## 许可

基于 Teeworlds Archive；详见 `license.txt`。

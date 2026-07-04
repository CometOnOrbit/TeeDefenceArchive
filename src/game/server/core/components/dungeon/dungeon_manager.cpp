#include "dungeon_manager.h"

#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/core/components/mmo/mmo_manager.h>
#include <game/server/core/components/worlds/world_manager.h>
#include <game/server/core/tools/event_listener.h>
#include <game/server/entities/character.h>


CDungeonManager::CDungeonManager()
	: m_pGS(0), m_NumTemplates(0), m_NumInstances(0), m_NextInstanceID(1)
{
	mem_zero(m_aInstances, sizeof(m_aInstances));
}

CDungeonManager::~CDungeonManager()
{
	if(m_pGS && m_pGS->Core())
		m_pGS->Core()->Events().Unregister(this);
}

void CDungeonManager::Init(CGameContext *pGameServer)
{
	m_pGS = pGameServer;
	if(m_pGS->Core())
		m_pGS->Core()->Events().Register(this);
	LoadTemplates();
}

void CDungeonManager::OnCharacterDeath(CPlayer *pVictim, CPlayer *pKiller, int Weapon)
{
	(void)Weapon;
	if(!pVictim || !pKiller)
		return;

	// 只处理 bot (MMO mob) 击杀
	if(!pVictim->m_pMMOBotData)
		return;

	OnMobKilled(pVictim->GetCID(), pKiller);
}

// ══════════════════════════════════════════════════════════════
//  Template loader
// ══════════════════════════════════════════════════════════════

void CDungeonManager::LoadTemplates()
{
	m_NumTemplates = 0;

	// ── Goblin Cave — entry level dungeon (Lv 1-5) ─────────
	{
		SDungeonTemplate *pT = &m_aTemplates[m_NumTemplates++];
		pT->m_ID = 1;
		str_copy(pT->m_aName, "Goblin Cave", sizeof(pT->m_aName));
		pT->m_WorldID = 0; // 先用 hub 地图，后续可配专属世界
		pT->m_MinLevel = 1;
		pT->m_MaxLevel = 5;
		pT->m_MaxPlayers = 4;
		pT->m_TimeLimitTicks = 30000;
		pT->m_RewardGold = 200;
		pT->m_RewardExp = 50;

		// Room 1 — 哥布林前哨
		{
			SDungeonRoom &R = pT->m_aRooms[pT->m_NumRooms++];
			R.m_RoomNumber = 1;
			R.m_PlayerSpawnPos = vec2(512.0f, 384.0f);
			R.m_IsBossRoom = false;
			// 3 只哥布林 (def ID 1)
			R.m_aSpawns[R.m_NumSpawns++] = { vec2(400.0f, 300.0f), 1 };
			R.m_aSpawns[R.m_NumSpawns++] = { vec2(550.0f, 350.0f), 1 };
			R.m_aSpawns[R.m_NumSpawns++] = { vec2(480.0f, 420.0f), 1 };
		}

		// Room 2 — 哥布林营地
		{
			SDungeonRoom &R = pT->m_aRooms[pT->m_NumRooms++];
			R.m_RoomNumber = 2;
			R.m_PlayerSpawnPos = vec2(768.0f, 384.0f);
			R.m_IsBossRoom = false;
			R.m_aSpawns[R.m_NumSpawns++] = { vec2(720.0f, 320.0f), 1 };
			R.m_aSpawns[R.m_NumSpawns++] = { vec2(800.0f, 400.0f), 1 };
			R.m_aSpawns[R.m_NumSpawns++] = { vec2(680.0f, 440.0f), 1 };
			R.m_aSpawns[R.m_NumSpawns++] = { vec2(850.0f, 340.0f), 1 };
		}

		// Room 3 — BOSS: 哥布林首领
		{
			SDungeonRoom &R = pT->m_aRooms[pT->m_NumRooms++];
			R.m_RoomNumber = 3;
			R.m_PlayerSpawnPos = vec2(1024.0f, 384.0f);
			R.m_IsBossRoom = true;
			R.m_BossMobDefID = 1; // 暂时用同样的史莱姆 BOSS
			R.m_aSpawns[R.m_NumSpawns++] = { vec2(1024.0f, 384.0f), 1 };
		}
	}

	// ── Ancient Ruins — mid level dungeon (Lv 5-10) ────────
	{
		SDungeonTemplate *pT = &m_aTemplates[m_NumTemplates++];
		pT->m_ID = 2;
		str_copy(pT->m_aName, "Ancient Ruins", sizeof(pT->m_aName));
		pT->m_WorldID = 0;
		pT->m_MinLevel = 5;
		pT->m_MaxLevel = 10;
		pT->m_MaxPlayers = 4;
		pT->m_TimeLimitTicks = 45000;
		pT->m_RewardGold = 500;
		pT->m_RewardExp = 150;

		// Room 1
		{
			SDungeonRoom &R = pT->m_aRooms[pT->m_NumRooms++];
			R.m_RoomNumber = 1;
			R.m_PlayerSpawnPos = vec2(256.0f, 256.0f);
			R.m_IsBossRoom = false;
			for(int i = 0; i < 5; i++)
				R.m_aSpawns[R.m_NumSpawns++] = { vec2(256.0f + i * 64.0f, 300.0f), 1 };
		}

		// Room 2 — BOSS
		{
			SDungeonRoom &R = pT->m_aRooms[pT->m_NumRooms++];
			R.m_RoomNumber = 2;
			R.m_PlayerSpawnPos = vec2(512.0f, 256.0f);
			R.m_IsBossRoom = true;
			R.m_BossMobDefID = 1;
			R.m_aSpawns[R.m_NumSpawns++] = { vec2(512.0f, 256.0f), 1 };
		}
	}
}

// ══════════════════════════════════════════════════════════════
//  Template access
// ══════════════════════════════════════════════════════════════

const SDungeonTemplate *CDungeonManager::GetTemplate(int Index) const
{
	if(Index < 0 || Index >= m_NumTemplates) return 0;
	return &m_aTemplates[Index];
}

const SDungeonTemplate *CDungeonManager::FindTemplateByName(const char *pName) const
{
	for(int i = 0; i < m_NumTemplates; i++)
		if(str_comp_nocase(m_aTemplates[i].m_aName, pName) == 0)
			return &m_aTemplates[i];
	return 0;
}

// ══════════════════════════════════════════════════════════════
//  Instance management
// ══════════════════════════════════════════════════════════════

int CDungeonManager::FindEmptySlot()
{
	for(int i = 0; i < MAX_DUNGEON_INSTANCES; i++)
		if(!m_aInstances[i].m_Active)
			return i;
	return -1;
}

int CDungeonManager::FindWorldHub() const
{
	return 0;
}

bool CDungeonManager::IsInDungeon(int PlayerCID) const
{
	for(int i = 0; i < m_NumInstances; i++)
	{
		const SDungeonInstance &D = m_aInstances[i];
		if(!D.m_Active) continue;
		for(int p = 0; p < D.m_NumPlayers; p++)
			if(D.m_aPlayerCIDs[p] == PlayerCID)
				return true;
	}
	return false;
}

int CDungeonManager::GetPlayerDungeonID(int PlayerCID) const
{
	for(int i = 0; i < m_NumInstances; i++)
	{
		const SDungeonInstance &D = m_aInstances[i];
		if(!D.m_Active) continue;
		for(int p = 0; p < D.m_NumPlayers; p++)
			if(D.m_aPlayerCIDs[p] == PlayerCID)
				return D.m_ID;
	}
	return -1;
}

SDungeonInstance *CDungeonManager::GetDungeon(int DungeonID)
{
	for(int i = 0; i < m_NumInstances; i++)
		if(m_aInstances[i].m_Active && m_aInstances[i].m_ID == DungeonID)
			return &m_aInstances[i];
	return 0;
}

SDungeonInstance *CDungeonManager::GetDungeonByIndex(int Index)
{
	if(Index < 0 || Index >= m_NumInstances) return 0;
	return &m_aInstances[Index];
}

int CDungeonManager::CreateDungeon(int TemplateID, int CreatorCID)
{
	const SDungeonTemplate *pT = 0;
	int TplIdx = -1;
	for(int i = 0; i < m_NumTemplates; i++)
	{
		if(m_aTemplates[i].m_ID == TemplateID)
		{
			pT = &m_aTemplates[i];
			TplIdx = i;
			break;
		}
	}
	if(!pT) return -1;

	CPlayer *pCreator = m_pGS->m_apPlayers[CreatorCID];
	if(!pCreator) return -1;
	if(pCreator->GetStat(AttributeIdentifier::Level) < pT->m_MinLevel || pCreator->GetStat(AttributeIdentifier::Level) > pT->m_MaxLevel)
	{
		m_pGS->SendChatTo(CreatorCID, "你的等级不符合该副本要求。");
		return -2;
	}

	int Slot = FindEmptySlot();
	if(Slot < 0) return -1;

	SDungeonInstance &D = m_aInstances[Slot];
	mem_zero(&D, sizeof(D));
	D.m_ID = m_NextInstanceID++;
	D.m_TemplateID = TplIdx;
	D.m_WorldID = pT->m_WorldID;
	D.m_MinLevel = pT->m_MinLevel;
	D.m_MaxLevel = pT->m_MaxLevel;
	D.m_MaxPlayers = pT->m_MaxPlayers;
	D.m_Active = true;
	D.m_TimeLimitTicks = pT->m_TimeLimitTicks;
	D.m_ElapsedTicks = 0;
	D.m_CurrentRoom = 0;
	D.m_Completed = false;
	D.m_RewardsGiven = false;

	if(m_NumInstances < Slot + 1)
		m_NumInstances = Slot + 1;

	// 创建动态世界实例
	if(m_pGS->Core() && m_pGS->Core()->WorldManager())
	{
		const char *pMapName = "TDef-Deeply"; // 通用副本地图
		int ArenaID = m_pGS->Core()->WorldManager()->CreateArenaWorld(pMapName, "pvp", pMapName);
		if(ArenaID >= 0)
		{
			D.m_ArenaWorldID = ArenaID;
			D.m_WorldID = ArenaID; // 副本逻辑在此世界运行
		}
	}

	// Auto-join creator
	JoinDungeon(CreatorCID, D.m_ID);

	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "创建了副本 #%d (%s)", D.m_ID, pT->m_aName);
	m_pGS->SendChatTo(CreatorCID, aBuf);

	return D.m_ID;
}

bool CDungeonManager::JoinDungeon(int PlayerCID, int DungeonID)
{
	SDungeonInstance *pD = GetDungeon(DungeonID);
	if(!pD || !pD->m_Active) return false;

	CPlayer *pPlayer = m_pGS->m_apPlayers[PlayerCID];
	if(!pPlayer) return false;

	if(pPlayer->GetStat(AttributeIdentifier::Level) < pD->m_MinLevel || pPlayer->GetStat(AttributeIdentifier::Level) > pD->m_MaxLevel)
	{
		m_pGS->SendChatTo(PlayerCID, "你的等级不符合该副本要求。");
		return false;
	}

	if(pD->m_NumPlayers >= pD->m_MaxPlayers)
	{
		m_pGS->SendChatTo(PlayerCID, "副本已满。");
		return false;
	}

	if(IsInDungeon(PlayerCID))
	{
		m_pGS->SendChatTo(PlayerCID, "你已经在副本中。");
		return false;
	}

	pPlayer->ChangeWorld(pD->m_WorldID);
	pD->m_aPlayerCIDs[pD->m_NumPlayers++] = PlayerCID;

	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "加入了副本 #%d", DungeonID);
	m_pGS->SendChatTo(PlayerCID, aBuf);

	// 传送玩家到当前房间的出生点
	const SDungeonTemplate *pT = GetTemplate(pD->m_TemplateID);
	if(pT && pD->m_CurrentRoom < pT->m_NumRooms)
	{
		pPlayer->ChangeWorld(pD->m_WorldID);
		vec2 SpawnPos = pT->m_aRooms[pD->m_CurrentRoom].m_PlayerSpawnPos;
		if(pPlayer->GetCharacter())
			pPlayer->GetCharacter()->GetCore()->m_Pos = SpawnPos;
	}

	return true;
}

bool CDungeonManager::LeaveDungeon(int PlayerCID)
{
	int DungID = GetPlayerDungeonID(PlayerCID);
	if(DungID < 0) return false;

	SDungeonInstance *pD = GetDungeon(DungID);
	if(!pD) return false;

	CPlayer *pPlayer = m_pGS->m_apPlayers[PlayerCID];
	if(pPlayer)
		pPlayer->ChangeWorld(FindWorldHub());

	// Remove player
	int NewCount = 0;
	for(int i = 0; i < pD->m_NumPlayers; i++)
	{
		if(pD->m_aPlayerCIDs[i] != PlayerCID)
			pD->m_aPlayerCIDs[NewCount++] = pD->m_aPlayerCIDs[i];
	}
	pD->m_NumPlayers = NewCount;

	if(pD->m_NumPlayers <= 0)
	{
		CleanupInstance(*pD);
	}

	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "离开了副本 #%d", DungID);
	m_pGS->SendChatTo(PlayerCID, aBuf);
	return true;
}

// ══════════════════════════════════════════════════════════════
//  Gameplay — Room spawning & progression
// ══════════════════════════════════════════════════════════════

void CDungeonManager::SpawnRoomMobs(SDungeonInstance &D, SDungeonRoomState &RState, const SDungeonRoom &Room)
{
	CMMOManager *pMMO = m_pGS->Core()->GetMMOManager();
	if(!pMMO)
	{
		dbg_msg("dungeon", "SpawnRoomMobs: no MMO manager");
		return;
	}

	for(int i = 0; i < Room.m_NumSpawns; i++)
	{
		int MobDefID = Room.m_aSpawns[i].m_MobDefID;
		if(Room.m_IsBossRoom && Room.m_BossMobDefID > 0)
			MobDefID = Room.m_BossMobDefID;

		int CID = pMMO->SpawnMob(MobDefID, Room.m_aSpawns[i].m_Position);
		if(CID >= 0 && RState.m_NumSpawnedBots < MAX_ROOM_SPAWNS)
		{
			RState.m_aSpawnedBotCIDs[RState.m_NumSpawnedBots++] = CID;
		}
	}

	RState.m_Spawned = true;
	dbg_msg("dungeon", "Spawned %d mobs in room %d", RState.m_NumSpawnedBots, Room.m_RoomNumber);
}

void CDungeonManager::CheckRoomCleared(SDungeonInstance &D, int RoomIdx)
{
	if(RoomIdx < 0 || RoomIdx >= MAX_DUNGEON_ROOMS)
		return;
	SDungeonRoomState &RState = D.m_aRoomStates[RoomIdx];
	if(RState.m_Cleared)
		return;

	// 检查该房间所有 spawned bots 是否都还活着
	for(int i = 0; i < RState.m_NumSpawnedBots; i++)
	{
		int CID = RState.m_aSpawnedBotCIDs[i];
		if(CID < 0 || CID >= MAX_CLIENTS)
			continue;
		CPlayer *pBot = m_pGS->m_apPlayers[CID];
		if(pBot && pBot->m_pMMOBotData && pBot->GetCharacter() && pBot->GetCharacter()->IsAlive())
			return; // 还有活着的怪物
	}

	// 所有怪物已消灭
	RState.m_Cleared = true;

	const SDungeonTemplate *pT = GetTemplate(D.m_TemplateID);
	if(!pT) return;

	// 通知玩家
	char aBuf[128];
	if(pT->m_aRooms[RoomIdx].m_IsBossRoom)
	{
		str_format(aBuf, sizeof(aBuf), "BOSS 已被击败！");
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "房间 %d 已清理完毕！", pT->m_aRooms[RoomIdx].m_RoomNumber);
	}

	for(int p = 0; p < D.m_NumPlayers; p++)
	{
		m_pGS->SendChatTo(D.m_aPlayerCIDs[p], aBuf);
	}

	// 如果是 BOSS 房间 → 副本通关
	if(pT->m_aRooms[RoomIdx].m_IsBossRoom)
	{
		D.m_Completed = true;
		D.m_Active = false; // 停止倒计时
		GrantRewards(D);
	}
	else
	{
		// 进入下一个房间
		AdvanceToNextRoom(D);
	}
}

void CDungeonManager::AdvanceToNextRoom(SDungeonInstance &D)
{
	const SDungeonTemplate *pT = GetTemplate(D.m_TemplateID);
	if(!pT) return;

	int NextRoom = D.m_CurrentRoom + 1;
	if(NextRoom >= pT->m_NumRooms)
	{
		// 没有更多房间了 — 自动通关
		D.m_Completed = true;
		D.m_Active = false;
		GrantRewards(D);
		return;
	}

	D.m_CurrentRoom = NextRoom;
	const SDungeonRoom &Room = pT->m_aRooms[NextRoom];

	char aBuf[128];
	if(Room.m_IsBossRoom)
		str_format(aBuf, sizeof(aBuf), "进入 BOSS 房间！");
	else
		str_format(aBuf, sizeof(aBuf), "进入房间 %d", Room.m_RoomNumber);

	// 传送所有玩家到新房间
	TransportPlayers(D, Room.m_PlayerSpawnPos);

	for(int p = 0; p < D.m_NumPlayers; p++)
	{
		m_pGS->SendChatTo(D.m_aPlayerCIDs[p], aBuf);
	}

	// 立即生成怪物
	SDungeonRoomState &RState = D.m_aRoomStates[NextRoom];
	SpawnRoomMobs(D, RState, Room);
}

void CDungeonManager::TransportPlayers(SDungeonInstance &D, vec2 Pos)
{
	for(int p = 0; p < D.m_NumPlayers; p++)
	{
		CPlayer *pPlayer = m_pGS->m_apPlayers[D.m_aPlayerCIDs[p]];
		if(!pPlayer) continue;

		pPlayer->ChangeWorld(D.m_WorldID);
		if(pPlayer->GetCharacter())
		{
			pPlayer->GetCharacter()->GetCore()->m_Pos = Pos;
		}
	}
}

void CDungeonManager::CleanupInstance(SDungeonInstance &D)
{
	// 清理当前房间残留的怪物
	if(D.m_CurrentRoom >= 0 && D.m_CurrentRoom < MAX_DUNGEON_ROOMS)
	{
		SDungeonRoomState &RState = D.m_aRoomStates[D.m_CurrentRoom];
		for(int bi = 0; bi < RState.m_NumSpawnedBots; bi++)
		{
			int BotCID = RState.m_aSpawnedBotCIDs[bi];
			if(BotCID >= 0 && BotCID < MAX_CLIENTS)
			{
				CPlayer *pBot = m_pGS->m_apPlayers[BotCID];
				if(pBot && pBot->GetCharacter())
				{
					pBot->GetCharacter()->Die(0, WEAPON_WORLD);
				}
			}
		}
	}

	// 销毁动态世界
	if(D.m_ArenaWorldID >= 0 && m_pGS->Core() && m_pGS->Core()->WorldManager())
	{
		m_pGS->Core()->WorldManager()->DestroyArenaWorld(D.m_ArenaWorldID);
		D.m_ArenaWorldID = -1;
	}

	D.m_Active = false;
	dbg_msg("dungeon", "Dungeon #%d instance cleaned up", D.m_ID);
}

void CDungeonManager::GrantRewards(SDungeonInstance &D)
{
	if(D.m_RewardsGiven) return;
	D.m_RewardsGiven = true;

	const SDungeonTemplate *pT = GetTemplate(D.m_TemplateID);
	if(!pT) return;

	CMMOManager *pMMO = m_pGS->Core()->GetMMOManager();
	if(!pMMO)
	{
		dbg_msg("dungeon", "GrantRewards: no MMO manager");
		return;
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "═ 副本通关！%s ═", pT->m_aName);

	for(int p = 0; p < D.m_NumPlayers; p++)
	{
		int CID = D.m_aPlayerCIDs[p];
		CPlayer *pPlayer = m_pGS->m_apPlayers[CID];
		if(!pPlayer) continue;

		m_pGS->SendChatTo(CID, aBuf);

		// 金币
		if(pT->m_RewardGold > 0)
		{
			pMMO->AddGold(pPlayer, pT->m_RewardGold);
			str_format(aBuf, sizeof(aBuf), "  金币: +%d", pT->m_RewardGold);
			m_pGS->SendChatTo(CID, aBuf);
		}

		// 经验
		if(pT->m_RewardExp > 0)
		{
			pPlayer->SetStat(AttributeIdentifier::Experience, pPlayer->GetStat(AttributeIdentifier::Experience) + pT->m_RewardExp);
			str_format(aBuf, sizeof(aBuf), "  经验: +%d", pT->m_RewardExp);
			m_pGS->SendChatTo(CID, aBuf);
		}

		// 物品
		for(int i = 0; i < (int)pT->m_RewardItemIDs.size(); i++)
		{
			int ItemID = pT->m_RewardItemIDs[i];
			int Count = pT->m_RewardItemCounts[i];
			if(ItemID >= 0 && pMMO->GiveItem(pPlayer, ItemID, Count))
			{
				str_format(aBuf, sizeof(aBuf), "  物品 #%d: +%d", ItemID, Count);
				m_pGS->SendChatTo(CID, aBuf);
			}
		}

		// 传送回主城
		pPlayer->ChangeWorld(FindWorldHub());
	}

	// 奖励发放完毕后清理副本实例
	CleanupInstance(D);
}

// ══════════════════════════════════════════════════════════════
//  OnMobKilled — 外部回调（game loop 调用）
// ══════════════════════════════════════════════════════════════

void CDungeonManager::OnMobKilled(int VictimCID, CPlayer *pKiller)
{
	if(!pKiller) return;

	// 确认击杀者在一个副本中
	int DungID = GetPlayerDungeonID(pKiller->GetCID());
	if(DungID < 0) return;

	SDungeonInstance *pD = GetDungeon(DungID);
	if(!pD || !pD->m_Active || pD->m_Completed) return;

	// 从所有房间的 spawned bot 列表中找到这个 CID 并标记
	for(int r = 0; r < pD->m_CurrentRoom + 1; r++)
	{
		SDungeonRoomState &RState = pD->m_aRoomStates[r];
		if(RState.m_Cleared) continue;

		for(int i = 0; i < RState.m_NumSpawnedBots; i++)
		{
			if(RState.m_aSpawnedBotCIDs[i] == VictimCID)
			{
				// 检查当前房间是否已清理
				CheckRoomCleared(*pD, r);
				return;
			}
		}
	}
}

// ══════════════════════════════════════════════════════════════
//  Tick
// ══════════════════════════════════════════════════════════════

void CDungeonManager::Tick()
{
	for(int i = 0; i < m_NumInstances; i++)
	{
		SDungeonInstance &D = m_aInstances[i];
		if(!D.m_Active)
			continue;

		// ── 时间限制 ──
		if(D.m_TimeLimitTicks > 0)
		{
			D.m_ElapsedTicks++;
			if(D.m_ElapsedTicks >= D.m_TimeLimitTicks)
			{
				for(int p = 0; p < D.m_NumPlayers; p++)
				{
					int CID = D.m_aPlayerCIDs[p];
					CPlayer *pP = m_pGS->m_apPlayers[CID];
					if(pP)
					{
						pP->ChangeWorld(FindWorldHub());
						m_pGS->SendChatTo(CID, "副本时间到！");
					}
				}
				CleanupInstance(D);
				dbg_msg("dungeon", "Dungeon #%d timed out", D.m_ID);
				continue;
			}
		}

		// ── 检查断线玩家 ──
		for(int p = 0; p < D.m_NumPlayers; p++)
		{
			int CID = D.m_aPlayerCIDs[p];
			CPlayer *pP = m_pGS->m_apPlayers[CID];
			if(!pP || pP->GetAccountId() < 0) // 断线或未登录
			{
				// 从数组中移除
				for(int p2 = p; p2 < D.m_NumPlayers - 1; p2++)
					D.m_aPlayerCIDs[p2] = D.m_aPlayerCIDs[p2 + 1];
				D.m_NumPlayers--;
				p--; // 重新检查当前索引
			}
		}
		if(D.m_NumPlayers <= 0)
		{
			// 全员断线，关闭副本
			CleanupInstance(D);
			continue;
		}

		// ── 首次进入房间时生成怪物 ──
		const SDungeonTemplate *pT = GetTemplate(D.m_TemplateID);
		if(!pT || pT->m_NumRooms <= 0)
		{
			D.m_Active = false;
			continue;
		}

		int CurRoom = D.m_CurrentRoom;
		if(CurRoom < pT->m_NumRooms)
		{
			SDungeonRoomState &RState = D.m_aRoomStates[CurRoom];
			if(!RState.m_Spawned)
			{
				SpawnRoomMobs(D, RState, pT->m_aRooms[CurRoom]);
			}
		}
	}
}

// ══════════════════════════════════════════════════════════════
//  Commands
// ══════════════════════════════════════════════════════════════

void CDungeonManager::RegisterChatCommands(CCommandManager *pManager)
{
	if(!pManager || !m_pGS) return;
	pManager->AddCommand("dungeon", "副本管理: /dungeon create|join|leave|list", "r", ConDungeonCmd, m_pGS);
	pManager->AddCommand("dungeon_list", "查看所有副本模板", "", ConDungeonCmd, m_pGS);
	pManager->AddCommand("dungeon_create", "<名字> - 创建副本", "s", ConDungeonCmd, m_pGS);
	pManager->AddCommand("dungeon_join", "<ID> - 加入副本", "i", ConDungeonCmd, m_pGS);
	pManager->AddCommand("dungeon_leave", "离开副本", "", ConDungeonCmd, m_pGS);
}

void CDungeonManager::ConDungeonCmd(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	CDungeonManager *pDM = pGame->Core()->GetDungeonManager();
	if(!pDM) return;

	const char *pCommandName = pCtx->m_pCommand;

	if(str_comp_nocase(pCommandName, "dungeon_create") == 0)
	{
		const char *pName = pResult->GetString(0);
		if(!pName || pName[0] == '\0')
		{
			pGame->SendChatTo(pCtx->m_ClientID, "用法: /dungeon_create <副本名字>");
			return;
		}
		const SDungeonTemplate *pT = pDM->FindTemplateByName(pName);
		if(!pT)
		{
			pGame->SendChatTo(pCtx->m_ClientID, "未找到副本模板。可用: Goblin Cave, Ancient Ruins");
			return;
		}
		int ID = pDM->CreateDungeon(pT->m_ID, pCtx->m_ClientID);
		if(ID <= 0)
			pGame->SendChatTo(pCtx->m_ClientID, "创建副本失败！");
		return;
	}
	else if(str_comp_nocase(pCommandName, "dungeon_join") == 0)
	{
		int DungID = pResult->GetInteger(0);
		if(DungID <= 0)
		{
			pGame->SendChatTo(pCtx->m_ClientID, "用法: /dungeon_join <id>");
			return;
		}
		if(!pDM->JoinDungeon(pCtx->m_ClientID, DungID))
			pGame->SendChatTo(pCtx->m_ClientID, "加入副本失败！");
		return;
	}
	else if(str_comp_nocase(pCommandName, "dungeon_leave") == 0)
	{
		if(!pDM->LeaveDungeon(pCtx->m_ClientID))
			pGame->SendChatTo(pCtx->m_ClientID, "你不在副本中。");
		return;
	}
	else if(str_comp_nocase(pCommandName, "dungeon_list") == 0)
	{
		// 走下面 list 逻辑
	}

	// ── /dungeon 传统子命令 ──
	const char *pSub = pResult->GetString(0);

	if(str_comp_nocase(pSub, "create") == 0 || str_comp_nocase(pSub, "c") == 0)
	{
		const char *pName = pResult->GetString(1);
		if(!pName || pName[0] == '\0')
		{
			pGame->SendChatTo(pCtx->m_ClientID, "用法: /dungeon create <副本名字>");
			return;
		}
		const SDungeonTemplate *pT = pDM->FindTemplateByName(pName);
		if(!pT)
		{
			pGame->SendChatTo(pCtx->m_ClientID, "未找到副本模板。可用: Goblin Cave, Ancient Ruins");
			return;
		}
		int ID = pDM->CreateDungeon(pT->m_ID, pCtx->m_ClientID);
		if(ID <= 0)
			pGame->SendChatTo(pCtx->m_ClientID, "创建副本失败！");
	}
	else if(str_comp_nocase(pSub, "join") == 0 || str_comp_nocase(pSub, "j") == 0)
	{
		int DungID = pResult->GetInteger(1);
		if(DungID <= 0)
		{
			pGame->SendChatTo(pCtx->m_ClientID, "用法: /dungeon join <id>");
			return;
		}
		if(!pDM->JoinDungeon(pCtx->m_ClientID, DungID))
			pGame->SendChatTo(pCtx->m_ClientID, "加入副本失败！");
	}
	else if(str_comp_nocase(pSub, "leave") == 0 || str_comp_nocase(pSub, "l") == 0)
	{
		if(!pDM->LeaveDungeon(pCtx->m_ClientID))
			pGame->SendChatTo(pCtx->m_ClientID, "你不在副本中。");
	}
	else if(str_comp_nocase(pSub, "list") == 0)
	{
		pGame->SendChatTo(pCtx->m_ClientID, "═══ 副本模板 ═══");
		for(int i = 0; i < pDM->NumTemplates(); i++)
		{
			const SDungeonTemplate *pT = pDM->GetTemplate(i);
			if(!pT) continue;
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "%s (Lv%d-%d, %d人, %d房间, %d金币)",
				pT->m_aName, pT->m_MinLevel, pT->m_MaxLevel, pT->m_MaxPlayers,
				pT->m_NumRooms, pT->m_RewardGold);
			pGame->SendChatTo(pCtx->m_ClientID, aBuf);
		}
		pGame->SendChatTo(pCtx->m_ClientID, "═══ 进行中的副本 ═══");
		for(int i = 0; i < pDM->NumInstances(); i++)
		{
			SDungeonInstance *pD = pDM->GetDungeonByIndex(i);
			if(!pD || !pD->m_Active) continue;
			const SDungeonTemplate *pT = pDM->GetTemplate(pD->m_TemplateID);
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "#%d %s (%d/%d) 房间%d/%d %s",
				pD->m_ID, pT ? pT->m_aName : "?", pD->m_NumPlayers, pD->m_MaxPlayers,
				pD->m_CurrentRoom + 1, pT ? pT->m_NumRooms : 0,
				pD->m_Completed ? "已完成" : "进行中");
			pGame->SendChatTo(pCtx->m_ClientID, aBuf);
		}
	}
	else
	{
		pGame->SendChatTo(pCtx->m_ClientID, "用法: /dungeon create|join|leave|list");
	}
	(void)pResult;
}

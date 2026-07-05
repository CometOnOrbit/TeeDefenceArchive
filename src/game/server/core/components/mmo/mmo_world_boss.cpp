#include "mmo_world_boss.h"
#include "mmo_manager.h"
#include <game/server/data_center.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/entities/character.h>
#include <game/commands.h>
#include <game/server/core/components/vote/vote_menu_types.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/core/attribute_types.h>
#include <engine/shared/config.h>
#include <engine/shared/jsonparser.h>
#include <base/math.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace
{
static std::vector<SWorldBossSpawnDef> gs_aWorldBossSpawns;

static void ParseItemRewards(const json_value &Arr, std::vector<SWorldBossItemReward> &vOut)
{
	if(Arr.type != json_array)
		return;
	for(unsigned i = 0; i < Arr.u.array.length; i++)
	{
		const json_value &Item = Arr[(int)i];
		if(Item.type != json_object)
			continue;
		SWorldBossItemReward Reward;
		if(Item["id"].type == json_integer)
			Reward.m_ItemID = (int)Item["id"].u.integer;
		if(Item["count"].type == json_integer)
			Reward.m_Count = (int)Item["count"].u.integer;
		if(Reward.m_ItemID > 0 && Reward.m_Count > 0)
			vOut.push_back(Reward);
	}
}

static int JsonInt(const json_value &Obj, const char *pKey, int Default)
{
	const json_value &Val = Obj[pKey];
	if(Val.type == json_integer)
		return (int)Val.u.integer;
	return Default;
}

static void ParseBroadcastLine(const json_value &Obj, SWorldBossBroadcastLine &Line)
{
	if(Obj.type != json_object)
		return;
	if(Obj["key"].type == json_string)
		str_copy(Line.m_aKey, Obj["key"].u.string.ptr, sizeof(Line.m_aKey));
	if(Obj["fallback"].type == json_string)
		str_copy(Line.m_aFallback, Obj["fallback"].u.string.ptr, sizeof(Line.m_aFallback));
}

static void ParseRewardTiers(const json_value &Rewards, SWorldBossSpawnDef &Spawn)
{
	if(Rewards.type != json_object)
		return;

	Spawn.m_LeaderboardLines = JsonInt(Rewards, "leaderboard_lines", Spawn.m_LeaderboardLines);

	const json_value &Tiers = Rewards["tiers"];
	if(Tiers.type == json_array && Tiers.u.array.length > 0)
	{
		Spawn.m_vRewardTiers.clear();
		for(unsigned i = 0; i < Tiers.u.array.length; i++)
		{
			const json_value &T = Tiers[(int)i];
			if(T.type != json_object)
				continue;
			SWorldBossRewardTier Tier;
			Tier.m_RankMin = JsonInt(T, "rank_min", 1);
			Tier.m_RankMax = JsonInt(T, "rank_max", Tier.m_RankMin);
			Tier.m_Gold = JsonInt(T, "gold", 0);
			Tier.m_Exp = JsonInt(T, "exp", 0);
			Tier.m_Reputation = JsonInt(T, "reputation", 0);
			if(T["label"].type == json_string)
				str_copy(Tier.m_aLabel, T["label"].u.string.ptr, sizeof(Tier.m_aLabel));
			ParseItemRewards(T["items"], Tier.m_vItems);
			Spawn.m_vRewardTiers.push_back(Tier);
		}
	}

	const json_value &KillBonus = Rewards["kill_bonus"];
	if(KillBonus.type == json_object)
	{
		Spawn.m_KillBonus.m_Reputation = JsonInt(KillBonus, "reputation", 0);
		Spawn.m_KillBonus.m_vItems.clear();
		ParseItemRewards(KillBonus["items"], Spawn.m_KillBonus.m_vItems);
	}
}

static void ApplyDefaultSpawn(SWorldBossSpawnDef &Spawn)
{
	Spawn = {};
	str_copy(Spawn.m_aId, "default_world_boss", sizeof(Spawn.m_aId));
	Spawn.m_Enabled = false;
	Spawn.m_World = 0;
	Spawn.m_MobID = 55;
	Spawn.m_SpawnPos = vec2(960.f, 1408.f);
	Spawn.m_Slot = 63;
	Spawn.m_Schedule.m_InitialMinSec = 300;
	Spawn.m_Schedule.m_InitialMaxSec = 900;
	Spawn.m_Schedule.m_RespawnSec = 3600;
	Spawn.m_LeaderboardLines = 5;

	str_copy(Spawn.m_SpawnMsg.m_aKey, "world_boss.spawn", sizeof(Spawn.m_SpawnMsg.m_aKey));
	str_copy(Spawn.m_SpawnMsg.m_aFallback, "%s 已降临！HP %d/%d", sizeof(Spawn.m_SpawnMsg.m_aFallback));
	str_copy(Spawn.m_KillMsg.m_aKey, "world_boss.kill", sizeof(Spawn.m_KillMsg.m_aKey));
	str_copy(Spawn.m_KillMsg.m_aFallback, "%s 已被击败！击杀者：%s", sizeof(Spawn.m_KillMsg.m_aFallback));
	str_copy(Spawn.m_HpMsg.m_aKey, "world_boss.hp", sizeof(Spawn.m_HpMsg.m_aKey));
	str_copy(Spawn.m_HpMsg.m_aFallback, "%s HP: %d/%d (%d%%)", sizeof(Spawn.m_HpMsg.m_aFallback));
	str_copy(Spawn.m_StatusAliveMsg.m_aKey, "world_boss.status.alive", sizeof(Spawn.m_StatusAliveMsg.m_aKey));
	str_copy(Spawn.m_StatusAliveMsg.m_aFallback, "%s 存活中 | HP: %d/%d (%d%%)", sizeof(Spawn.m_StatusAliveMsg.m_aFallback));
	str_copy(Spawn.m_StatusWaitingMsg.m_aKey, "world_boss.status.waiting", sizeof(Spawn.m_StatusWaitingMsg.m_aKey));
	str_copy(Spawn.m_StatusWaitingMsg.m_aFallback, "%s 已消失，约 %d 秒后刷新", sizeof(Spawn.m_StatusWaitingMsg.m_aFallback));
	str_copy(Spawn.m_StatusSoonMsg.m_aKey, "world_boss.status.soon", sizeof(Spawn.m_StatusSoonMsg.m_aKey));
	str_copy(Spawn.m_StatusSoonMsg.m_aFallback, "%s 即将降临…", sizeof(Spawn.m_StatusSoonMsg.m_aFallback));
	str_copy(Spawn.m_ClanMsg.m_aKey, "world_boss.clan", sizeof(Spawn.m_ClanMsg.m_aKey));
	str_copy(Spawn.m_ClanMsg.m_aFallback, "%s: %d/%d [%d%%]", sizeof(Spawn.m_ClanMsg.m_aFallback));

	SWorldBossRewardTier Tier1;
	Tier1.m_RankMin = 1;
	Tier1.m_RankMax = 1;
	Tier1.m_Gold = 5000;
	Tier1.m_Exp = 2000;
	Tier1.m_Reputation = 50;
	str_copy(Tier1.m_aLabel, "🏆", sizeof(Tier1.m_aLabel));
	Tier1.m_vItems.push_back({1, 1});
	Spawn.m_vRewardTiers.push_back(Tier1);

	SWorldBossRewardTier Tier2 = Tier1;
	Tier2.m_RankMin = 2;
	Tier2.m_RankMax = 2;
	Tier2.m_Gold = 3000;
	Tier2.m_Exp = 1000;
	Tier2.m_Reputation = 30;
	str_copy(Tier2.m_aLabel, "🥈", sizeof(Tier2.m_aLabel));
	Spawn.m_vRewardTiers.push_back(Tier2);

	SWorldBossRewardTier Tier3 = Tier2;
	Tier3.m_RankMin = 3;
	Tier3.m_RankMax = 3;
	Tier3.m_Gold = 2000;
	Tier3.m_Exp = 500;
	Tier3.m_Reputation = 20;
	str_copy(Tier3.m_aLabel, "🥉", sizeof(Tier3.m_aLabel));
	Spawn.m_vRewardTiers.push_back(Tier3);

	SWorldBossRewardTier Tier4;
	Tier4.m_RankMin = 4;
	Tier4.m_RankMax = 999;
	Tier4.m_Gold = 500;
	Tier4.m_Exp = 200;
	Tier4.m_Reputation = 5;
	str_copy(Tier4.m_aLabel, "💫", sizeof(Tier4.m_aLabel));
	Spawn.m_vRewardTiers.push_back(Tier4);

	Spawn.m_KillBonus.m_Reputation = 100;
	Spawn.m_KillBonus.m_vItems.push_back({1, 2});
}

static bool ParseSpawnEntry(const json_value &El, SWorldBossSpawnDef &Spawn)
{
	if(El.type != json_object)
		return false;

	ApplyDefaultSpawn(Spawn);

	if(El["id"].type == json_string)
		str_copy(Spawn.m_aId, El["id"].u.string.ptr, sizeof(Spawn.m_aId));
	if(El["enabled"].type == json_boolean)
		Spawn.m_Enabled = El["enabled"].u.boolean != 0;
	Spawn.m_World = JsonInt(El, "world", Spawn.m_World);
	Spawn.m_MobID = JsonInt(El, "mob_id", Spawn.m_MobID);
	Spawn.m_Slot = JsonInt(El, "slot", Spawn.m_Slot);

	const json_value &SpawnPos = El["spawn"];
	if(SpawnPos.type == json_object)
	{
		if(SpawnPos["x"].type == json_integer)
			Spawn.m_SpawnPos.x = (float)SpawnPos["x"].u.integer;
		if(SpawnPos["y"].type == json_integer)
			Spawn.m_SpawnPos.y = (float)SpawnPos["y"].u.integer;
	}

	if(El["display_name"].type == json_string)
		str_copy(Spawn.m_aDisplayName, El["display_name"].u.string.ptr, sizeof(Spawn.m_aDisplayName));
	if(El["display_name_key"].type == json_string)
		str_copy(Spawn.m_aDisplayNameKey, El["display_name_key"].u.string.ptr, sizeof(Spawn.m_aDisplayNameKey));

	const json_value &Schedule = El["schedule"];
	if(Schedule.type == json_object)
	{
		Spawn.m_Schedule.m_InitialMinSec = JsonInt(Schedule, "initial_min_sec", Spawn.m_Schedule.m_InitialMinSec);
		Spawn.m_Schedule.m_InitialMaxSec = JsonInt(Schedule, "initial_max_sec", Spawn.m_Schedule.m_InitialMaxSec);
		Spawn.m_Schedule.m_RespawnSec = JsonInt(Schedule, "respawn_sec", Spawn.m_Schedule.m_RespawnSec);
		if(Schedule["first_spawn_min_sec"].type == json_integer)
			Spawn.m_Schedule.m_InitialMinSec = (int)Schedule["first_spawn_min_sec"].u.integer;
		if(Schedule["first_spawn_max_sec"].type == json_integer)
			Spawn.m_Schedule.m_InitialMaxSec = (int)Schedule["first_spawn_max_sec"].u.integer;
		if(Schedule["respawn_ticks"].type == json_integer)
			Spawn.m_Schedule.m_RespawnSec = maximum(1, (int)Schedule["respawn_ticks"].u.integer / 10);
	}

	const json_value &Broadcast = El["broadcast"];
	if(Broadcast.type == json_object)
	{
		ParseBroadcastLine(Broadcast["spawn"], Spawn.m_SpawnMsg);
		ParseBroadcastLine(Broadcast["kill"], Spawn.m_KillMsg);
		ParseBroadcastLine(Broadcast["hp"], Spawn.m_HpMsg);
		ParseBroadcastLine(Broadcast["status_alive"], Spawn.m_StatusAliveMsg);
		ParseBroadcastLine(Broadcast["status_waiting"], Spawn.m_StatusWaitingMsg);
		ParseBroadcastLine(Broadcast["status_soon"], Spawn.m_StatusSoonMsg);
		ParseBroadcastLine(Broadcast["clan"], Spawn.m_ClanMsg);
	}

	ParseRewardTiers(El["rewards"], Spawn);
	return Spawn.m_World >= 0 && Spawn.m_MobID > 0;
}

static void LoadAllSpawns(IStorage *pStorage)
{
	gs_aWorldBossSpawns.clear();
	if(!pStorage)
		return;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_content/mmo/world_boss.json", pStorage);
	if(!pRoot)
	{
		dbg_msg("world_boss", "world_boss.json: %s", Parser.Error());
		return;
	}

	const json_value &Spawns = (*pRoot)["spawns"];
	if(Spawns.type == json_array)
	{
		for(unsigned i = 0; i < Spawns.u.array.length; i++)
		{
			SWorldBossSpawnDef Spawn;
			if(!ParseSpawnEntry(Spawns[(int)i], Spawn))
				continue;
			gs_aWorldBossSpawns.push_back(Spawn);
		}
	}
	else
	{
		SWorldBossSpawnDef Spawn;
		ApplyDefaultSpawn(Spawn);
		const json_value &Boss = (*pRoot)["boss"];
		if(Boss.type == json_object)
		{
			Spawn.m_MobID = JsonInt(Boss, "mob_id", Spawn.m_MobID);
			Spawn.m_World = JsonInt(Boss, "world", Spawn.m_World);
			Spawn.m_Slot = JsonInt(Boss, "slot", Spawn.m_Slot);
			if(Boss["x"].type == json_integer)
				Spawn.m_SpawnPos.x = (float)Boss["x"].u.integer;
			if(Boss["y"].type == json_integer)
				Spawn.m_SpawnPos.y = (float)Boss["y"].u.integer;
			Spawn.m_Schedule.m_InitialMinSec = JsonInt(Boss, "first_spawn_min_sec", Spawn.m_Schedule.m_InitialMinSec);
			Spawn.m_Schedule.m_InitialMaxSec = JsonInt(Boss, "first_spawn_max_sec", Spawn.m_Schedule.m_InitialMaxSec);
			if(Boss["respawn_sec"].type == json_integer)
				Spawn.m_Schedule.m_RespawnSec = JsonInt(Boss, "respawn_sec", Spawn.m_Schedule.m_RespawnSec);
			else if(Boss["respawn_ticks"].type == json_integer)
				Spawn.m_Schedule.m_RespawnSec = maximum(1, JsonInt(Boss, "respawn_ticks", 3600) / 10);
		}
		ParseRewardTiers((*pRoot)["rewards"], Spawn);
		Spawn.m_Enabled = true;
		gs_aWorldBossSpawns.push_back(Spawn);
	}

	dbg_msg("world_boss", "Loaded %d world boss spawn definitions", (int)gs_aWorldBossSpawns.size());
}
}

void CWorldBossManager::OnPreInit()
{
	LoadAllSpawns(Storage());
}

CWorldBossManager::CWorldBossManager()
{
}

CWorldBossManager::~CWorldBossManager()
{
	if(Core())
		Core()->Events().Unregister(this);
}

void CWorldBossManager::ResolveSpawnForWorld(int WorldID)
{
	m_pSpawn = nullptr;
	m_pMobDef = nullptr;
	for(const SWorldBossSpawnDef &Spawn : gs_aWorldBossSpawns)
	{
		if(Spawn.m_World == WorldID && Spawn.m_Enabled)
		{
			m_pSpawn = &Spawn;
			m_pMobDef = SMMOMobDef::Get(Spawn.m_MobID);
			if(!m_pMobDef)
				dbg_msg("world_boss", "world %d spawn '%s': unknown mob_id %d", WorldID, Spawn.m_aId, Spawn.m_MobID);
			return;
		}
	}
}

void CWorldBossManager::OnInitWorld(const char *pWhereLocalWorld)
{
	(void)pWhereLocalWorld;
	if(Core())
	{
		Core()->Events().Register(this);
		m_WorldID = GS()->GetWorldID();
	}

	ResolveSpawnForWorld(m_WorldID);
	if(!IsEnabled())
	{
		dbg_msg("world_boss", "world %d: no enabled world boss spawn configured", m_WorldID);
		return;
	}

	ScheduleNextSpawn(true);
	dbg_msg("world_boss", "world %d: spawn '%s' mob_id=%d at (%.0f,%.0f), next in %d ticks",
		m_WorldID, m_pSpawn->m_aId, m_pSpawn->m_MobID,
		m_pSpawn->m_SpawnPos.x, m_pSpawn->m_SpawnPos.y,
		GetNextSpawnInTicks());
}

void CWorldBossManager::OnShutdown()
{
	if(Core())
		Core()->Events().Unregister(this);
	if(m_IsAlive)
		DespawnBoss();
}

int CWorldBossManager::RespawnDelayTicks() const
{
	if(!Server() || !m_pSpawn)
		return Server() ? Server()->TickSpeed() * 3600 : 36000;
	return maximum(Server()->TickSpeed(), m_pSpawn->m_Schedule.m_RespawnSec * Server()->TickSpeed());
}

void CWorldBossManager::ScheduleNextSpawn(bool bInitial)
{
	if(!Server() || !m_pSpawn)
		return;

	if(bInitial)
	{
		const int MinSec = maximum(1, m_pSpawn->m_Schedule.m_InitialMinSec);
		const int MaxSec = maximum(MinSec, m_pSpawn->m_Schedule.m_InitialMaxSec);
		const int Sec = MinSec + random_int() % (MaxSec - MinSec + 1);
		m_NextSpawnTick = Server()->Tick() + Sec * Server()->TickSpeed();
	}
	else
	{
		m_NextSpawnTick = Server()->Tick() + RespawnDelayTicks();
	}
}

const char *CWorldBossManager::GetBossDisplayName(int ClientID) const
{
	if(!m_pSpawn)
		return "World Boss";
	if(m_pSpawn->m_aDisplayNameKey[0] && GS())
		return GS()->Loc(ClientID, m_pSpawn->m_aDisplayNameKey, m_pSpawn->m_aDisplayName);
	if(m_pSpawn->m_aDisplayName[0])
		return m_pSpawn->m_aDisplayName;
	if(m_pMobDef && m_pMobDef->m_aName[0])
		return m_pMobDef->m_aName;
	return "World Boss";
}

void CWorldBossManager::FormatBroadcast(int ClientID, char *pBuf, int BufSize, const SWorldBossBroadcastLine &Line, ...) const
{
	if(!pBuf || BufSize <= 0)
		return;
	pBuf[0] = 0;
	if(!GS())
		return;

	char aFmt[256];
	if(Line.m_aKey[0])
		GS()->LocFormat(aFmt, sizeof(aFmt), ClientID, Line.m_aKey, Line.m_aFallback[0] ? Line.m_aFallback : "%s");
	else
		str_copy(aFmt, Line.m_aFallback[0] ? Line.m_aFallback : "%s", sizeof(aFmt));

	va_list Args;
	va_start(Args, Line);
	vsnprintf(pBuf, BufSize, aFmt, Args);
	va_end(Args);
	pBuf[BufSize - 1] = 0;
}

void CWorldBossManager::OnTick()
{
	if(!IsEnabled())
		return;

	if(!m_IsAlive && Server()->Tick() >= m_NextSpawnTick)
	{
		SpawnBoss();
		return;
	}

	if(m_IsAlive && m_BossClientID >= 0)
	{
		CPlayer *pBoss = GS()->m_apPlayers[m_BossClientID];
		if(pBoss && pBoss->GetCharacter())
		{
			m_BossHP = maximum(1, pBoss->GetCharacter()->GetHealth());
			m_BossMaxHP = pBoss->GetCharacter()->GetMaxHealth();
			if((Server()->Tick() % 100) == 0)
				BroadcastBossStatus();
		}
		else
		{
			dbg_msg("world_boss", "Boss character lost, despawning");
			DespawnBoss();
			ScheduleNextSpawn(false);
		}
	}
}

void CWorldBossManager::OnCharacterDeath(CPlayer *pVictim, CPlayer *pKiller, int Weapon)
{
	(void)Weapon;
	if(!pVictim || !pVictim->m_IsWorldBoss || !m_pSpawn)
		return;

	DistributeRewards(pKiller);
	DespawnBoss();
	ScheduleNextSpawn(false);

	char aBuf[256];
	FormatBroadcast(-1, aBuf, sizeof(aBuf), m_pSpawn->m_KillMsg,
		GetBossDisplayName(-1),
		pKiller ? Server()->ClientName(pKiller->GetCID()) : "未知");
	GS()->BroadcastWorldMsg(m_WorldID, CGameContext::BROADCAST_PRIORITY_CRITICAL, 300, aBuf);
}

void CWorldBossManager::RecordDamage(int BossCID, int AttackerCID, int Damage)
{
	if(BossCID != m_BossClientID || !m_IsAlive || Damage <= 0)
		return;

	CPlayer *pAttacker = GS()->m_apPlayers[AttackerCID];
	if(!pAttacker || pAttacker->IsDummy())
		return;

	for(auto &Entry : m_aDamage)
	{
		if(Entry.ClientID == AttackerCID)
		{
			Entry.Damage += Damage;
			return;
		}
	}

	SPlayerDamage Entry;
	Entry.ClientID = AttackerCID;
	Entry.Damage = Damage;
	Entry.AccountID = pAttacker->GetAccountId();
	m_aDamage.push_back(Entry);
}

bool CWorldBossManager::IsWorldBoss(CPlayer *pPlayer) const
{
	return pPlayer && pPlayer->m_IsWorldBoss;
}

bool CWorldBossManager::IsWorldBossCharacter(CCharacter *pChar) const
{
	return pChar && pChar->GetPlayer() && pChar->GetPlayer()->m_IsWorldBoss;
}

int CWorldBossManager::GetNextSpawnInTicks() const
{
	if(!IsEnabled() || m_IsAlive || !Server())
		return 0;
	return maximum(0, m_NextSpawnTick - Server()->Tick());
}

bool CWorldBossManager::GetBossPos(vec2 *pOut) const
{
	if(!pOut || m_BossClientID < 0)
		return false;
	CPlayer *pBoss = GS()->m_apPlayers[m_BossClientID];
	if(!pBoss || !pBoss->GetCharacter())
		return false;
	*pOut = pBoss->GetCharacter()->GetPos();
	return true;
}

void CWorldBossManager::SpawnBoss()
{
	if(!IsEnabled() || !m_pSpawn)
		return;

	CMMOManager *pMMO = Core() ? Core()->GetMMOManager() : nullptr;
	if(!pMMO)
		return;

	const int CID = pMMO->SpawnMob(m_pSpawn->m_MobID, m_pSpawn->m_SpawnPos, m_pSpawn->m_Slot);
	if(CID < 0)
	{
		dbg_msg("world_boss", "SpawnBoss: SpawnMob failed for mob_id=%d", m_pSpawn->m_MobID);
		m_NextSpawnTick = Server()->Tick() + RespawnDelayTicks() / 2;
		return;
	}

	CPlayer *pBoss = GS()->m_apPlayers[CID];
	if(!pBoss || !pBoss->GetCharacter())
	{
		DespawnBoss();
		m_NextSpawnTick = Server()->Tick() + RespawnDelayTicks() / 2;
		return;
	}

	pBoss->m_IsWorldBoss = true;
	m_IsAlive = true;
	m_BossClientID = CID;
	m_BossHP = maximum(1, pBoss->GetCharacter()->GetHealth());
	m_BossMaxHP = pBoss->GetCharacter()->GetMaxHealth();
	ResetDamageTracking();

	char aBuf[256];
	FormatBroadcast(-1, aBuf, sizeof(aBuf), m_pSpawn->m_SpawnMsg,
		GetBossDisplayName(-1), m_BossHP, m_BossMaxHP);
	GS()->BroadcastWorldMsg(m_WorldID, CGameContext::BROADCAST_PRIORITY_CRITICAL, 300, aBuf);

	dbg_msg("world_boss", "World Boss '%s' spawned at (%.0f,%.0f) slot %d HP %d/%d",
		m_pSpawn->m_aId, m_pSpawn->m_SpawnPos.x, m_pSpawn->m_SpawnPos.y,
		CID, m_BossHP, m_BossMaxHP);
}

void CWorldBossManager::DespawnBoss()
{
	if(m_BossClientID < 0)
		return;

	CGameContext *pGS = GS();
	if(!pGS)
		return;

	CPlayer *pBoss = pGS->m_apPlayers[m_BossClientID];
	if(pBoss)
	{
		pBoss->m_IsWorldBoss = false;
		delete pBoss->m_pMMOBotData;
		pBoss->m_pMMOBotData = 0;
		pGS->Server()->DummyRemove(m_BossClientID);
		delete pGS->m_apPlayers[m_BossClientID];
		pGS->m_apPlayers[m_BossClientID] = 0;
	}

	m_IsAlive = false;
	m_BossClientID = -1;
	m_BossHP = 0;
	m_BossMaxHP = 0;
}

void CWorldBossManager::BroadcastBossStatus()
{
	if(!m_IsAlive || m_BossClientID < 0 || !m_pSpawn)
		return;

	CPlayer *pBoss = GS()->m_apPlayers[m_BossClientID];
	if(!pBoss)
		return;
	CCharacter *pChr = pBoss->GetCharacter();
	if(!pChr)
		return;

	m_BossHP = pChr->GetHealth();
	m_BossMaxHP = pChr->GetMaxHealth();
	const int HpPct = m_BossMaxHP > 0 ? m_BossHP * 100 / m_BossMaxHP : 0;

	char aClan[64];
	FormatBroadcast(m_BossClientID, aClan, sizeof(aClan), m_pSpawn->m_ClanMsg,
		GetBossDisplayName(m_BossClientID), m_BossHP, m_BossMaxHP, HpPct);
	GS()->Server()->SetClientClan(m_BossClientID, aClan);

	if((Server()->Tick() % 30) == 1)
	{
		char aBuf[128];
		FormatBroadcast(-1, aBuf, sizeof(aBuf), m_pSpawn->m_HpMsg,
			GetBossDisplayName(-1), m_BossHP, m_BossMaxHP, HpPct);
		GS()->BroadcastWorldMsg(m_WorldID, CGameContext::BROADCAST_PRIORITY_HIGH, 30, aBuf);
	}
}

void CWorldBossManager::ResetDamageTracking()
{
	m_aDamage.clear();
}

const SWorldBossRewardTier *CWorldBossManager::FindRewardTier(int Rank) const
{
	if(!m_pSpawn)
		return nullptr;
	for(const SWorldBossRewardTier &Tier : m_pSpawn->m_vRewardTiers)
	{
		if(Rank >= Tier.m_RankMin && Rank <= Tier.m_RankMax)
			return &Tier;
	}
	return nullptr;
}

void CWorldBossManager::AppendItemRewardSummary(char *pBuf, int BufSize, const std::vector<SWorldBossItemReward> &vItems) const
{
	if(!pBuf || BufSize <= 0)
		return;
	pBuf[0] = 0;

	bool First = true;
	for(const SWorldBossItemReward &Item : vItems)
	{
		if(Item.m_ItemID <= 0 || Item.m_Count <= 0)
			continue;
		const char *pName = GS() ? GS()->LocItemName(-1, Item.m_ItemID) : "?";
		if(!First)
			str_append(pBuf, " + ", BufSize);
		First = false;

		char aPart[64];
		str_format(aPart, sizeof(aPart), "%d× %s", Item.m_Count, pName);
		str_append(pBuf, aPart, BufSize);
	}
}

void CWorldBossManager::GrantReward(CMMOManager *pMMO, CPlayer *pPlayer, const SWorldBossRewardTier &Tier,
	int Rank, int Damage, int DamagePct) const
{
	(void)Rank;
	if(!pMMO || !pPlayer || pPlayer->GetAccountId() <= 0)
		return;

	if(Tier.m_Gold > 0)
		pMMO->AddGold(pPlayer, Tier.m_Gold);
	if(Tier.m_Exp > 0)
		pMMO->AddExperience(pPlayer, Tier.m_Exp);
	if(Tier.m_Reputation > 0)
		pPlayer->m_MMOReputation += Tier.m_Reputation;
	for(const SWorldBossItemReward &Item : Tier.m_vItems)
	{
		if(Item.m_ItemID > 0 && Item.m_Count > 0)
			pMMO->GiveItem(pPlayer, Item.m_ItemID, Item.m_Count, 0);
	}

	char aItems[128];
	AppendItemRewardSummary(aItems, sizeof(aItems), Tier.m_vItems);

	char aBuf[256];
	if(aItems[0])
	{
		GS()->LocFormat(aBuf, sizeof(aBuf), pPlayer->GetCID(), "world_boss.reward.tier_items",
			"%s 对世界 Boss 造成了 %d 伤害 (%d%%)，获得 %d金币 + %d经验 + %d声望 + %s",
			Tier.m_aLabel[0] ? Tier.m_aLabel : "", Damage, DamagePct,
			Tier.m_Gold, Tier.m_Exp, Tier.m_Reputation, aItems);
	}
	else
	{
		GS()->LocFormat(aBuf, sizeof(aBuf), pPlayer->GetCID(), "world_boss.reward.tier",
			"%s 对世界 Boss 造成了 %d 伤害 (%d%%)，获得 %d金币 + %d经验 + %d声望",
			Tier.m_aLabel[0] ? Tier.m_aLabel : "", Damage, DamagePct,
			Tier.m_Gold, Tier.m_Exp, Tier.m_Reputation);
	}
	GS()->SendChatTo(pPlayer->GetCID(), aBuf);
}

void CWorldBossManager::GrantKillBonus(CMMOManager *pMMO, CPlayer *pKiller) const
{
	if(!pMMO || !pKiller || pKiller->GetAccountId() <= 0 || !m_pSpawn)
		return;

	const SWorldBossKillBonus &Bonus = m_pSpawn->m_KillBonus;
	if(Bonus.m_Reputation <= 0 && Bonus.m_vItems.empty())
		return;

	if(Bonus.m_Reputation > 0)
		pKiller->m_MMOReputation += Bonus.m_Reputation;
	for(const SWorldBossItemReward &Item : Bonus.m_vItems)
	{
		if(Item.m_ItemID > 0 && Item.m_Count > 0)
			pMMO->GiveItem(pKiller, Item.m_ItemID, Item.m_Count, 0);
	}

	char aItems[128];
	AppendItemRewardSummary(aItems, sizeof(aItems), Bonus.m_vItems);
	char aBuf[256];
	if(aItems[0])
	{
		GS()->LocFormat(aBuf, sizeof(aBuf), pKiller->GetCID(), "world_boss.reward.kill_items",
			"⚔️ 你击杀世界 Boss！额外获得 %d声望 + %s", Bonus.m_Reputation, aItems);
	}
	else
	{
		GS()->LocFormat(aBuf, sizeof(aBuf), pKiller->GetCID(), "world_boss.reward.kill",
			"⚔️ 你击杀世界 Boss！额外获得 %d声望", Bonus.m_Reputation);
	}
	GS()->SendChatTo(pKiller->GetCID(), aBuf);
}

void CWorldBossManager::DistributeRewards(CPlayer *pKiller)
{
	CMMOManager *pMMO = Core() ? Core()->GetMMOManager() : nullptr;
	if(!pMMO || !m_pSpawn)
	{
		dbg_msg("world_boss", "Cannot distribute rewards: no MMO manager or spawn config");
		return;
	}

	std::sort(m_aDamage.begin(), m_aDamage.end(),
		[](const SPlayerDamage &A, const SPlayerDamage &B) {
			return A.Damage > B.Damage;
		});

	int TotalDamage = 0;
	for(const SPlayerDamage &Entry : m_aDamage)
		TotalDamage += Entry.Damage;

	for(size_t i = 0; i < m_aDamage.size(); i++)
	{
		const int Rank = (int)(i + 1);
		const SWorldBossRewardTier *pTier = FindRewardTier(Rank);
		if(!pTier)
			continue;

		CPlayer *pPlayer = GS()->m_apPlayers[m_aDamage[i].ClientID];
		if(!pPlayer)
			continue;

		const int Pct = TotalDamage > 0 ? m_aDamage[i].Damage * 100 / TotalDamage : 0;
		GrantReward(pMMO, pPlayer, *pTier, Rank, m_aDamage[i].Damage, Pct);
	}

	GrantKillBonus(pMMO, pKiller);

	char aBuf[512];
	GS()->LocFormat(aBuf, sizeof(aBuf), -1, "world_boss.leaderboard.header", "=== 世界 Boss 讨伐结果 ===");
	GS()->BroadcastWorldMsg(m_WorldID, CGameContext::BROADCAST_PRIORITY_CRITICAL, 300, aBuf);

	const int Lines = maximum(1, m_pSpawn->m_LeaderboardLines);
	for(int i = 0; i < minimum(Lines, (int)m_aDamage.size()); i++)
	{
		const int Pct = TotalDamage > 0 ? m_aDamage[i].Damage * 100 / TotalDamage : 0;
		GS()->LocFormat(aBuf, sizeof(aBuf), -1, "world_boss.leaderboard.line",
			"#%d. %s — %d 伤害 (%d%%)",
			i + 1, Server()->ClientName(m_aDamage[i].ClientID), m_aDamage[i].Damage, Pct);
		GS()->BroadcastWorldMsg(m_WorldID, CGameContext::BROADCAST_PRIORITY_CRITICAL, 300, aBuf);
	}

	dbg_msg("world_boss", "Rewards distributed: %d participants, total damage %d",
		(int)m_aDamage.size(), TotalDamage);
}

void CWorldBossManager::SendBossStatusChat(int ClientID) const
{
	if(!IsEnabled() || !m_pSpawn)
	{
		if(GS())
			GS()->SendChatLoc(ClientID, "world_boss.disabled", "世界 Boss 系统未启用");
		return;
	}

	char aBuf[256];
	auto *pSelf = const_cast<CWorldBossManager *>(this);
	if(m_IsAlive)
	{
		const int HpPct = m_BossMaxHP > 0 ? m_BossHP * 100 / m_BossMaxHP : 0;
		pSelf->FormatBroadcast(ClientID, aBuf, sizeof(aBuf), m_pSpawn->m_StatusAliveMsg,
			GetBossDisplayName(ClientID), m_BossHP, m_BossMaxHP, HpPct);
	}
	else
	{
		const int Sec = Server() ? GetNextSpawnInTicks() / maximum(1, Server()->TickSpeed()) : 0;
		if(Sec > 0)
		{
			pSelf->FormatBroadcast(ClientID, aBuf, sizeof(aBuf), m_pSpawn->m_StatusWaitingMsg,
				GetBossDisplayName(ClientID), Sec);
		}
		else
		{
			pSelf->FormatBroadcast(ClientID, aBuf, sizeof(aBuf), m_pSpawn->m_StatusSoonMsg,
				GetBossDisplayName(ClientID));
		}
	}
	GS()->SendChatTo(ClientID, aBuf);
}

void CWorldBossManager::RegisterBossCommands()
{
	CCommandManager *pManager = GS() ? GS()->CommandManager() : nullptr;
	if(!pManager)
		return;

	pManager->AddCommand("boss", "查看世界 Boss 状态", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetWorldBossManager())
			return;
		pG->Core()->GetWorldBossManager()->SendBossStatusChat(pCtx->m_ClientID);
		(void)pR;
	}, GS());

	pManager->AddCommand("boss_tp", "传送到世界 Boss 位置", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetWorldBossManager())
			return;

		CWorldBossManager *pWB = pG->Core()->GetWorldBossManager();
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || !pP->GetCharacter())
			return;

		if(!pWB->IsBossAlive())
		{
			pG->SendChatLoc(pCtx->m_ClientID, "world_boss.cmd.no_boss", "❌ 世界 Boss 当前不存在。");
			return;
		}

		vec2 BossPos;
		if(pWB->GetBossPos(&BossPos))
		{
			pP->GetCharacter()->SetCharacterPos(BossPos + vec2(64.f, 0.f));
			pG->SendChatLoc(pCtx->m_ClientID, "world_boss.cmd.tp_ok", "🚀 已传送到世界 Boss 位置！");
		}
		(void)pR;
	}, GS());
}

void CWorldBossManager::RegisterBossVoteCommands(CCommandManager *pManager)
{
	if(!pManager)
		return;
	CGameContext *pGame = GS();

	VOTE_CMD(pManager, "boss", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetWorldBossManager())
			return;
		pG->Core()->GetWorldBossManager()->SendBossStatusChat(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "boss_tp", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetWorldBossManager())
			return;

		CWorldBossManager *pWB = pG->Core()->GetWorldBossManager();
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || !pP->GetCharacter())
			return;

		if(!pWB->IsBossAlive())
		{
			pG->SendChatLoc(pCtx->m_ClientID, "world_boss.cmd.no_boss", "❌ 世界 Boss 当前不存在。");
			return;
		}

		vec2 BossPos;
		if(pWB->GetBossPos(&BossPos))
		{
			pP->GetCharacter()->SetCharacterPos(BossPos + vec2(64.f, 0.f));
			pG->SendChatLoc(pCtx->m_ClientID, "world_boss.cmd.tp_ok", "🚀 已传送到世界 Boss 位置！");
		}
		(void)pR;
	}, pGame);
}

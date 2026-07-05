#ifndef GAME_SERVER_CORE_COMPONENTS_NPCS_NPC_MANAGER_H
#define GAME_SERVER_CORE_COMPONENTS_NPCS_NPC_MANAGER_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/server/core/tworld_component.h>

class CCharacter;
class CPlayer;

enum
{
	MAX_NPC_DEFS = 32,
	MAX_SPAWN_DEFS = 64,
	NPC_KEY_LEN = 32,
	NPC_HAMMER_RANGE = 64,
	NPC_SLOT_FIRST = MAX_CLIENTS - MAX_SPAWN_DEFS,
};

// ── NPC Identity (who) ──────────────────────────────────────
struct SNpcDef
{
	char m_aId[NPC_KEY_LEN];
	char m_aNameKey[48];
	char m_aSkinBody[24];
	char m_aSkinDecoration[24];
	int m_Emote;
	int m_ClientID; // -1 if not spawned (used by spawn table)
};

// ── NPC Spawn (where/when) ──────────────────────────────────
struct SSpawnDef
{
	char m_aId[NPC_KEY_LEN];
	char m_aNpcId[NPC_KEY_LEN]; // references SNpcDef.m_aId
	int m_World;
	float m_X;
	float m_Y;
	float m_Radius;
	bool m_Static;
	char m_aDialogTag[48];
	char m_aConditions[512]; // JSON string of conditions (stored for eval)
	int m_ClientID; // spawned bot client id, -1 if not spawned
};

class CNpcManager : public TWorldComponent
{
	SNpcDef m_aNpcs[MAX_NPC_DEFS];
	int m_NumNpcs;

	SSpawnDef m_aSpawns[MAX_SPAWN_DEFS];
	int m_NumSpawns;

	int m_PendingSpawnIdx; // used by OnBotPlayerCreated

	// NPC 近距对话 — 避免频繁触发
	int m_aAutoTalkCooldownTick[MAX_CLIENTS];
	int m_aAutoTalkNpcSpawnIdx[MAX_CLIENTS]; // -1 = none

	bool TryAutoTalk(CPlayer *pPlayer);

public:
	CNpcManager();

	void OnInitWorld(const char *pWhereLocalWorld) override;
	void OnPostInit() override;
	void OnTick() override;
	void OnShutdown() override;

	bool OnBotPlayerCreated(CPlayer *pPlayer);
	bool TryHammerTalk(CCharacter *pChr, vec2 ProjStartPos);

	// NPC def access (identity only)
	int NumNpcs() const { return m_NumNpcs; }
	const SNpcDef *GetNpc(int Index) const;
	const SNpcDef *FindNpc(const char *pNpcId) const;

	// Spawn access
	int NumSpawns() const { return m_NumSpawns; }
	const SSpawnDef *GetSpawn(int Index) const;
	const SSpawnDef *FindSpawn(const char *pSpawnId) const;
	const SSpawnDef *FindSpawnNear(CPlayer *pPlayer, vec2 Pos) const;
	const char *NpcIdForClient(int ClientID) const;

	bool IsQuestNpc(const CPlayer *pPlayer) const;
	bool IsQuestNpcCharacter(CCharacter *pChr) const;
	bool IsPlayerNearNpc(CPlayer *pPlayer, const char *pNpcId, float MaxDist = 180.f) const;

private:
	void LoadNpcs();
	void LoadSpawns();
	void SpawnWorldNpcs();
	void DespawnAll();
	bool SpawnSpawn(int SpawnIdx);
	void ApplySkin(CPlayer *pPlayer, const SNpcDef &Def) const;
	void SpawnCharacterAt(CPlayer *pPlayer, const SSpawnDef &Def);
	static int ParseEmote(const char *pName);
	static int NpcSlotForSpawn(int SpawnIdx);
	static bool EvaluateSpawnConditions(const char *pConditionsJson, CPlayer *pPlayer);
};

#endif

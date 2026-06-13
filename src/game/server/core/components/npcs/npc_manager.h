#ifndef GAME_SERVER_CORE_COMPONENTS_NPCS_NPC_MANAGER_H
#define GAME_SERVER_CORE_COMPONENTS_NPCS_NPC_MANAGER_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/server/core/tworld_component.h>

class CCharacter;
class CPlayer;

enum
{
	MAX_NPC_DEFS = 16,
	NPC_KEY_LEN = 32,
	NPC_HAMMER_RANGE = 64,
	NPC_SLOT_FIRST = MAX_CLIENTS - MAX_NPC_DEFS,
};

struct SNpcDef
{
	char m_aId[NPC_KEY_LEN];
	int m_World;
	float m_X;
	float m_Y;
	float m_Radius;
	char m_aNameKey[48];
	char m_aSkinBody[24];
	char m_aSkinDecoration[24];
	int m_Emote;
	bool m_Static;
	int m_ClientID;
};

class CNpcManager : public TWorldComponent
{
	SNpcDef m_aNpcs[MAX_NPC_DEFS];
	int m_NumNpcs;
	int m_PendingSpawnDefIdx;

public:
	CNpcManager();

	void OnInitWorld(const char *pWhereLocalWorld) override;
	void OnPostInit() override;
	void OnTick() override;
	void OnShutdown() override;

	bool OnBotPlayerCreated(CPlayer *pPlayer);
	bool TryHammerTalk(CCharacter *pChr, vec2 ProjStartPos);

	int NumNpcs() const { return m_NumNpcs; }
	const SNpcDef *GetNpc(int Index) const;
	const SNpcDef *FindNpc(const char *pNpcId) const;
	const SNpcDef *FindNpcNear(CPlayer *pPlayer, vec2 Pos) const;
	const char *NpcIdForClient(int ClientID) const;
	bool IsQuestNpc(const CPlayer *pPlayer) const;
	bool IsQuestNpcCharacter(CCharacter *pChr) const;

private:
	void LoadNpcs();
	void SpawnWorldNpcs();
	bool SpawnNpc(int DefIdx);
	void DespawnAll();
	void ApplySkin(CPlayer *pPlayer, const SNpcDef &Def) const;
	void SpawnCharacterAt(CPlayer *pPlayer, const SNpcDef &Def);
	static int ParseEmote(const char *pName);
	static int NpcSlotForDef(int DefIdx);
};

#endif

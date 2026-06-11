#ifndef GAME_SERVER_CORE_COMPONENTS_CONTENT_TRAIT_MANAGER_H
#define GAME_SERVER_CORE_COMPONENTS_CONTENT_TRAIT_MANAGER_H

#include <engine/shared/protocol.h>

#include <game/server/core/tworld_component.h>

#include "content_types.h"

class CCommandManager;
class CPlayer;

class CTraitManager : public TWorldComponent
{
	STraitDef m_aTraits[MAX_CONTENT_TRAITS];
	int m_NumTraits;
	char m_aaPlayerTrait[MAX_CLIENTS][CONTENT_KEY_LEN];
	bool m_aTraitLocked[MAX_CLIENTS];

public:
	CTraitManager();

	void OnInitWorld(const char *pWhereLocalWorld) override;
	void OnPlayerLogin(CPlayer *pPlayer) override;
	void OnClientReset(int ClientID) override;
	void OnCharacterSpawn(CPlayer *pPlayer) override;

	void RegisterVoteCommands(CCommandManager *pMgr);
	void BuildTraitVotePage(int ClientID);
	bool SelectTrait(CPlayer *pPlayer, const char *pTraitId);

	const char *GetPlayerTrait(int ClientID) const;
	const char *GetTraitIdByIndex(int Index) const;
	void GetCombatModifiers(CPlayer *pPlayer, float &DamageMul, float &ReloadMul) const;
	void GetSpawnBonuses(CPlayer *pPlayer, int &ExtraHealth, int &ShieldBonus) const;
	int GetMiningLuckBonus(CPlayer *pPlayer) const;
	int GetMineCdBonus(CPlayer *pPlayer) const;
	float GetSkillCdMul(CPlayer *pPlayer) const;
	int GetLifestealBonus(CPlayer *pPlayer) const;

private:
	void LoadTraits();
	const STraitDef *FindTrait(const char *pId) const;
	void AssignTrait(CPlayer *pPlayer);
};

#endif

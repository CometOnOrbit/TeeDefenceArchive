#ifndef GAME_SERVER_CORE_COMPONENTS_META_META_MANAGER_H
#define GAME_SERVER_CORE_COMPONENTS_META_META_MANAGER_H

#include <game/server/core/components/meta/player_meta.h>
#include <game/server/core/tworld_component.h>

class CPlayer;

class CMetaManager : public TWorldComponent
{
	SPlayerMetaData m_aMeta[MAX_CLIENTS];
	int m_aAchProgress[MAX_CLIENTS][MAX_META_ACHIEVEMENTS];

public:
	CMetaManager();

	SPlayerMetaData &Get(int ClientID);
	int GetAchProgress(int ClientID, int Idx) const;
	void SetAchProgress(int ClientID, int Idx, int Value);

	void OnPlayerLogin(CPlayer *pPlayer) override;
	void OnClientReset(int ClientID) override;

	void Load(int ClientID, const char *pJson);
	void Persist(int ClientID);

	const char *GetTraitId(int ClientID) const;
	bool GetTraitLocked(int ClientID) const;
	void SetTrait(int ClientID, const char *pTraitId, bool Locked);
};

#endif

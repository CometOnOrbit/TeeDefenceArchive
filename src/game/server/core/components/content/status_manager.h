#ifndef GAME_SERVER_CORE_COMPONENTS_CONTENT_STATUS_MANAGER_H
#define GAME_SERVER_CORE_COMPONENTS_CONTENT_STATUS_MANAGER_H

#include <engine/shared/protocol.h>

#include <game/server/core/tworld_component.h>

#include "content_types.h"

enum
{
	MAX_STATUS_SLOTS = MAX_CLIENTS,
};

struct SActiveStatus
{
	char m_aId[CONTENT_KEY_LEN];
	int m_Stacks;
	int m_DurationTicks;
	int m_TickInterval;
	int m_NextTickAt;
	int m_Amount;
	float m_SlowMul;
	bool m_Active;
};

class CCharacter;

class CStatusManager : public TWorldComponent
{
	SActiveStatus m_aaStatuses[MAX_STATUS_SLOTS][MAX_CHARACTER_STATUSES];

public:
	void OnInitWorld(const char *pWhereLocalWorld) override;
	void OnTick() override;
	void OnClientReset(int ClientID) override;

	void ClearCharacter(int ClientID);
	void ApplyStatus(CCharacter *pChr, const char *pStatusId, int Stacks, int DurationTicks, float SlowMul = 0.86f, int Amount = 0);
	void ClearDebuffs(CCharacter *pChr);
	void TickCharacter(CCharacter *pChr);
	bool AbsorbDamage(CCharacter *pChr, int &Dmg);
	int GetSlowTicks(CCharacter *pChr) const;

private:
	int FindSlot(SActiveStatus *pSlots, const char *pId) const;
	int FindFreeSlot(SActiveStatus *pSlots) const;
	SActiveStatus *GetSlots(CCharacter *pChr);
	void ProcessStatus(CCharacter *pChr, SActiveStatus &St);
};

#endif

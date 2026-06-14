#ifndef GAME_SERVER_CORE_COMPONENTS_META_MINI_EVENTS_MANAGER_H
#define GAME_SERVER_CORE_COMPONENTS_META_MINI_EVENTS_MANAGER_H

#include <game/server/core/tworld_component.h>

struct SMiniEventDef
{
	char m_aId[32];
	char m_aTitle[48];
	char m_aType[16];
	int m_BonusPercent;
	int m_DurationSec;
};

class CMiniEventsManager : public TWorldComponent
{
	SMiniEventDef m_aEvents[8];
	int m_NumEvents;
	int m_RollIntervalSec;
	int m_ChancePercent;
	int m_ActiveIdx;
	int m_EndTick;
	int m_NextRollTick;

public:
	CMiniEventsManager();

	void OnInitWorld(const char *pWhereLocalWorld) override;
	void OnTick() override;

	int GetLootBonusPercent() const;
	int GetMiningBonusPercent() const;
	int GetRepairDiscountPercent() const;
	const char *ActiveTitle() const;

private:
	void LoadDefs();
	void TryRollEvent();
	void StartEvent(int Idx);
};

#endif

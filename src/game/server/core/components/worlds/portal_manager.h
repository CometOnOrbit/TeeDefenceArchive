#ifndef GAME_SERVER_CORE_COMPONENTS_WORLDS_PORTAL_MANAGER_H
#define GAME_SERVER_CORE_COMPONENTS_WORLDS_PORTAL_MANAGER_H

#include <base/vmath.h>

#include <game/server/core/tworld_component.h>

class CPlayer;

enum
{
	MAX_PORTALS = 16,
	PORTAL_KEY_LEN = 32,
};

struct SPortalDef
{
	char m_aId[PORTAL_KEY_LEN];
	int m_World;
	float m_X;
	float m_Y;
	float m_Radius;
	int m_DestWorld;
	float m_DestX;
	float m_DestY;
	char m_aRequireQuest[PORTAL_KEY_LEN];
	int m_RequireItem;
};

class CPortalManager : public TWorldComponent
{
	SPortalDef m_aPortals[MAX_PORTALS];
	int m_NumPortals;
	int m_aDwellStart[MAX_CLIENTS];
	int m_aInsidePortal[MAX_CLIENTS];

public:
	CPortalManager();

	void OnInitWorld(const char *pWhereLocalWorld) override;
	void OnTick() override;
	void OnClientReset(int ClientID) override;

	void TryPortalTravel(CPlayer *pPlayer, vec2 Pos);
	bool TravelDirect(CPlayer *pPlayer, const char *pPortalId);
	bool CanTravelToWorld(CPlayer *pPlayer, int DestWorld, char *pReason, int ReasonSize) const;
	const SPortalDef *FindPortal(const char *pId) const;

private:
	void LoadPortals();
	const SPortalDef *FindPortalAt(int World, vec2 Pos) const;
};

#endif

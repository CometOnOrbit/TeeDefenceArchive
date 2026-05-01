/* (c) TeeDefenceArchive - 2026 */
#ifndef GAME_SERVER_ENTITIES_TOWER_MAIN_H
#define GAME_SERVER_ENTITIES_TOWER_MAIN_H

#include <game/server/entity.h>

static const int s_TowerNumSide = 32;
static const int s_TowerSize = 240;

class CTowerMain : public CHitableEntity
{
public:
	CTowerMain(CGameWorld *pGameWorld, vec2 StandPos);
	virtual ~CTowerMain();

	virtual void Tick() override;
	virtual void Reset() override;
	virtual void Snap(int SnappingClient) override;
	virtual bool TakeHit(vec2 Force, vec2 Source, int Dmg, CEntity *pFrom, int Weapon) override;

	void TakeDamage(int Dmg);
	int GetHealth() const { return m_Health; }
	void SetHealth(int Health);

private:
	int m_FlagID;
	int m_aIDs[9];
	int m_alIDs[s_TowerNumSide];
	int m_Health;
};

#endif

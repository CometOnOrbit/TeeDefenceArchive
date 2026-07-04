#ifndef GAME_SERVER_ENTITIES_MMO_TRACKED_PLASMA_H
#define GAME_SERVER_ENTITIES_MMO_TRACKED_PLASMA_H

#include <game/server/entity.h>

class CMMOTrackedPlasma : public CChildEntity
{
	vec2 m_Direction;
	int m_Damage;
	int m_TrackedCID;
	int m_StartTick;
	float m_SpeedMin;
	float m_SpeedMax;

public:
	CMMOTrackedPlasma(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, vec2 Direction, int Damage, float SpeedMin, float SpeedMax);

	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	void Explode(CCharacter *pOwnerChar);
	void SearchPotentialTarget();
	CCharacter *GetTrackedTarget();
};

#endif

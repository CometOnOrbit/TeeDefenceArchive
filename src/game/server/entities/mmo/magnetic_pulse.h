#ifndef GAME_SERVER_ENTITIES_MMO_MAGNETIC_PULSE_H
#define GAME_SERVER_ENTITIES_MMO_MAGNETIC_PULSE_H

#include <game/server/entity.h>

class CMMOMagneticPulse : public CChildEntity
{
	vec2 m_Direction;
	vec2 m_PosTo;
	bool m_LastPhase;
	int m_LifeTick;
	float m_MaxRadius;
	float m_Radius;
	int m_RadiusGrowStartTick;

public:
	CMMOMagneticPulse(CGameWorld *pGameWorld, int OwnerCID, float Radius, vec2 Pos, vec2 Direction);

	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	void RunLastPhase();
	void TickFirstPhase();
	void TickLastPhase();
};

#endif

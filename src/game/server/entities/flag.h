#ifndef GAME_SERVER_ENTITIES_FLAG_H
#define GAME_SERVER_ENTITIES_FLAG_H

#include <game/server/entity.h>

class CFlag : public CEntity
{
	int m_Team;
	vec2 m_StandPos;

	bool m_AtStand;
	class CCharacter *m_pCarrier;
	vec2 m_Vel;
	int m_GrabTick;
	int m_DropTick;

public:
	static int const ms_PhysSize = 14;

	CFlag(CGameWorld *pGameWorld, int Team, vec2 StandPos);

	int GetTeam() const { return m_Team; }
	bool IsAtStand() const { return m_AtStand; }
	CCharacter *GetCarrier() const { return m_pCarrier; }
	int GetGrabTick() const { return m_GrabTick; }
	int GetDropTick() const { return m_DropTick; }

	void Reset() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;
	void TickDefered() override;

	void Grab(CCharacter *pChar);
	void Drop();
};

#endif

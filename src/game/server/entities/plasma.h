/* (c) TeeDefenceArchive */
#ifndef GAME_SERVER_ENTITIES_PLASMA_H
#define GAME_SERVER_ENTITIES_PLASMA_H

#include <game/server/entity.h>

class CPlasma : public CEntity
{
public:
	CPlasma(CGameWorld *pGameWorld, vec2 Pos, int Owner, int TrackedPlayer, vec2 Direction, bool Freeze, bool Explosive, int Weapon, int Damage = 3);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;

	int GetOwner() const { return m_Owner; }

private:
	void Explode();

	int m_Owner;
	int m_TrackedPlayer;
	vec2 m_Dir;
	bool m_Freeze;
	bool m_Explosive;
	int m_Weapon;
	int m_Damage;
	int m_LifeSpan;
	float m_Speed;
	float m_InitialAmount;
};

#endif

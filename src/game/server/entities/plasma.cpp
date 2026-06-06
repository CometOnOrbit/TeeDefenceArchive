/* (c) TeeDefenceArchive */
#include <engine/shared/config.h>

#include <generated/protocol.h>
#include <game/server/gamecontext.h>

#include "character.h"
#include "plasma.h"

CPlasma::CPlasma(CGameWorld *pGameWorld, vec2 Pos, int Owner, int TrackedPlayer, vec2 Direction, bool Freeze, bool Explosive, int Weapon, int Damage)
	: CEntity(pGameWorld, CGameWorld::ENTTYPE_PLASMA, CGameWorld::ENTFLAG_CHILD, Pos, 14)
{
	m_Owner = Owner;
	m_TrackedPlayer = TrackedPlayer;
	m_Dir = Direction;
	m_Freeze = Freeze;
	m_Explosive = Explosive;
	m_Weapon = Weapon;
	m_Damage = Damage;
	m_LifeSpan = Server()->TickSpeed() * 60;
	m_InitialAmount = 1.0f;
	m_Speed = 0.f;
	GameWorld()->InsertEntity(this);
}

void CPlasma::Reset()
{
	GameWorld()->DestroyEntity(this);
}

void CPlasma::Tick()
{
	if(m_LifeSpan < 0)
	{
		Reset();
		return;
	}
	m_LifeSpan--;

	CCharacter *pTarget = GameServer()->GetPlayerChar(m_TrackedPlayer);
	if(pTarget)
	{
		const float Dist = distance(m_Pos, pTarget->GetPos());
		if(Dist < 24.0f)
			Explode();
		else
		{
			m_Dir = normalize(pTarget->GetPos() - m_Pos);
			m_Speed = clamp(Dist, 0.0f, 16.0f) * (1.0f - m_InitialAmount);
			m_Pos += m_Dir * m_Speed;
			m_InitialAmount *= 0.98f;
			if(GameServer()->Collision()->CheckPoint(m_Pos.x, m_Pos.y))
				Explode();
		}
	}
	else
		Explode();
}

void CPlasma::Explode()
{
	if(m_Explosive)
		GameWorld()->CreateExplosion(m_Pos, this, WEAPON_GRENADE, m_Damage);
	else if(CCharacter *pTarget = GameServer()->GetPlayerChar(m_TrackedPlayer))
		pTarget->TakeDamage(vec2(0, 0), m_Pos, m_Damage, m_Owner, m_Weapon);
	Reset();
}

void CPlasma::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, GetID(), sizeof(CNetObj_Projectile)));
	if(!pObj)
		return;

	pObj->m_X = (int)m_Pos.x;
	pObj->m_Y = (int)m_Pos.y;
	pObj->m_VelX = (int)(m_Dir.x * 100);
	pObj->m_VelY = (int)(m_Dir.y * 100);
	pObj->m_StartTick = Server()->Tick();
	pObj->m_Type = m_Weapon;
}

#include <generated/protocol.h>

#include <game/server/gamecontext.h>

#include "../character.h"
#include "homing_grenade.h"
#include "mmo_weapon_common.h"

CMMOHomingGrenade::CMMOHomingGrenade(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, vec2 Direction, int Damage, float Speed)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE, 0, Pos, 24)
{
	m_Owner = OwnerCID;
	m_Direction = normalize(Direction + vec2((random_float() - 0.5f) * 0.3f, (random_float() - 0.5f) * 0.3f));
	m_Damage = Damage;
	m_Speed = Speed;

	GameWorld()->InsertEntity(this);
}

void CMMOHomingGrenade::Tick()
{
	CCharacter *pOwnerChar = MMOWeaponOwnerChar(GameServer(), m_Owner);
	if(!pOwnerChar)
	{
		MarkForDestroy();
		return;
	}

	m_Pos += normalize(m_Direction) * m_Speed;

	CCharacter *pHitChar = nullptr;
	for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pChar = static_cast<CCharacter *>(r.front());
		if(!MMOWeaponTargetValid(GameServer(), m_Owner, pChar))
			continue;

		const float Dist = distance(m_Pos, pChar->GetPos());
		if(Dist < 64.f)
		{
			pHitChar = pChar;
			break;
		}

		if(Dist < 300.f)
		{
			const vec2 ToEnemy = normalize(pChar->GetPos() - m_Pos);
			m_Direction = normalize(m_Direction + ToEnemy * 0.05f);
		}
	}

	if(pHitChar || GameServer()->Collision()->CheckPoint(m_Pos))
	{
		GameWorld()->CreateExplosion(m_Pos, pOwnerChar, WEAPON_GRENADE, m_Damage);
		MarkForDestroy();
	}
}

void CMMOHomingGrenade::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, GetID(), sizeof(CNetObj_Projectile)));
	if(!pObj)
		return;

	pObj->m_X = (int)m_Pos.x;
	pObj->m_Y = (int)m_Pos.y;
	pObj->m_VelX = (int)(m_Direction.x * 100);
	pObj->m_VelY = (int)(m_Direction.y * 100);
	pObj->m_StartTick = Server()->Tick();
	pObj->m_Type = WEAPON_GRENADE;
}

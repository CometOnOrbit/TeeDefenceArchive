#include <generated/protocol.h>

#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "../character.h"
#include "hammer_lamp_bolt.h"
#include "mmo_weapon_common.h"

CMMOHammerLampBolt::CMMOHammerLampBolt(CGameWorld *pGameWorld, int OwnerCID, int TargetCID, vec2 Pos, vec2 InitialVel, int Damage)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE, 0, Pos, 24)
{
	m_Owner = OwnerCID;
	m_TargetCID = TargetCID;
	m_InitialVel = InitialVel;
	m_InitialAmount = 1.0f;
	m_Vel = InitialVel;
	m_Damage = Damage;

	GameWorld()->InsertEntity(this);
}

void CMMOHammerLampBolt::Tick()
{
	CCharacter *pOwnerChar = MMOWeaponOwnerChar(GameServer(), m_Owner);
	if(!pOwnerChar)
	{
		MarkForDestroy();
		return;
	}

	if(m_TargetCID < 0 || m_TargetCID >= MAX_CLIENTS)
	{
		MarkForDestroy();
		return;
	}

	CPlayer *pTargetPl = GameServer()->m_apPlayers[m_TargetCID];
	CCharacter *pTarget = pTargetPl ? pTargetPl->GetCharacter() : nullptr;
	if(!pTarget || !MMOWeaponTargetValid(GameServer(), m_Owner, pTarget))
	{
		MarkForDestroy();
		return;
	}

	const float Dist = distance(m_Pos, pTarget->GetPos());
	if(Dist < (float)CCharacter::ms_PhysSize)
	{
		GameWorld()->CreateDeath(pTarget->GetPos(), pTarget->GetCID());
		pTarget->TakeDamage(m_InitialVel, m_InitialVel * -1.f, m_Damage, m_Owner, WEAPON_HAMMER);
		MarkForDestroy();
		return;
	}

	const vec2 Dir = normalize(pTarget->GetPos() - m_Pos);
	m_Vel = Dir * clamp(Dist, 0.0f, 16.0f) * (1.0f - m_InitialAmount) + m_InitialVel * m_InitialAmount;
	m_Pos += m_Vel;
	m_InitialAmount *= 0.98f;
}

void CMMOHammerLampBolt::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, GetID(), sizeof(CNetObj_Projectile)));
	if(!pObj)
		return;

	pObj->m_X = (int)m_Pos.x;
	pObj->m_Y = (int)m_Pos.y;
	pObj->m_VelX = (int)(m_Vel.x * 100);
	pObj->m_VelY = (int)(m_Vel.y * 100);
	pObj->m_StartTick = Server()->Tick();
	pObj->m_Type = WEAPON_HAMMER;
}

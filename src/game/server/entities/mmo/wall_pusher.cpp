#include <generated/protocol.h>
#include <generated/server_data.h>

#include <game/collision.h>
#include <game/server/gamecontext.h>

#include "../character.h"
#include "mmo_weapon_common.h"
#include "wall_pusher.h"

CMMOWallPusher::CMMOWallPusher(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, vec2 Direction, int LifeTick, int Damage)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, 0, Pos, 64)
{
	m_Owner = OwnerCID;
	m_Direction = Direction;
	m_PosTo = Pos;
	m_LifeTick = LifeTick;
	m_Damage = Damage;
	m_ID2 = Server()->SnapNewID(GameWorld()->GameServer()->GetWorldID());

	GameWorld()->CreateSound(Pos, SOUND_LASER_FIRE);
	GameWorld()->InsertEntity(this);
}

CMMOWallPusher::~CMMOWallPusher()
{
	Server()->SnapFreeID(m_ID2, GameWorld()->GameServer()->GetWorldID());
}

void CMMOWallPusher::TryHitCharacter(const vec2 PrevPos)
{
	CCharacter *pOwnerChar = MMOWeaponOwnerChar(GameServer(), m_Owner);
	if(!pOwnerChar)
		return;

	bool MarkedForDestroy = false;

	for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pChar = static_cast<CCharacter *>(r.front());
		if(!MMOWeaponTargetValid(GameServer(), m_Owner, pChar))
			continue;

		vec2 IntersectPos = closest_point_on_line(m_Pos, m_PosTo, pChar->GetPos());
		if(distance(IntersectPos, pChar->GetPos()) > GetProximityRadius())
			continue;

		const vec2 WallMovement = m_Pos - PrevPos;
		pChar->GetCore()->m_Vel = WallMovement * 1.2f;

		const vec2 CharTestPos = pChar->GetPos() + WallMovement;
		if(GameServer()->Collision()->TestBox(CharTestPos, vec2(CCharacter::ms_PhysSize, CCharacter::ms_PhysSize)))
		{
			GameWorld()->CreateExplosion(m_Pos, pOwnerChar, WEAPON_LASER, m_Damage / 2);
			GameWorld()->CreateExplosion(m_PosTo, pOwnerChar, WEAPON_LASER, m_Damage / 2);
			GameWorld()->CreateExplosion(IntersectPos + WallMovement, pOwnerChar, WEAPON_LASER, m_Damage);
			MarkedForDestroy = true;
		}
	}

	if(MarkedForDestroy)
		MarkForDestroy();
}

void CMMOWallPusher::Tick()
{
	if(!MMOWeaponOwnerChar(GameServer(), m_Owner))
	{
		MarkForDestroy();
		return;
	}

	m_LifeTick--;
	if(m_LifeTick <= 0)
	{
		MarkForDestroy();
		return;
	}

	if(GameServer()->Collision()->CheckPoint(m_Pos) && GameServer()->Collision()->CheckPoint(m_PosTo))
	{
		GameWorld()->CreateDeath(m_Pos, m_Owner);
		GameWorld()->CreateDeath(m_PosTo, m_Owner);
		MarkForDestroy();
		return;
	}

	const vec2 NormalizedDirection = normalize(m_Direction);
	const vec2 PrevPos = m_Pos;
	const float SegmentLength = distance(m_Pos, m_PosTo);

	if(SegmentLength < 240.f)
	{
		const vec2 PerpendicularDirection = vec2(-NormalizedDirection.y, NormalizedDirection.x);
		m_Pos += PerpendicularDirection * 3.f;
		m_PosTo -= PerpendicularDirection * 3.f;
	}

	m_Pos += NormalizedDirection * 10.f;
	m_PosTo += NormalizedDirection * 10.f;

	TryHitCharacter(PrevPos);
}

void CMMOWallPusher::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	const int StartTick = Server()->Tick() - 3;

	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));
	if(pObj)
	{
		pObj->m_X = round_to_int(m_Pos.x);
		pObj->m_Y = round_to_int(m_Pos.y);
		pObj->m_FromX = round_to_int(m_PosTo.x);
		pObj->m_FromY = round_to_int(m_PosTo.y);
		pObj->m_StartTick = StartTick;
	}

	CNetObj_Laser *pObj2 = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_ID2, sizeof(CNetObj_Laser)));
	if(pObj2)
	{
		pObj2->m_X = round_to_int(m_PosTo.x);
		pObj2->m_Y = round_to_int(m_PosTo.y);
		pObj2->m_FromX = round_to_int(m_Pos.x);
		pObj2->m_FromY = round_to_int(m_Pos.y);
		pObj2->m_StartTick = StartTick;
	}
}

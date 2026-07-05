#include "skill_ricochet_shot.h"
#include "skill_spawn.h"

#include <game/server/entities/mmo/mmo_weapon_common.h>
#include <game/server/gamecontext.h>
#include <generated/protocol.h>
#include <generated/server_data.h>

#include "../character.h"

CSkillRicochetShot::CSkillRicochetShot(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE, 0, Pos)
{
	m_SpawnPos = Pos;
	m_Pos = Pos;
	m_Owner = OwnerCID;
	m_Direction = normalize(Dir);
	if(length(m_Direction) < 0.01f)
		m_Direction = vec2(1.f, 0.f);
	m_Damage = maximum(1, Damage);
	m_StartTick = Server()->Tick();
	m_BouncesLeft = 3;
	m_DistanceLeft = 1200.f;
	AddSnappingGroupIds(SNAP_GROUP_CENTER, 1);
	GameWorld()->CreateSound(m_Pos, SOUND_WEAPON_SPAWN);
	GameWorld()->InsertEntity(this);
}

vec2 CSkillRicochetShot::GetPos(float Time)
{
	const float Curvature = GameServer()->Tuning()->m_ShotgunCurvature;
	const float Speed = GameServer()->Tuning()->m_ShotgunSpeed;
	return CalcPos(m_SpawnPos, m_Direction, Curvature, Speed, Time);
}

void CSkillRicochetShot::Tick()
{
	if(!MMOWeaponOwnerChar(GameServer(), m_Owner))
	{
		MarkForDestroy();
		return;
	}

	const float Pt = (Server()->Tick() - m_StartTick - 1) / (float)Server()->TickSpeed();
	const float Ct = (Server()->Tick() - m_StartTick) / (float)Server()->TickSpeed();
	const vec2 PrevPos = GetPos(Pt);
	const vec2 CurPos = GetPos(Ct);
	m_Pos = CurPos;

	m_DistanceLeft -= distance(CurPos, PrevPos);
	if(GameLayerClipped(CurPos) || m_BouncesLeft < 0 || m_DistanceLeft < 0.f)
	{
		MarkForDestroy();
		return;
	}

	vec2 HitPos = CurPos;
	const int Collide = GameServer()->Collision()->IntersectLine(PrevPos, CurPos, &HitPos, nullptr);

	CCharacter *pOwnerChar = MMOWeaponOwnerChar(GameServer(), m_Owner);
	CEntity *pHitEnt = GameWorld()->IntersectFlagEntity(PrevPos, CurPos, 6.f, HitPos, CGameWorld::ENTFLAG_HITABLE, pOwnerChar);
	if(pHitEnt && pHitEnt->ObjType() == CGameWorld::ENTTYPE_CHARACTER)
	{
		CCharacter *pTarget = static_cast<CCharacter *>(pHitEnt);
		if(MMOWeaponTargetValid(GameServer(), m_Owner, pTarget))
		{
			vec2 Force = normalize(CurPos - PrevPos);
			if(length(Force) < 0.01f)
				Force = m_Direction;
			pTarget->TakeDamage(Force * 8.f, HitPos, m_Damage, m_Owner, WEAPON_SHOTGUN);
			GameWorld()->CreateSound(HitPos, SOUND_LASER_BOUNCE);
			SpawnSkillHitBurst(GameWorld(), HitPos, 48.f, SKILL_VFX_ARCANE, 0, 6);
			MarkForDestroy();
			return;
		}
	}

	if(Collide)
	{
		if(m_BouncesLeft <= 0)
		{
			MarkForDestroy();
			return;
		}

		const bool CollideX = GameServer()->Collision()->IntersectLine(PrevPos, vec2(CurPos.x, HitPos.y), nullptr, nullptr) != 0;
		const bool CollideY = GameServer()->Collision()->IntersectLine(PrevPos, vec2(HitPos.x, CurPos.y), nullptr, nullptr) != 0;

		vec2 Vel;
		Vel.x = m_Direction.x;
		Vel.y = m_Direction.y + 2.f * GameServer()->Tuning()->m_ShotgunCurvature / 10000.f * Ct * GameServer()->Tuning()->m_ShotgunSpeed;

		if(CollideX && !CollideY)
		{
			m_Direction.x = -Vel.x;
			m_Direction.y = Vel.y;
		}
		else if(!CollideX && CollideY)
		{
			m_Direction.x = Vel.x;
			m_Direction.y = -Vel.y;
		}
		else
		{
			m_Direction.x = -Vel.x;
			m_Direction.y = -Vel.y;
		}

		m_Direction.x *= 0.5f;
		m_Direction.y *= 0.5f;
		m_SpawnPos = HitPos;
		m_StartTick = Server()->Tick();
		m_BouncesLeft--;
		GameWorld()->CreateSound(HitPos, SOUND_LASER_BOUNCE);
	}
}

void CSkillRicochetShot::Snap(int SnappingClient)
{
	const float Ct = (Server()->Tick() - m_StartTick) / (float)Server()->TickSpeed();
	const vec2 RenderPos = GetPos(Ct);
	if(NetworkClipped(SnappingClient, RenderPos))
		return;

	CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, GetID(), sizeof(CNetObj_Projectile)));
	if(!pProj)
		return;

	pProj->m_X = round_to_int(RenderPos.x);
	pProj->m_Y = round_to_int(RenderPos.y);
	pProj->m_VelX = round_to_int(m_Direction.x * 100.f);
	pProj->m_VelY = round_to_int(m_Direction.y * 100.f);
	pProj->m_StartTick = m_StartTick;
	pProj->m_Type = WEAPON_SHOTGUN;
}

#include "skill_fireball.h"
#include "skill_spawn.h"

#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/core/components/content/status_manager.h>
#include <generated/server_data.h>

#include "../character.h"

CSkillFireball::CSkillFireball(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage, bool ApplyBurn)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE, 0, Pos)
{
	m_Pos = Pos;
	m_Direction = normalize(Dir);
	if(length(m_Direction) < 0.01f)
		m_Direction = vec2(1.f, 0.f);
	m_Owner = OwnerCID;
	m_Damage = maximum(1, Damage);
	m_LifeSpan = Server()->TickSpeed() * 2;
	m_StartTick = Server()->Tick();
	m_ApplyBurn = ApplyBurn;
	AddSnappingGroupIds(SNAP_GROUP_CENTER, 1);
	GameWorld()->InsertEntity(this);
}

vec2 CSkillFireball::GetPos(float Time)
{
	const float Speed = GameServer()->Tuning()->m_GunSpeed * 1.15f;
	const float Curvature = GameServer()->Tuning()->m_GunCurvature * 0.35f;
	return CalcPos(m_Pos, m_Direction, Curvature, Speed, Time);
}

void CSkillFireball::Impact(vec2 Pos, CEntity *pHitEnt)
{
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	GameWorld()->CreateSound(Pos, SOUND_GRENADE_EXPLODE);
	SpawnSkillHitBurst(GameWorld(), Pos, 80.f, SKILL_VFX_FIRE);

	if(pOwnerChar)
		GameWorld()->CreateExplosion(Pos, pOwnerChar, WEAPON_GRENADE, maximum(1, m_Damage / 2));

	if(pHitEnt && pHitEnt->ObjType() == CGameWorld::ENTTYPE_CHARACTER)
	{
		CCharacter *pTarget = static_cast<CCharacter *>(pHitEnt);
		pTarget->TakeDamage(m_Direction * 6.f, m_Direction * -1.f, m_Damage, m_Owner, WEAPON_GUN);
		if(m_ApplyBurn && GameServer()->Core() && GameServer()->Core()->StatusManager())
			GameServer()->Core()->StatusManager()->ApplyStatus(pTarget, "burn", 1, Server()->TickSpeed() * 3);
	}

	MarkForDestroy();
}

void CSkillFireball::Tick()
{
	if(!GameServer()->GetPlayerChar(m_Owner))
	{
		MarkForDestroy();
		return;
	}

	const float Pt = (Server()->Tick() - m_StartTick - 1) / (float)Server()->TickSpeed();
	const float Ct = (Server()->Tick() - m_StartTick) / (float)Server()->TickSpeed();
	const vec2 PrevPos = GetPos(Pt);
	const vec2 CurPos = GetPos(Ct);

	vec2 HitPos = CurPos;
	const int Collide = GameServer()->Collision()->IntersectLine(PrevPos, CurPos, &HitPos, nullptr);
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	CEntity *pIntersect = GameWorld()->IntersectFlagEntity(PrevPos, CurPos, 6.0f, HitPos, CGameWorld::ENTFLAG_HITABLE, pOwnerChar);

	m_LifeSpan--;
	if(Collide || pIntersect || m_LifeSpan <= 0)
		Impact(HitPos, pIntersect);
}

void CSkillFireball::Snap(int SnappingClient)
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
	pProj->m_Type = WEAPON_GRENADE;

	const array<int> *pTrailIds = FindSnappingGroupIds(SNAP_GROUP_CENTER);
	if(pTrailIds && pTrailIds->size() > 0)
	{
		const vec2 TrailFrom = RenderPos - m_Direction * 32.f;
		CNetObj_Laser *pTrail = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, (*pTrailIds)[0], sizeof(CNetObj_Laser)));
		if(pTrail)
		{
			pTrail->m_X = round_to_int(RenderPos.x);
			pTrail->m_Y = round_to_int(RenderPos.y);
			pTrail->m_FromX = round_to_int(TrailFrom.x);
			pTrail->m_FromY = round_to_int(TrailFrom.y);
			pTrail->m_StartTick = m_StartTick;
		}
	}
}

#include "skill_laser_trap.h"
#include "skill_spawn.h"
#include "skill_vfx_common.h"

#include <base/math.h>
#include <base/vmath.h>

#include <game/server/entities/mmo/mmo_weapon_common.h>
#include <game/server/gamecontext.h>
#include <generated/protocol.h>
#include <generated/server_data.h>

#include "../character.h"

CSkillLaserTrap::CSkillLaserTrap(CGameWorld *pGameWorld, int OwnerCID, vec2 From, vec2 To, int NumBeams, int Damage, int DurationTicks)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, 0, From, 64)
{
	m_Owner = OwnerCID;
	m_From = From;
	m_To = To;
	m_Pos = (From + To) * 0.5f;
	m_Damage = maximum(1, Damage);
	m_LifeSpan = maximum(1, DurationTicks);
	m_DamageInterval = maximum(1, Server()->TickSpeed() / 4);
	m_NextDamageTick = Server()->Tick() + m_DamageInterval;
	m_StartTick = Server()->Tick();

	const int Beams = clamp(NumBeams, 3, 5);
	const vec2 LineDir = normalize(To - From);
	const vec2 Perp(-LineDir.y, LineDir.x);
	const float BeamHalf = 36.f;

	m_aBeamA.clear();
	m_aBeamB.clear();
	for(int i = 0; i < Beams; i++)
	{
		const float T = (float)(i + 1) / (float)(Beams + 1);
		const vec2 Center = From + (To - From) * T;
		m_aBeamA.add(Center - Perp * BeamHalf);
		m_aBeamB.add(Center + Perp * BeamHalf);
	}

	AddSnappingGroupIds(SNAP_GROUP_RING, Beams);
	AddSnappingGroupIds(SNAP_GROUP_CENTER, 1);

	GameWorld()->CreateSound(From, SOUND_LASER_FIRE);
	GameWorld()->InsertEntity(this);
}

void CSkillLaserTrap::Tick()
{
	if(!MMOWeaponOwnerChar(GameServer(), m_Owner))
	{
		MarkForDestroy();
		return;
	}

	if(Server()->Tick() >= m_NextDamageTick)
	{
		const float HitRadius = 28.f;
		for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
		{
			CCharacter *pTarget = static_cast<CCharacter *>(r.front());
			if(!MMOWeaponTargetValid(GameServer(), m_Owner, pTarget))
				continue;

			bool Hit = false;
			const vec2 TargetPos = pTarget->GetPos();
			for(int i = 0; i < m_aBeamA.size(); i++)
			{
				const vec2 Closest = closest_point_on_line(m_aBeamA[i], m_aBeamB[i], TargetPos);
				if(distance(Closest, TargetPos) <= HitRadius)
				{
					Hit = true;
					break;
				}
			}

			if(!Hit)
			{
				const vec2 ClosestLine = closest_point_on_line(m_From, m_To, TargetPos);
				if(distance(ClosestLine, TargetPos) <= HitRadius)
					Hit = true;
			}

			if(Hit)
			{
				vec2 Force = normalize(TargetPos - m_From);
				if(length(Force) < 0.01f)
					Force = vec2(0.f, -1.f);
				pTarget->TakeDamage(Force * 4.f, TargetPos, m_Damage, m_Owner, WEAPON_LASER);
				GameWorld()->CreateHammerHit(TargetPos);
			}
		}
		m_NextDamageTick = Server()->Tick() + m_DamageInterval;
	}

	if(--m_LifeSpan <= 0)
		MarkForDestroy();
}

void CSkillLaserTrap::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient, m_Pos))
		return;

	const array<int> *pCenterIds = FindSnappingGroupIds(SNAP_GROUP_CENTER);
	if(pCenterIds && pCenterIds->size() > 0)
		SnapLaserDot(Server(), (*pCenterIds)[0], m_From, m_StartTick);

	const array<int> *pBeamIds = FindSnappingGroupIds(SNAP_GROUP_RING);
	if(!pBeamIds)
		return;

	for(int i = 0; i < minimum((int)pBeamIds->size(), m_aBeamA.size()); i++)
		SnapLaserSegment(Server(), (*pBeamIds)[i], m_aBeamA[i], m_aBeamB[i], m_StartTick);

	if(pBeamIds->size() > 0)
		SnapLaserSegment(Server(), (*pBeamIds)[0], m_From, m_To, m_StartTick);
}

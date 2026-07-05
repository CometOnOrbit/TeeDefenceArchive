#include "skill_beam_sweep.h"
#include "skill_spawn.h"
#include "skill_vfx_common.h"

#include <game/server/gamecontext.h>
#include <game/server/entities/mmo/mmo_weapon_common.h>
#include <generated/protocol.h>
#include <generated/server_data.h>

#include "../character.h"

CSkillBeamSweep::CSkillBeamSweep(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, float StartAngle, float BeamLength, int Damage, int DurationTicks)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, 0, Pos, (int)BeamLength)
{
	m_Pos = Pos;
	m_Owner = OwnerCID;
	m_BeamLength = maximum(160.f, BeamLength);
	m_Angle = StartAngle;
	m_AngleSpeed = 1.35f;
	m_Damage = maximum(1, Damage);
	m_LifeSpan = maximum(1, DurationTicks);
	m_StartTick = Server()->Tick();
	m_NextHitTick = Server()->Tick();

	AddSnappingGroupIds(SNAP_GROUP_RING, 3);
	AddSnappingGroupIds(SNAP_GROUP_CENTER, 1);

	SpawnSkillCastRing(GameWorld(), m_Pos, 72.f, m_LifeSpan, SKILL_VFX_HOLY, SKILL_RING_STEADY, m_LifeSpan / 3);
	GameWorld()->CreateSound(m_Pos, SOUND_LASER_FIRE);
	GameWorld()->InsertEntity(this);
}

void CSkillBeamSweep::DamageAlongBeam()
{
	CCharacter *pOwnerChar = MMOWeaponOwnerChar(GameServer(), m_Owner);
	if(!pOwnerChar)
		return;

	const vec2 BeamDir = direction(m_Angle);

	for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pTarget = static_cast<CCharacter *>(r.front());
		if(!MMOWeaponTargetValid(GameServer(), m_Owner, pTarget))
			continue;

		const vec2 TargetPos = pTarget->GetPos();
		const vec2 ToTarget = TargetPos - m_Pos;
		const float Along = dot(ToTarget, BeamDir);
		if(Along < 0.f || Along > m_BeamLength)
			continue;

		const vec2 Closest = m_Pos + BeamDir * Along;
		if(distance(Closest, TargetPos) > 36.f)
			continue;

		pTarget->TakeDamage(BeamDir * 6.f, m_Pos, m_Damage, m_Owner, WEAPON_LASER);
		SpawnSkillHitBurst(GameWorld(), TargetPos, 40.f, SKILL_VFX_HOLY, Server()->TickSpeed() / 4, 8);
	}
}

void CSkillBeamSweep::Tick()
{
	if(!MMOWeaponOwnerChar(GameServer(), m_Owner))
	{
		MarkForDestroy();
		return;
	}

	m_Angle += m_AngleSpeed / (float)maximum(1, Server()->TickSpeed()) * 2.f * pi;

	if(Server()->Tick() >= m_NextHitTick)
	{
		DamageAlongBeam();
		m_NextHitTick = Server()->Tick() + maximum(1, Server()->TickSpeed() / 8);
		GameWorld()->CreateSound(m_Pos, SOUND_LASER_BOUNCE);
	}

	if(--m_LifeSpan <= 0)
	{
		SpawnSkillHitBurst(GameWorld(), m_Pos, 80.f, SKILL_VFX_HOLY);
		MarkForDestroy();
	}
}

void CSkillBeamSweep::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	const vec2 BeamDir = direction(m_Angle);
	const array<int> *pBeamIds = FindSnappingGroupIds(SNAP_GROUP_RING);
	if(!pBeamIds || pBeamIds->size() < 3)
		return;

	for(int i = 0; i < 3; i++)
	{
		const float Off = ((float)i - 1.f) * 0.08f;
		const vec2 Dir = direction(m_Angle + Off);
		const vec2 End = m_Pos + Dir * m_BeamLength;
		CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, (*pBeamIds)[i], sizeof(CNetObj_Laser)));
		if(!pLaser)
			continue;
		pLaser->m_X = round_to_int(End.x);
		pLaser->m_Y = round_to_int(End.y);
		pLaser->m_FromX = round_to_int(m_Pos.x);
		pLaser->m_FromY = round_to_int(m_Pos.y);
		pLaser->m_StartTick = m_StartTick;
	}

	const array<int> *pCenterIds = FindSnappingGroupIds(SNAP_GROUP_CENTER);
	if(pCenterIds && pCenterIds->size() > 0)
		SnapLaserDot(Server(), (*pCenterIds)[0], m_Pos, m_StartTick);
}

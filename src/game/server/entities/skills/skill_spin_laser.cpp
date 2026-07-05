#include "skill_spin_laser.h"
#include "skill_spawn.h"

#include <base/math.h>

#include <generated/protocol.h>
#include <generated/server_data.h>
#include <game/server/gamecontext.h>

#include "../character.h"
#include "../mmo/mmo_weapon_common.h"

CSkillSpinLaser::CSkillSpinLaser(CGameWorld *pGameWorld, int OwnerCID, float StartAngle, float OrbitRadius, float BeamEnergy, int Damage, int DurationTicks)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, 0, vec2(0, 0), (int)maximum(64.f, OrbitRadius + BeamEnergy))
{
	m_Owner = OwnerCID;
	m_OrbitAngle = StartAngle;
	m_OrbitRadius = maximum(32.f, OrbitRadius);
	m_BeamEnergy = maximum(160.f, BeamEnergy);
	m_Damage = maximum(1, Damage);
	m_DamageTimer = 0;
	m_LifeSpan = maximum(1, DurationTicks);
	m_EvalTick = Server()->Tick();

	CCharacter *pOwnerChar = MMOWeaponOwnerChar(GameServer(), m_Owner);
	if(pOwnerChar)
		m_Pos = pOwnerChar->GetPos();

	m_Dir = direction(m_OrbitAngle + pi / 2.f);
	GameWorld()->CreateSound(m_Pos, SOUND_LASER_FIRE);
	GameWorld()->InsertEntity(this);
	SpinStep();
}

bool CSkillSpinLaser::HitAlongBeam(vec2 From, vec2 To)
{
	vec2 At;
	CCharacter *pOwnerChar = MMOWeaponOwnerChar(GameServer(), m_Owner);
	CCharacter *pHit = GameWorld()->IntersectCharacter(From, To, 0.f, At, pOwnerChar);
	if(!pHit || !MMOWeaponTargetValid(GameServer(), m_Owner, pHit))
		return false;

	if(m_DamageTimer > 0)
		return true;

	m_From = From;
	m_Pos = At;
	pHit->TakeDamage(normalize(To - From) * 4.f, m_Pos, m_Damage, m_Owner, WEAPON_LASER);
	m_DamageTimer = 5;
	GameWorld()->CreateSound(m_Pos, SOUND_LASER_BOUNCE);
	SpawnSkillHitBurst(GameWorld(), m_Pos, 36.f, SKILL_VFX_ARCANE, Server()->TickSpeed() / 4, 6);
	return true;
}

void CSkillSpinLaser::SpinStep()
{
	m_DamageTimer = maximum(0, m_DamageTimer - 1);

	CCharacter *pOwnerChar = MMOWeaponOwnerChar(GameServer(), m_Owner);
	if(!pOwnerChar)
		return;

	const vec2 Center = pOwnerChar->GetPos();
	const vec2 OrbitPos = Center + direction(m_OrbitAngle) * m_OrbitRadius;
	const vec2 BeamDir = direction(m_OrbitAngle + pi / 2.f);
	const vec2 To = OrbitPos + BeamDir * m_BeamEnergy;

	vec2 HitTo = To;
	if(GameServer()->Collision()->IntersectLine(OrbitPos, To, 0x0, &HitTo))
	{
		if(!HitAlongBeam(OrbitPos, HitTo))
		{
			m_From = OrbitPos;
			m_Pos = HitTo;

			vec2 TempPos = m_Pos;
			vec2 TempDir = BeamDir * 4.0f;
			GameServer()->Collision()->MovePoint(&TempPos, &TempDir, 1.0f, 0);
			m_Pos = TempPos;
			m_Dir = normalize(TempDir);
		}
	}
	else
	{
		HitAlongBeam(OrbitPos, To);
		m_From = OrbitPos;
		m_Pos = To;
		m_Dir = BeamDir;
	}

	m_EvalTick = Server()->Tick();
}

void CSkillSpinLaser::Tick()
{
	CCharacter *pOwnerChar = MMOWeaponOwnerChar(GameServer(), m_Owner);
	if(!pOwnerChar)
	{
		MarkForDestroy();
		return;
	}

	m_OrbitAngle += 2.4f / (float)maximum(1, Server()->TickSpeed()) * 2.f * pi;
	SpinStep();

	if(--m_LifeSpan <= 0)
	{
		SpawnSkillHitBurst(GameWorld(), pOwnerChar->GetPos(), 72.f, SKILL_VFX_ARCANE);
		MarkForDestroy();
	}
}

void CSkillSpinLaser::TickPaused()
{
	++m_EvalTick;
}

void CSkillSpinLaser::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));
	if(!pObj)
		return;

	pObj->m_X = (int)m_Pos.x;
	pObj->m_Y = (int)m_Pos.y;
	pObj->m_FromX = (int)m_From.x;
	pObj->m_FromY = (int)m_From.y;
	pObj->m_StartTick = m_EvalTick;
}

#include "skill_fork_lightning.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>
#include <generated/server_data.h>
#include <game/server/gamecontext.h>

#include "../character.h"
#include "../mmo/mmo_weapon_common.h"

CSkillForkLightning::CSkillForkLightning(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, vec2 Direction, float StartEnergy, float StepEnergy, int Damage, int MaxForks, int ForkNum)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, 0, Pos, 0)
{
	m_Owner = OwnerCID;
	m_Damage = maximum(1, Damage);
	m_Energy = StartEnergy;
	m_StartEnergy = StartEnergy;
	m_StepEnergy = StepEnergy;
	m_MaxForks = clamp(MaxForks, 1, 3);
	m_ForkNum = ForkNum;
	m_Dir = normalize(Direction);
	if(length(m_Dir) < 0.01f)
		m_Dir = vec2(1.f, 0.f);
	m_EvalTick = 0;

	GameWorld()->InsertEntity(this);
	DoBounce();
}

bool CSkillForkLightning::HitCharacter(vec2 From, vec2 To)
{
	vec2 At;
	CCharacter *pOwnerChar = MMOWeaponOwnerChar(GameServer(), m_Owner);
	CCharacter *pHit = GameWorld()->IntersectCharacter(m_Pos, To, 0.f, At, pOwnerChar);
	if(!pHit || !MMOWeaponTargetValid(GameServer(), m_Owner, pHit))
		return false;

	m_From = From;
	m_Pos = At;
	m_Energy = -1;
	pHit->TakeDamage(vec2(0.f, 0.f), m_Pos, m_Damage, m_Owner, WEAPON_LASER);
	GameWorld()->CreateSound(m_Pos, SOUND_LASER_BOUNCE);
	return true;
}

void CSkillForkLightning::DoBounce()
{
	m_EvalTick = Server()->Tick();

	if(m_Energy < 0)
	{
		MarkForDestroy();
		return;
	}

	const float Jitter = 40.f;
	vec2 To = m_Pos + m_Dir * minimum(m_Energy, m_StepEnergy) + vec2(random_float() - random_float(), random_float() - random_float()) * Jitter;

	if(GameServer()->Collision()->IntersectLine(m_Pos, To, 0x0, &To))
	{
		if(!HitCharacter(m_Pos, To))
		{
			m_From = m_Pos;
			m_Pos = To;

			vec2 TempPos = m_Pos;
			vec2 TempDir = m_Dir * 4.0f;
			GameServer()->Collision()->MovePoint(&TempPos, &TempDir, 1.0f, 0);
			m_Pos = TempPos;
			m_Dir = normalize(TempDir);
			m_Energy -= m_StepEnergy;
		}
	}
	else
	{
		if(!HitCharacter(m_Pos, To))
		{
			m_From = m_Pos;
			m_Pos = To;
			m_Energy -= m_StepEnergy;

			if(m_ForkNum < m_MaxForks)
				new CSkillForkLightning(GameWorld(), m_Owner, m_Pos, m_Dir, m_StartEnergy, m_StepEnergy, m_Damage, m_MaxForks, m_ForkNum + 1);
		}
	}
}

void CSkillForkLightning::Tick()
{
	if(!MMOWeaponOwnerChar(GameServer(), m_Owner))
	{
		MarkForDestroy();
		return;
	}

	if(Server()->Tick() > m_EvalTick + (Server()->TickSpeed() * GameServer()->Tuning()->m_LaserBounceDelay) / 2000)
		DoBounce();
}

void CSkillForkLightning::TickPaused()
{
	++m_EvalTick;
}

void CSkillForkLightning::Snap(int SnappingClient)
{
	if(Config()->m_SvGESnapTime > 1 && rand() % Config()->m_SvGESnapTime != 0)
		return;
	if(NetworkClipped(SnappingClient))
		return;

	if(GameServer()->ClientUsesDDNetLaser(SnappingClient))
	{
		CNetObj_DDNetLaser *pObj = static_cast<CNetObj_DDNetLaser *>(Server()->SnapNewItem(NETOBJTYPE_DDNETLASER, GetID(), sizeof(CNetObj_DDNetLaser)));
		if(!pObj)
			return;

		pObj->m_ToX = (int)m_Pos.x;
		pObj->m_ToY = (int)m_Pos.y;
		pObj->m_FromX = (int)m_From.x;
		pObj->m_FromY = (int)m_From.y;
		pObj->m_StartTick = m_EvalTick;
		pObj->m_Owner = m_Owner;
		pObj->m_Type = rand() % NUM_LASERTYPES;
		pObj->m_SwitchNumber = -1;
		pObj->m_Subtype = -1;
		pObj->m_Flags = 0;
	}
	else
	{
		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));
		if(!pObj)
			return;

		pObj->m_X = (int)m_Pos.x;
		pObj->m_Y = (int)m_Pos.y;
		pObj->m_FromX = (int)m_From.x;
		pObj->m_FromY = (int)m_From.y;
		pObj->m_StartTick = m_EvalTick;
	}
}

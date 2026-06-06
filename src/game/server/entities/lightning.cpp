/* (c) Siile / TeeDefenceArchive */
#include <base/math.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>
#include <generated/server_data.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "character.h"
#include "lightning.h"

CLightning::CLightning(CGameWorld *pGameWorld, vec2 Pos, vec2 Direction, float StartEnergy, float StepEnergy, int Owner, int Damage, int Num)
	: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, CGameWorld::ENTFLAG_CHILD, Pos, 0)
{
	m_Damage = Damage;
	m_Owner = Owner;
	m_Energy = StartEnergy;
	m_StartEnergy = StartEnergy;
	m_StepEnergy = StepEnergy;
	m_Num = Num;
	m_Dir = Direction;
	m_Bounces = 0;
	m_EvalTick = 0;
	GameWorld()->InsertEntity(this);
	DoBounce();
}

bool CLightning::HitCharacter(vec2 From, vec2 To)
{
	vec2 At;
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *pHit = GameWorld()->IntersectCharacter(m_Pos, To, 0.f, At, pOwnerChar);
	if(!pHit || !pHit->GetPlayer())
		return false;

	m_From = From;
	m_Pos = At;
	m_Energy = -1;

	if(pHit->GetPlayer()->IsDummy() || pHit->GetPlayer()->GetZomb() > 0)
		pHit->TakeDamage(vec2(0.f, 0.f), m_Pos, m_Damage, m_Owner, WEAPON_LASER);
	else
		return false;

	return true;
}

void CLightning::DoBounce()
{
	m_EvalTick = Server()->Tick();

	if(m_Energy < 0)
	{
		GameWorld()->DestroyEntity(this);
		return;
	}

	vec2 To = m_Pos + m_Dir * minimum(m_Energy, m_StepEnergy) + vec2(random_float() * 50 - random_float() * 50, random_float() * 50 - random_float() * 50);

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
			m_Energy = -1;
		}
	}
	else
	{
		if(!HitCharacter(m_Pos, To))
		{
			m_From = m_Pos;
			m_Pos = To;
			m_Energy -= m_StepEnergy;
			if(m_Num < 1)
				new CLightning(GameWorld(), m_Pos, m_Dir, m_StartEnergy, m_StepEnergy, m_Owner, m_Damage, m_Num + 1);
		}
	}
}

void CLightning::Reset()
{
	GameWorld()->DestroyEntity(this);
}

void CLightning::Tick()
{
	if(Server()->Tick() > m_EvalTick + (Server()->TickSpeed() * GameServer()->Tuning()->m_LaserBounceDelay) / 2000)
		DoBounce();
}

void CLightning::TickPaused()
{
	++m_EvalTick;
}

void CLightning::Snap(int SnappingClient)
{
	if(Config()->m_SvGESnapTime > 1 && rand() % Config()->m_SvGESnapTime != 0)
		return;
	if(NetworkClipped(SnappingClient))
		return;

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

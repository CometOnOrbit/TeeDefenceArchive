#include "npc_ai.h"
#include <game/server/entities/character_bot_ai.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CNpcAI::CNpcAI(CCharacterBotAI *pCharacter, const SMMONpcInfo &NpcInfo)
	: CBaseAI(pCharacter), m_NpcInfo(NpcInfo)
{
	m_DefaultMoveDirection = (random_int() % 2 == 0) ? -1 : 1;
	m_DefaultMoveNextTick = Server()->Tick() + Server()->TickSpeed() * 2;
}

CNpcAI::CNpcAI(CCharacterBotAI *pCharacter, EMMONpcFunction Function)
	: CBaseAI(pCharacter)
{
	m_NpcInfo.m_Function = Function;
	m_NpcInfo.m_Static = (Function == EMMONpcFunction::Nurse);
	m_NpcInfo.m_GuardRadius = 800;
	m_DefaultMoveDirection = (random_int() % 2 == 0) ? -1 : 1;
	m_DefaultMoveNextTick = Server()->Tick() + Server()->TickSpeed() * 2;
}

bool CNpcAI::CanDamage(CPlayer *pFrom)
{
	if(m_NpcInfo.m_Function == EMMONpcFunction::Guardian)
	{
		return pFrom && !pFrom->m_pMMOBotData;
	}
	return false;
}

void CNpcAI::OnSpawn()
{
	switch(m_NpcInfo.m_Function)
	{
	case EMMONpcFunction::GiveQuest:
		m_pCharacter->SetEmote(EMOTE_HAPPY, 5);
		break;
	case EMMONpcFunction::Guardian:
		m_pCharacter->SetEmote(EMOTE_ANGRY, 5);
		break;
	default:
		m_pCharacter->SetEmote(EMOTE_BLINK, 5);
		break;
	}
}

void CNpcAI::OnTakeDamage(int Dmg, int From, int Weapon)
{
	if(m_NpcInfo.m_Function == EMMONpcFunction::Guardian)
	{
		m_Target.Set(From, 200);
		m_pCharacter->SetEmote(EMOTE_ANGRY, 3);
	}
}

void CNpcAI::OnTargetRules(float Radius)
{
	if(m_NpcInfo.m_Function == EMMONpcFunction::Guardian)
	{
		int CID = SearchNearestPlayer(Radius, true);
		if(CID >= 0)
			m_Target.Set(CID, 100);
	}
}

void CNpcAI::ProcessGuardianNPC()
{
	float DistToSpawn = distance(m_SpawnPoint, m_pCharacter->GetPos());

	if(DistToSpawn > (float)m_NpcInfo.m_GuardRadius && m_Target.IsEmpty())
	{
		MoveToward(m_SpawnPoint);
		return;
	}

	OnTargetRules((float)m_NpcInfo.m_GuardRadius);

	CGameContext *pGS = GS();
	if(!pGS) return;

	int TargetCID = m_Target.GetCID();
	CPlayer *pTarget = (TargetCID >= 0) ? pGS->m_apPlayers[TargetCID] : nullptr;
	CCharacter *pTargetChr = pTarget ? pTarget->GetCharacter() : nullptr;

	if(pTargetChr && pTargetChr->IsAlive())
	{
		vec2 MyPos = m_pCharacter->GetPos();
		vec2 TargetPos = pTargetChr->GetPos();
		float Dist = distance(MyPos, TargetPos);

		if(Dist < 200.f)
		{
			vec2 Dir = TargetPos - MyPos;
			if(Dist > 0.1f) Dir /= Dist;

			CNetObj_PlayerInput Input;
			mem_zero(&Input, sizeof(Input));
			Input.m_TargetX = (int)(Dir.x * 100.f);
			Input.m_TargetY = (int)(Dir.y * 100.f);
			Input.m_Fire = 1;
			m_pCharacter->SetBotInput(Input);
		}
		else
		{
			FollowPath(TargetPos, 200.f);
		}
	}
	else
	{
		m_Target.Reset();

		if(DistToSpawn < 256.f)
		{
			CNetObj_PlayerInput Input;
			mem_zero(&Input, sizeof(Input));
			if(Server()->Tick() % Server()->TickSpeed() == 0)
				Input.m_TargetY = (random_int() % 4) - 8;
			m_pCharacter->SetBotInput(Input);
		}
		else
		{
			MoveToward(m_SpawnPoint);
		}
	}
}

void CNpcAI::UpdateDefaultMovementDirection(bool HasPlayerNearby, bool HasGroundAhead)
{
	if(m_NpcInfo.m_Static || HasPlayerNearby || !HasGroundAhead)
	{
		CNetObj_PlayerInput Input;
		mem_zero(&Input, sizeof(Input));
		m_pCharacter->SetBotInput(Input);
		return;
	}

	CNetObj_PlayerInput Input;
	mem_zero(&Input, sizeof(Input));

	if(Server()->Tick() >= m_DefaultMoveNextTick)
	{
		if(random_int() % 5 == 0)
			m_DefaultMoveDirection = -m_DefaultMoveDirection;
		else if(random_int() % 3 == 0)
			m_DefaultMoveDirection = (random_int() % 2 == 0) ? -1 : 1;

		int MinTicks = Server()->TickSpeed() / 3;
		int MaxTicks = Server()->TickSpeed() * 2;
		m_DefaultMoveNextTick = Server()->Tick() + MinTicks + random_int() % MaxTicks;

		if(random_int() % 3 == 0)
			m_DefaultMoveDirection = 0;
	}

	Input.m_Direction = m_DefaultMoveDirection;
	Input.m_TargetX = Input.m_Direction * 10 + 1;
	m_pCharacter->SetBotInput(Input);
}

void CNpcAI::ProcessDefaultNPC()
{
	CGameContext *pGS = GS();
	if(!pGS) return;

	float CollisionWidth = (float)pGS->Collision()->GetWidth() * 32.0f;

	CNetObj_PlayerInput Input;
	mem_zero(&Input, sizeof(Input));

	bool HasPlayerNearby = false;
	int NearbyCID = SearchNearestPlayer(128.f, true);
	if(NearbyCID >= 0)
	{
		CPlayer *pNearby = pGS->m_apPlayers[NearbyCID];
		if(pNearby && pNearby->GetCharacter())
		{
			vec2 CandidatePos = pNearby->GetCharacter()->GetPos();
			vec2 SelfPos = m_pCharacter->GetPos();
			Input.m_TargetX = (int)(CandidatePos.x - SelfPos.x);
			Input.m_TargetY = (int)(CandidatePos.y - SelfPos.y);
			Input.m_Direction = 0;
			HasPlayerNearby = true;
		}
	}

	if(Server()->Tick() % Server()->TickSpeed() == 0)
		Input.m_TargetY = (random_int() % 9) - 4;

	const float PhysSize = CCharacter::ms_PhysSize;
	float ForwardX = m_pCharacter->GetPos().x + (float)Input.m_Direction * (PhysSize + 16.0f);
	float FootY = m_pCharacter->GetPos().y + PhysSize + 10.0f;
	bool HasGroundAhead = pGS->Collision()->CheckPoint(ForwardX + 8.0f, FootY)
		|| pGS->Collision()->CheckPoint(ForwardX - 8.0f, FootY);

	if(!HasPlayerNearby)
		Input.m_Direction = m_DefaultMoveDirection;

	float NpcPosX = m_pCharacter->GetPos().x + Input.m_Direction * 45.0f;
	if(NpcPosX < 0)
		Input.m_Direction = 1;
	else if(NpcPosX >= CollisionWidth)
		Input.m_Direction = -1;

	m_DefaultMoveDirection = Input.m_Direction;
	UpdateDefaultMovementDirection(HasPlayerNearby, HasGroundAhead);
}

void CNpcAI::Process()
{
	if(!m_pCharacter || !m_pCharacter->IsAlive()) return;

	switch(m_NpcInfo.m_Function)
	{
	case EMMONpcFunction::Guardian:
		ProcessGuardianNPC();
		return;
	case EMMONpcFunction::Nurse:
		return;
	default:
		ProcessDefaultNPC();
		return;
	}
}

bool CNpcAI::IsConversational()
{
	return m_NpcInfo.m_Function != EMMONpcFunction::Guardian
		&& m_NpcInfo.m_Function != EMMONpcFunction::Nurse;
}

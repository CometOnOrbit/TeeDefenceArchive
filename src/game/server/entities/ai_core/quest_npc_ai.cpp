#include "quest_npc_ai.h"
#include <game/server/entities/character_bot_ai.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CQuestNpcAI::CQuestNpcAI(CCharacterBotAI *pCharacter, SMMOQuestNpcInfo *pQuestNpcInfo)
	: CBaseAI(pCharacter), m_pQuestNpcInfo(pQuestNpcInfo) {}

bool CQuestNpcAI::CanDamage(CPlayer *pFrom)
{
	return false; // Quest NPCs cannot be damaged
}

void CQuestNpcAI::OnSpawn()
{
	m_pCharacter->SetEmote(EMOTE_BLINK, 5);

	if(m_pQuestNpcInfo->m_HasAction)
	{
		// Indicator: keep a happy emote
		m_pCharacter->SetEmote(EMOTE_HAPPY, 5);
	}
}

void CQuestNpcAI::Process()
{
	if(!m_pCharacter || !m_pCharacter->IsAlive()) return;

	CGameContext *pGS = GS();
	if(!pGS) return;

	CNetObj_PlayerInput Input;
	mem_zero(&Input, sizeof(Input));

	// Random aim Y oscillation
	if(Server()->Tick() % Server()->TickSpeed() == 0)
		Input.m_TargetY = (random_int() % 9) - 4;

	Input.m_TargetX = Input.m_Direction * 10 + 1;

	// Face nearby players
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
		}
	}

	m_pCharacter->SetBotInput(Input);
}

bool CQuestNpcAI::IsConversational()
{
	return m_pQuestNpcInfo->m_HasAction;
}

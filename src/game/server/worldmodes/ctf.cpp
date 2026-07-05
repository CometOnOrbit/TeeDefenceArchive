#include "ctf.h"

#include <game/collision.h>
#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/entities/flag.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

#include <generated/protocol.h>

namespace
{
enum
{
	CAPTURE_SCORE = 15,
	GRAB_SCORE = 1,
	RETURN_SCORE = 2,
	FLAG_KILL_SCORE = 2,
};
}

CGameControllerCTF::CGameControllerCTF(CGameContext *pGameServer)
	: CGameControllerTDM(pGameServer)
{
	m_apFlags[0] = nullptr;
	m_apFlags[1] = nullptr;
}

void CGameControllerCTF::OnCharacterSpawn(CCharacter *pChr)
{
	CGameControllerTDM::OnCharacterSpawn(pChr);
}

int CGameControllerCTF::OnCharacterDeath(CCharacter *pVictim, CPlayer *pKiller, int Weapon)
{
	if(!pVictim || !pVictim->GetPlayer())
	{
		CGameControllerTDM::OnCharacterDeath(pVictim, pKiller, Weapon);
		return 0;
	}

	for(int i = 0; i < 2; i++)
	{
		CFlag *pFlag = m_apFlags[i];
		if(!pFlag)
			continue;

		if(pFlag->GetCarrier() == pVictim)
		{
			GameServer()->SendGameMsg(GAMEMSG_CTF_DROP, -1);
			pFlag->Drop();

			if(pKiller && pKiller->GetCharacter() && pKiller->GetTeam() != pVictim->GetPlayer()->GetTeam())
				pKiller->m_Score += FLAG_KILL_SCORE;
		}
	}

	if(pVictim->GetPlayer())
		pVictim->GetPlayer()->m_RespawnTick = maximum(pVictim->GetPlayer()->m_RespawnTick, Server()->Tick() + Server()->TickSpeed() * 3);

	return 0;
}

void CGameControllerCTF::OnFlagReturn(CFlag *pFlag)
{
	(void)pFlag;
	GameServer()->SendGameMsg(GAMEMSG_CTF_RETURN, -1);
}

bool CGameControllerCTF::OnEntity(int Index, vec2 Pos)
{
	if(CGameControllerArena::OnEntity(Index, Pos))
		return true;

	int Team = -1;
	if(Index == ENTITY_FLAGSTAND_RED)
		Team = TEAM_RED;
	if(Index == ENTITY_FLAGSTAND_BLUE)
		Team = TEAM_BLUE;
	if(Team == -1 || m_apFlags[Team])
		return false;

	m_apFlags[Team] = new CFlag(&GameServer()->m_World, Team, Pos);
	return true;
}

void CGameControllerCTF::Snap(int SnappingClient)
{
	CGameController::Snap(SnappingClient);

	CNetObj_GameDataFlag *pGameDataFlag = static_cast<CNetObj_GameDataFlag *>(Server()->SnapNewItem(NETOBJTYPE_GAMEDATAFLAG, 0, sizeof(CNetObj_GameDataFlag)));
	if(!pGameDataFlag)
		return;

	pGameDataFlag->m_FlagDropTickRed = 0;
	if(m_apFlags[TEAM_RED])
	{
		if(m_apFlags[TEAM_RED]->IsAtStand())
			pGameDataFlag->m_FlagCarrierRed = FLAG_ATSTAND;
		else if(m_apFlags[TEAM_RED]->GetCarrier() && m_apFlags[TEAM_RED]->GetCarrier()->GetPlayer())
			pGameDataFlag->m_FlagCarrierRed = m_apFlags[TEAM_RED]->GetCarrier()->GetPlayer()->GetCID();
		else
		{
			pGameDataFlag->m_FlagCarrierRed = FLAG_TAKEN;
			pGameDataFlag->m_FlagDropTickRed = m_apFlags[TEAM_RED]->GetDropTick();
		}
	}
	else
		pGameDataFlag->m_FlagCarrierRed = FLAG_MISSING;

	pGameDataFlag->m_FlagDropTickBlue = 0;
	if(m_apFlags[TEAM_BLUE])
	{
		if(m_apFlags[TEAM_BLUE]->IsAtStand())
			pGameDataFlag->m_FlagCarrierBlue = FLAG_ATSTAND;
		else if(m_apFlags[TEAM_BLUE]->GetCarrier() && m_apFlags[TEAM_BLUE]->GetCarrier()->GetPlayer())
			pGameDataFlag->m_FlagCarrierBlue = m_apFlags[TEAM_BLUE]->GetCarrier()->GetPlayer()->GetCID();
		else
		{
			pGameDataFlag->m_FlagCarrierBlue = FLAG_TAKEN;
			pGameDataFlag->m_FlagDropTickBlue = m_apFlags[TEAM_BLUE]->GetDropTick();
		}
	}
	else
		pGameDataFlag->m_FlagCarrierBlue = FLAG_MISSING;
}

void CGameControllerCTF::Tick()
{
	CGameControllerHub::Tick();

	for(int fi = 0; fi < 2; fi++)
	{
		CFlag *pFlag = m_apFlags[fi];
		if(!pFlag)
			continue;

		if(pFlag->GetCarrier())
		{
			CFlag *pEnemyStand = m_apFlags[fi ^ 1];
			if(pEnemyStand && pEnemyStand->IsAtStand())
			{
				const float CaptureDist = (float)(CFlag::ms_PhysSize + CCharacter::ms_PhysSize);
				if(distance(pFlag->GetPos(), pEnemyStand->GetPos()) < CaptureDist)
				{
					CPlayer *pCarrier = pFlag->GetCarrier()->GetPlayer();
					if(pCarrier)
						pCarrier->m_Score += CAPTURE_SCORE;

					GameServer()->SendGameMsg(GAMEMSG_CTF_CAPTURE, fi, pCarrier ? pCarrier->GetCID() : -1, Server()->Tick() - pFlag->GetGrabTick(), -1);

					for(int i = 0; i < 2; i++)
					{
						if(m_apFlags[i])
							m_apFlags[i]->Reset();
					}
					continue;
				}
			}
		}
		else
		{
			array<CEntity *> aClose;
			GameServer()->m_World.FindEntities(pFlag->GetPos(), (float)CFlag::ms_PhysSize, aClose, CGameWorld::ENTTYPE_CHARACTER);
			for(int i = 0; i < aClose.size(); i++)
			{
				CCharacter *pChr = static_cast<CCharacter *>(aClose[i]);
				if(!pChr->IsAlive() || !pChr->GetPlayer() || pChr->GetPlayer()->GetTeam() == TEAM_SPECTATORS)
					continue;
				if(GameServer()->Collision()->IntersectLine(pFlag->GetPos(), pChr->GetPos(), nullptr, nullptr))
					continue;

				if(pChr->GetPlayer()->GetTeam() == pFlag->GetTeam())
				{
					if(!pFlag->IsAtStand())
					{
						pChr->GetPlayer()->m_Score += RETURN_SCORE;
						GameServer()->SendGameMsg(GAMEMSG_CTF_RETURN, -1);
						pFlag->Reset();
					}
				}
				else
				{
					if(pFlag->IsAtStand())
						pChr->GetPlayer()->m_Score += GRAB_SCORE;

					pFlag->Grab(pChr);
					GameServer()->SendGameMsg(GAMEMSG_CTF_GRAB, fi, -1);
					break;
				}
			}
		}
	}
}

#include "mmo_weapon_common.h"

#include <game/server/core/components/npcs/npc_manager.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entities/character.h>
#include <game/server/entities/character_bot_ai.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/player.h>

bool MMOWeaponTargetValid(CGameContext *pGS, int OwnerCID, CCharacter *pTarget)
{
	if(!pGS || !pTarget || !pTarget->IsAlive())
		return false;
	if(pTarget->GetCID() == OwnerCID)
		return false;

	CPlayer *pTargetPl = pTarget->GetPlayer();
	if(!pTargetPl)
		return false;
	if(pTargetPl->IsQuestNpc())
		return false;
	if(pGS->Core() && pGS->Core()->NpcManager() &&
		pGS->Core()->NpcManager()->IsQuestNpcCharacter(pTarget))
		return false;

	if(CCharacterBotAI *pBot = dynamic_cast<CCharacterBotAI *>(pTarget))
	{
		if(!pBot->IsAllowedPVP(OwnerCID))
			return false;
	}
	else if(pGS->m_pController && pGS->m_pController->IsFriendlyFire(pTargetPl->GetCID(), OwnerCID, 1))
		return false;

	return true;
}

CCharacter *MMOWeaponOwnerChar(CGameContext *pGS, int OwnerCID)
{
	if(OwnerCID < 0 || OwnerCID >= MAX_CLIENTS)
		return nullptr;
	CPlayer *pPl = pGS->m_apPlayers[OwnerCID];
	return pPl ? pPl->GetCharacter() : nullptr;
}

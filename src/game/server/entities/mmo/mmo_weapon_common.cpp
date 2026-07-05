#include "mmo_weapon_common.h"

#include <base/math.h>

#include <game/server/core/components/mmo/mmo_item.h>
#include <game/server/core/components/npcs/npc_manager.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entities/character.h>
#include <game/server/entities/character_bot_ai.h>
#include <game/server/entities/electro.h>
#include <game/server/entities/lightning.h>
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

void FireMMOLightningBolts(CGameContext *pGS, vec2 Pos, vec2 Dir, int OwnerCID, int Damage, const SMMOWeaponProfile *pProf)
{
	if(!pGS || length(Dir) < 0.001f)
		return;

	const float Reach = pProf && pProf->m_PulseReach > 0 ? (float)pProf->m_PulseReach : 200.f;
	const float StepEnergy = Reach * 0.5f;
	const int MaxDesc = pProf && pProf->m_TeslaChainTargets > 0 ? pProf->m_TeslaChainTargets : 1;
	const int BoltCount = pProf && pProf->m_FanShots > 0 ? pProf->m_FanShots : 1;
	const vec2 BaseDir = normalize(Dir);

	if(BoltCount <= 1)
	{
		new CLightning(&pGS->m_World, Pos, BaseDir, Reach, StepEnergy, OwnerCID, Damage, MaxDesc);
		return;
	}

	const float FanSpread = pProf && pProf->m_FanSpreadRad > 0
		? pProf->m_FanSpreadRad * pi / 180.f : 0.185f;
	for(int i = 0; i < BoltCount; i++)
	{
		const float Center = (BoltCount - 1) * 0.5f;
		const float a = angle(BaseDir) + (i - Center) * FanSpread;
		new CLightning(&pGS->m_World, Pos, vec2(cosf(a), sinf(a)), Reach, StepEnergy, OwnerCID, Damage, MaxDesc);
	}
}

void FireMMOElectroArc(CGameContext *pGS, CCharacter *pOwner, vec2 Start, vec2 Dir, int Damage, const SMMOWeaponProfile *pProf)
{
	if(!pGS || !pOwner || length(Dir) < 0.001f)
		return;

	const int OwnerCID = pOwner->GetCID();
	const float Reach = pProf && pProf->m_PulseReach > 0 ? (float)pProf->m_PulseReach : 400.f;
	const int ArcCount = pProf && pProf->m_FanShots >= 2 ? pProf->m_FanShots : 1;
	const vec2 BaseDir = normalize(Dir);

	auto FireSingleArc = [&](vec2 FireDir)
	{
		vec2 To = Start + FireDir * Reach;
		pGS->Collision()->IntersectLine(Start, To, 0x0, &To);

		vec2 At;
		CCharacter *pHit = pGS->m_World.IntersectCharacter(Start, To, 70.f, At, pOwner);
		if(pHit && MMOWeaponTargetValid(pGS, OwnerCID, pHit))
		{
			To = pHit->GetPos();
			pHit->TakeHit(FireDir, FireDir * -1, Damage, pOwner, WEAPON_LASER);
		}

		int Segments = distance(Start, To) / 100;
		Segments = clamp(Segments, 2, 4);
		const float a = angle(FireDir);
		new CElectro(&pGS->m_World, Start, To, vec2(cosf(a * 1.2f), sinf(a * 1.2f)) * 40.f, Segments);
	};

	if(ArcCount <= 1)
	{
		FireSingleArc(BaseDir);
		return;
	}

	for(int i = -1; i <= 1; i += 2)
	{
		float a = angle(BaseDir);
		a += i / 10.0f;
		FireSingleArc(vec2(cosf(a), sinf(a)));
	}
}

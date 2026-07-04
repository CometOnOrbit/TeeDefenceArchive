#include <engine/shared/config.h>

#include <game/server/core/components/content/status_manager.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

void CStatusManager::OnInitWorld(const char *pWhereLocalWorld)
{
	(void)pWhereLocalWorld;
	for(int i = 0; i < MAX_STATUS_SLOTS; i++)
	{
		for(int s = 0; s < MAX_CHARACTER_STATUSES; s++)
			m_aaStatuses[i][s].m_Active = false;
	}
}

void CStatusManager::OnClientReset(int ClientID)
{
	ClearCharacter(ClientID);
}

void CStatusManager::ClearCharacter(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_STATUS_SLOTS)
		return;
	for(int s = 0; s < MAX_CHARACTER_STATUSES; s++)
		m_aaStatuses[ClientID][s].m_Active = false;
}

SActiveStatus *CStatusManager::GetSlots(CCharacter *pChr)
{
	if(!pChr || !pChr->GetPlayer())
		return nullptr;
	const int CID = pChr->GetCID();
	if(CID < 0 || CID >= MAX_STATUS_SLOTS)
		return nullptr;
	return m_aaStatuses[CID];
}

int CStatusManager::FindSlot(SActiveStatus *pSlots, const char *pId) const
{
	if(!pSlots || !pId)
		return -1;
	for(int i = 0; i < MAX_CHARACTER_STATUSES; i++)
	{
		if(pSlots[i].m_Active && str_comp(pSlots[i].m_aId, pId) == 0)
			return i;
	}
	return -1;
}

int CStatusManager::FindFreeSlot(SActiveStatus *pSlots) const
{
	if(!pSlots)
		return -1;
	for(int i = 0; i < MAX_CHARACTER_STATUSES; i++)
	{
		if(!pSlots[i].m_Active)
			return i;
	}
	return -1;
}

void CStatusManager::ApplyStatus(CCharacter *pChr, const char *pStatusId, int Stacks, int DurationTicks, float SlowMul, int Amount)
{
	if(!pChr || !pStatusId || !pStatusId[0] || Stacks <= 0)
		return;

	SActiveStatus *pSlots = GetSlots(pChr);
	if(!pSlots)
		return;

	int Slot = FindSlot(pSlots, pStatusId);
	if(Slot < 0)
		Slot = FindFreeSlot(pSlots);
	if(Slot < 0)
		return;

	SActiveStatus &St = pSlots[Slot];
	if(!St.m_Active)
	{
		mem_zero(&St, sizeof(St));
		St.m_Active = true;
		str_copy(St.m_aId, pStatusId, sizeof(St.m_aId));
		St.m_TickInterval = 25;
		St.m_SlowMul = SlowMul > 0.01f ? SlowMul : 0.86f;
	}
	St.m_Stacks += Stacks;
	St.m_DurationTicks = maximum(St.m_DurationTicks, DurationTicks);
	if(Amount > 0)
		St.m_Amount += Amount;
}

bool CStatusManager::AbsorbDamage(CCharacter *pChr, int &Dmg)
{
	SActiveStatus *pSlots = GetSlots(pChr);
	if(!pSlots || Dmg <= 0)
		return false;

	const int Slot = FindSlot(pSlots, "shield");
	if(Slot < 0 || !pSlots[Slot].m_Active || pSlots[Slot].m_Amount <= 0)
		return false;

	SActiveStatus &St = pSlots[Slot];
	const int Absorb = minimum(Dmg, St.m_Amount);
	St.m_Amount -= Absorb;
	Dmg -= Absorb;
	if(St.m_Amount <= 0)
		St.m_Active = false;
	return Absorb > 0;
}

int CStatusManager::GetSlowTicks(CCharacter *pChr) const
{
	if(!pChr || !pChr->GetPlayer())
		return 0;
	const int CID = pChr->GetCID();
	if(CID < 0 || CID >= MAX_STATUS_SLOTS)
		return 0;

	for(int s = 0; s < MAX_CHARACTER_STATUSES; s++)
	{
		const SActiveStatus &St = m_aaStatuses[CID][s];
		if(St.m_Active && (str_comp(St.m_aId, "electron_slow") == 0 || str_comp(St.m_aId, "slow") == 0 || str_comp(St.m_aId, "frost") == 0))
			return St.m_DurationTicks;
	}
	return 0;
}

void CStatusManager::ProcessStatus(CCharacter *pChr, SActiveStatus &St)
{
	if(!St.m_Active || !pChr || !GS())
		return;

	const int Tick = GS()->Server()->Tick();
	if(str_comp(St.m_aId, "burn") == 0)
	{
		if(St.m_NextTickAt <= Tick)
		{
			St.m_NextTickAt = Tick + St.m_TickInterval;
			pChr->TakeDamage(vec2(0, 0), pChr->GetPos(), maximum(1, St.m_Stacks * 1), -1, WEAPON_GAME);
		}
	}
	else if(str_comp(St.m_aId, "bleed") == 0)
	{
		if(length(pChr->GetVelocity()) > 2.f && St.m_NextTickAt <= Tick)
		{
			St.m_NextTickAt = Tick + St.m_TickInterval;
			pChr->TakeDamage(vec2(0, 0), pChr->GetPos(), maximum(1, St.m_Stacks), -1, WEAPON_GAME);
		}
	}
	else if(str_comp(St.m_aId, "electron_slow") == 0 || str_comp(St.m_aId, "frost") == 0)
	{
		pChr->GetCore()->m_Vel *= St.m_SlowMul;
	}
	else if(str_comp(St.m_aId, "poison") == 0)
	{
		if(St.m_NextTickAt <= Tick)
		{
			St.m_NextTickAt = Tick + St.m_TickInterval;
			pChr->TakeDamage(vec2(0, 0), pChr->GetPos(), maximum(1, St.m_Stacks), -1, WEAPON_GAME);
		}
	}
	else if(str_comp(St.m_aId, "atk_boost") == 0)
	{
		// atk_boost is applied in battle_cry/enlighten
		// Effect is passive: stored in St.m_Amount (bonus damage added in FireWeapon)
		// No tick action needed, just duration tracking
		(void)0;
	}

	St.m_DurationTicks--;
	if(St.m_DurationTicks <= 0)
		St.m_Active = false;
}

void CStatusManager::TickCharacter(CCharacter *pChr)
{
	if(!GS() || !GS()->Config()->m_SvContentFramework || !pChr)
		return;

	SActiveStatus *pSlots = GetSlots(pChr);
	if(!pSlots)
		return;

	for(int s = 0; s < MAX_CHARACTER_STATUSES; s++)
	{
		if(pSlots[s].m_Active)
			ProcessStatus(pChr, pSlots[s]);
	}
}

void CStatusManager::OnTick()
{
	if(!GS() || !GS()->Config()->m_SvContentFramework)
		return;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GS()->m_apPlayers[i];
		if(!pP || !pP->GetCharacter() || !pP->GetCharacter()->IsAlive())
			continue;
		TickCharacter(pP->GetCharacter());
	}
}

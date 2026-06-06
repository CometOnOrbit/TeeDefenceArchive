/* (c) TeeDefenceArchive - 2026 */
#include <base/math.h>

#include <game/server/account.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <generated/protocol.h>
#include <generated/server_data.h>
#include <game/server/item_system.h>
#include <game/server/player.h>

#include "CKs.h"
#include "character.h"

static const int CK_BARE_HAND_DAMAGE = 10;

CKs::CKs(CGameWorld *pGameWorld, int Type, vec2 Pos)
	: CEntity(pGameWorld, CGameWorld::ENTTYPE_PICKUP, CGameWorld::ENTFLAG_CKS, Pos, ms_PhysSize)
{
	m_Type = Type;
	m_Pos = Pos;
	m_LockedPlayer = -1;
	m_Health = 0;
	Reset();
	GameWorld()->InsertEntity(this);
}

void CKs::Reset()
{
}

void CKs::HandleLock(CCharacter *pChr)
{
	if(!GameServer()->GetPlayerChar(m_LockedPlayer))
		m_LockedPlayer = -1;
	else if(!GameServer()->GetPlayerChar(m_LockedPlayer)->m_LockedCK)
		m_LockedPlayer = -1;

	if(!pChr)
		return;

	if(!pChr->m_LockedCK)
		m_LockedPlayer = -1;

	if(m_LockedPlayer == -1 && !pChr->m_LockedCK && pChr->GetPlayer()->PressTab())
	{
		pChr->m_LockedCK = true;
		m_LockedPlayer = pChr->GetPlayer()->GetCID();
		pChr->m_LockPos = GetPos();
	}
}

void CKs::Tick()
{
	if(m_Health == 0)
	{
		const int Cap = GetMaxHealth();
		m_Health = Cap > 0 ? Cap : 1;
	}

	CEntity *pEnt = GameServer()->m_World.ClosestEntity(GetPos(), 20.0f, CGameWorld::ENTTYPE_CHARACTER, 0);
	CCharacter *pChr = pEnt ? (CCharacter *)pEnt : nullptr;
	if(pChr && pChr->IsAlive() && !pChr->GetPlayer()->GetZomb())
	{
		HandleLock(pChr);

		if(pChr->LatestInput().m_Fire & 1 && pChr->GetActiveWeapon() == WEAPON_HAMMER && pChr->m_MiningTick <= 0)
		{
			int Tool = ITYPE_PICKAXE;
			if(m_Type == ITEM_LOG)
				Tool = ITYPE_AXE;

			pChr->m_InMining = true;
			GameServer()->m_World.CreateSound(m_Pos, SOUND_HAMMER_FIRE);

			const int HoldingId = pChr->GetPlayer()->m_AccData.m_Holding[Tool];
			const int BaseDmg = HoldingId ? GameServer()->ItemHelper()->GetDmg(HoldingId) : CK_BARE_HAND_DAMAGE;
			Picking(BaseDmg, pChr->GetPlayer());
		}
	}
}

void CKs::RewardIfDestroyed(CPlayer *pPlayer)
{
	if(!pPlayer || m_Health > 0)
		return;

	const int CID = pPlayer->GetCID();
	pPlayer->m_AccData.m_aItems[m_Type].m_Num++;
	GameServer()->SendChatLocF(CID, "mine.pickup", "You picked up %s", GameServer()->LocItemName(CID, m_Type));
	const int Cap = GetMaxHealth();
	m_Health = Cap > 0 ? Cap : 1;

	if(GameServer()->Accounts()->IsEnabled() && pPlayer->GetAccountId() >= 0)
		GameServer()->Accounts()->RequestSaveItems(CID);
}

void CKs::Picking(int BaseDmg, CPlayer *Player)
{
	int HoldKind = ITYPE_PICKAXE;
	if(m_Type == ITEM_LOG)
		HoldKind = ITYPE_AXE;

	const int HoldingId = Player->GetHolding(HoldKind);
	CItemHelper *pH = GameServer()->ItemHelper();
	const char *pHoldingExtra = HoldingId ? Player->GetExtraForItem(HoldingId) : "";

	int DmgPart;
	if(!HoldingId)
		DmgPart = CK_BARE_HAND_DAMAGE;
	else
	{
		const int CardDmg = pH ? pH->GetCard(pHoldingExtra, ITEM_CARD_DAMAGE_ID) : 0;
		DmgPart = BaseDmg * (1 + CardDmg);
		if(HoldKind == ITYPE_AXE && CardDmg == 0)
			DmgPart = BaseDmg * 2;
	}

	m_Health -= DmgPart;
	RewardIfDestroyed(Player);

	if(pH && pHoldingExtra)
	{
		const int Exp = pH->GetCard(pHoldingExtra, ITEM_CARD_EXPLOSION_ID);
		if(Exp > 0)
		{
			const int DmgC = maximum(1, pH->GetCard(pHoldingExtra, ITEM_CARD_DAMAGE_ID));
			const int Splash = maximum(1, (DmgPart * Exp) / 4 + Exp * DmgC);
			const float Radius = 72.f + (float)Exp * 6.f;

			for(CGameWorld::TypeRange r = GameServer()->m_World.DoTypeRange(CGameWorld::ENTTYPE_PICKUP); !r.empty(); r.pop_front())
			{
				CEntity *pE = r.front();
				if(!(pE->ObjFlag() & CGameWorld::ENTFLAG_CKS))
					continue;
				if(pE == this)
					continue;
				CKs *pCk = static_cast<CKs *>(pE);
				if(distance(pCk->m_Pos, m_Pos) > Radius)
					continue;
				pCk->m_Health -= Splash;
				pCk->RewardIfDestroyed(Player);
			}
			GameServer()->m_World.CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
		}
	}

	GameServer()->SendBroadcastLocF(Player->GetCID(), "mine.progress", "%s — %d / %d HP (hammer)",
		GameServer()->LocItemName(Player->GetCID(), m_Type), m_Health, GetMaxHealth());

	const int QFire = pH ? pH->GetCard(pHoldingExtra, ITEM_CARD_QUICKLY_FIRE_ID) : 0;
	const int MineCd = maximum(1, 25 - QFire);
	if(Player->GetCharacter())
		Player->GetCharacter()->m_MiningTick = MineCd;
}

void CKs::TickPaused()
{
}

void CKs::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, GetID(), sizeof(CNetObj_Pickup)));
	if(!pP)
		return;

	pP->m_X = round_to_int(m_Pos.x);
	pP->m_Y = round_to_int(m_Pos.y);
	if(m_Type == ITEM_LOG)
		pP->m_Type = PICKUP_HEALTH;
	else
		pP->m_Type = PICKUP_ARMOR;
}

int CKs::GetMaxHealth()
{
	return GameServer()->ItemHelper()->GetMaxHealth(m_Type);
}

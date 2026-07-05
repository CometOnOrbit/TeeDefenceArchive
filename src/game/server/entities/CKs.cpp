/* (c) TeeDefenceArchive - 2026 */
#include <base/math.h>

#include <game/server/account.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <generated/protocol.h>
#include <generated/server_data.h>
#include <game/server/item_system.h>
#include <game/server/player.h>

#include <engine/shared/config.h>

#include <game/server/core/components/content/content_types.h>
#include <game/server/core/components/content/effect_registry.h>
#include <game/server/core/components/content/trait_manager.h>
#include <game/server/core/components/meta/mini_events_manager.h>
#include <game/server/core/tworld_controller.h>

#include "CKs.h"
#include "character.h"

static const int CK_BARE_HAND_DAMAGE = 8;

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
			CPlayer *pPlayer = pChr->GetPlayer();
			CItemHelper *pH = GameServer()->ItemHelper();
			int Dmg = CK_BARE_HAND_DAMAGE;

			if(m_Type == ITEM_LOG)
			{
				const int AxeId = pPlayer->GetHolding(ITYPE_AXE);
				if(AxeId > 0 && pH)
					Dmg = maximum(1, pH->GetDmg(AxeId));
			}
			else
			{
				const int PickId = pPlayer->GetHolding(ITYPE_PICKAXE);
				if(PickId > 0 && pH)
				{
					Dmg = maximum(1, pH->GetDmg(PickId));
				}
			}

			pChr->m_InMining = true;
			GameServer()->m_World.CreateSound(m_Pos, SOUND_SFX_MINER);
			Picking(Dmg, pPlayer);
		}
	}
}

void CKs::RewardIfDestroyed(CPlayer *pPlayer)
{
	if(!pPlayer || m_Health > 0)
		return;

	const int CID = pPlayer->GetCID();
	vec2 Pos = m_Pos;
	int Num = 1;
	if(TWorldController *pCore = GameServer()->Core())
	{
		if(pCore->MiniEventsManager())
		{
			const int Bonus = pCore->MiniEventsManager()->GetMiningBonusPercent();
			if(Bonus > 0)
				Num = maximum(1, Num + Num * Bonus / 100);
		}
		if(pCore->EntityManager())
			pCore->EntityManager()->DropItem(Pos, CID, m_Type, Num);
		else
		{
			pPlayer->m_AccData.m_aItems[m_Type].m_Num++;
			GameServer()->SendChatLocF(CID, "mine.pickup", "You picked up %s", GameServer()->LocItemName(CID, m_Type));
			if(GameServer()->Accounts()->IsEnabled() && pPlayer->GetAccountId() >= 0)
				GameServer()->Accounts()->RequestSaveItems(CID);
		}
		pCore->Events().EmitPlayerMine(pPlayer, m_Type);
	}
	else
	{
		pPlayer->m_AccData.m_aItems[m_Type].m_Num++;
		GameServer()->SendChatLocF(CID, "mine.pickup", "You picked up %s", GameServer()->LocItemName(CID, m_Type));
		if(GameServer()->Accounts()->IsEnabled() && pPlayer->GetAccountId() >= 0)
			GameServer()->Accounts()->RequestSaveItems(CID);
	}
	const int Cap = GetMaxHealth();
	m_Health = Cap > 0 ? Cap : 1;
}

void CKs::Picking(int Dmg, CPlayer *Player)
{
	int EffectiveDmg = maximum(1, Dmg);
	int HoldKind = ITYPE_PICKAXE;
	if(m_Type == ITEM_LOG)
		HoldKind = ITYPE_AXE;

	const int HoldingId = Player->GetHolding(HoldKind);
	CItemHelper *pH = GameServer()->ItemHelper();
	const bool ContentOn = Config()->m_SvContentFramework && GameServer()->Core() && GameServer()->Core()->EffectRegistry();

	CEffectContext MainCtx = {};
	MainCtx.m_pPlayer = Player;
	MainCtx.m_InDamage = EffectiveDmg;

	if(pH && ContentOn)
	{
		const char *pHoldingExtra = HoldingId ? Player->GetExtraForItem(HoldingId) : nullptr;
		if(pHoldingExtra && pHoldingExtra[0])
		{
			MainCtx.m_pExtraJson = pHoldingExtra;
			GameServer()->Core()->EffectRegistry()->Apply(TRIGGER_MINE, MainCtx);

			// Aggregate mining effects from armor items into MainCtx
			const int ArmorTypes[] = {ITYPE_HELMET, ITYPE_CHEST, ITYPE_LEGS};
			for(int a = 0; a < 3; a++)
			{
				const int ArmorId = Player->GetHolding(ArmorTypes[a]);
				if(ArmorId <= 0)
					continue;
				const char *pArmorExtra = Player->GetExtraForItem(ArmorId);
				if(!pArmorExtra || !pArmorExtra[0])
					continue;
				CEffectContext ArmorCtx = {};
				ArmorCtx.m_pPlayer = Player;
				ArmorCtx.m_pExtraJson = pArmorExtra;
				ArmorCtx.m_InDamage = EffectiveDmg;
				GameServer()->Core()->EffectRegistry()->Apply(TRIGGER_MINE, ArmorCtx);
				MainCtx.m_MiningCritBonus += ArmorCtx.m_MiningCritBonus;
				MainCtx.m_OutDamage += ArmorCtx.m_OutDamage;
				MainCtx.m_OutMineCd += ArmorCtx.m_OutMineCd;
			}
		}

		// Legacy fallback for CD (cards lookup without JSON re-parse)
		if(!pHoldingExtra || !pHoldingExtra[0])
		{
			pHoldingExtra = HoldingId ? Player->GetExtraForItem(HoldingId) : "";
			if(pHoldingExtra && pHoldingExtra[0])
				MainCtx.m_OutMineCd = pH->GetCard(pHoldingExtra, ITEM_CARD_QUICKLY_FIRE_ID);
		}
		else if(Config()->m_SvContentLegacyCards || !Config()->m_SvContentFramework)
		{
			MainCtx.m_OutMineCd = pH->GetCard(pHoldingExtra, ITEM_CARD_QUICKLY_FIRE_ID);
		}
	}

	int CritChance = MainCtx.m_MiningCritBonus;
	if(GameServer()->Core() && GameServer()->Core()->TraitManager())
		CritChance += GameServer()->Core()->TraitManager()->GetMiningLuckBonus(Player);
	if(CritChance > 0 && (random_int() % 100) < CritChance)
		EffectiveDmg *= 2;

	EffectiveDmg = maximum(1, EffectiveDmg + MainCtx.m_OutDamage);

	m_Health -= EffectiveDmg;
	RewardIfDestroyed(Player);

	GameServer()->SendBroadcastLocF(Player->GetCID(), "mine.progress", "%s — %d / %d HP (hammer)",
		GameServer()->LocItemName(Player->GetCID(), m_Type), m_Health, GetMaxHealth());

	// Use cached CD reduction from the single Apply pass
	const int MineCd = maximum(1, 25 - MainCtx.m_OutMineCd);
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

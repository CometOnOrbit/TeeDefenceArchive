#include "rpg_ck.h"

#include <game/server/core/attribute_types.h>
#include <game/server/core/components/mmo/mmo_item.h>
#include <game/server/core/components/mmo/mmo_manager.h>
#include <game/server/core/components/content/trait_manager.h>
#include <game/server/core/components/meta/mini_events_manager.h>
#include <game/server/core/mmo_context.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/item_system.h>
#include <game/server/player.h>

#include <generated/protocol.h>
#include <generated/server_data.h>

CRpgCk::CRpgCk(CGameWorld *pGameWorld, GatheringNode *pNode, vec2 Pos, ERpgCkKind Kind)
	: CEntity(pGameWorld, CGameWorld::ENTTYPE_RPG_CK, 0, Pos, ms_PhysSize)
{
	m_pNode = pNode;
	m_Kind = Kind;
	m_RespawnEndTick = -1;
	if(m_Kind == ERpgCkKind::ORE)
		SnapToWall();
	Reset();
	GameWorld()->InsertEntity(this);
}

void CRpgCk::SnapToWall()
{
	constexpr int MaxProbe = 32;
	vec2 Best = m_Pos;
	float BestDist = (float)(MaxProbe * MaxProbe) + 1.f;
	const vec2 aDirs[] = {vec2(0.f, 1.f), vec2(0.f, -1.f), vec2(1.f, 0.f), vec2(-1.f, 0.f)};

	for(const vec2 &Dir : aDirs)
	{
		vec2 Hit;
		if(GameServer()->Collision()->IntersectLine(m_Pos, m_Pos + Dir * (float)MaxProbe, &Hit, nullptr))
		{
			const float Dist = distance(m_Pos, Hit);
			if(Dist < BestDist)
			{
				BestDist = Dist;
				Best = Hit;
			}
		}
	}
	m_Pos = Best;
}

void CRpgCk::Reset()
{
	if(!m_pNode)
		return;
	m_RespawnEndTick = -1;
	m_CurrentHealth = maximum(1, m_pNode->Health);
}

void CRpgCk::Tick()
{
	if(m_RespawnEndTick < 0)
		return;
	if(Server()->Tick() >= m_RespawnEndTick)
		Reset();
}

int CRpgCk::ComputeDamage(CPlayer *pPlayer)
{
	if(!pPlayer)
		return 1;

	if(m_Kind == ERpgCkKind::PLANT)
	{
		int Dmg = maximum(1, 1 + pPlayer->GetStat(AttributeIdentifier::WIS) / 4);
		Dmg += maximum(0, pPlayer->GetStat(AttributeIdentifier::Extraction));

		const int RakeId = pPlayer->m_EquippedSlots.getSlot(ItemType::EquipRake);
		if(RakeId > 0)
		{
			if(const CMMOItemDescription *pDesc = CMMOItemDescription::Get(RakeId))
				Dmg = maximum(Dmg, Dmg + pDesc->GetAttributeValue(AttributeIdentifier::Extraction));
		}
		else
		{
			const int AxeId = pPlayer->GetHolding(ITYPE_AXE);
			if(AxeId > 0)
			{
				if(CItemHelper *pH = GameServer()->ItemHelper())
					Dmg = maximum(Dmg, pH->GetDmg(AxeId));
			}
		}

		return maximum(1, Dmg);
	}

	int Dmg = maximum(1, 1 + pPlayer->GetStat(AttributeIdentifier::STR) / 4);

	const int PickId = pPlayer->GetHolding(ITYPE_PICKAXE);
	if(PickId > 0)
	{
		if(CItemHelper *pH = GameServer()->ItemHelper())
			Dmg = maximum(Dmg, pH->GetDmg(PickId));
	}

	if(TWorldController *pCore = GameServer()->Core())
	{
		if(pCore->TraitManager())
			Dmg = maximum(Dmg, Dmg + pCore->TraitManager()->GetMiningLuckBonus(pPlayer) / 10);
	}

	return maximum(1, Dmg);
}

bool CRpgCk::TakeHit(CPlayer *pPlayer)
{
	if(!pPlayer || !m_pNode || !IsActive() || m_pNode->m_vItems.empty())
		return false;

	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive() || pChr->m_MiningTick > 0)
		return false;

	const int CID = pPlayer->GetCID();
	const int Now = Server()->Tick();
	const int HintInterval = Server()->TickSpeed() * 3;

	const int PlayerLevel = maximum(1, pPlayer->GetStat(AttributeIdentifier::Level));
	if(PlayerLevel < m_pNode->Level)
	{
		if(Now - pPlayer->m_GatherHintTick >= HintInterval)
		{
			pPlayer->m_GatherHintTick = Now;
			if(m_Kind == ERpgCkKind::PLANT)
			{
				GameServer()->SendChatLocF(CID, "rpg_plant.level_low", "等级不足，需要 Lv%d 才能采集「%s」。",
					m_pNode->Level, m_pNode->Name.c_str());
			}
			else
			{
				GameServer()->SendChatLocF(CID, "rpg_ck.level_low", "等级不足，需要 Lv%d 才能开采「%s」。",
					m_pNode->Level, m_pNode->Name.c_str());
			}
		}
		return false;
	}

	if(m_Kind == ERpgCkKind::ORE)
	{
		const bool HasPick = pPlayer->m_EquippedSlots.isEquipped(ItemType::EquipPickaxe)
			|| pPlayer->GetHolding(ITYPE_PICKAXE) > 0;
		if(!HasPick && Now - pPlayer->m_GatherHintTick >= HintInterval)
		{
			pPlayer->m_GatherHintTick = Now;
			GameServer()->SendChatLoc(CID, "rpg_ck.need_pickaxe", "装备镐子可加快开采（ESC 菜单 → 装备）。");
		}
	}
	else
	{
		const bool HasRake = pPlayer->m_EquippedSlots.isEquipped(ItemType::EquipRake)
			|| pPlayer->GetHolding(ITYPE_AXE) > 0;
		if(!HasRake && Now - pPlayer->m_GatherHintTick >= HintInterval)
		{
			pPlayer->m_GatherHintTick = Now;
			GameServer()->SendChatLoc(CID, "rpg_plant.need_rake", "装备耙子可加快采集（ESC 菜单 → 装备）。");
		}
	}

	int Dmg = ComputeDamage(pPlayer);
	if(m_Kind == ERpgCkKind::ORE)
	{
		if(TWorldController *pCore = GameServer()->Core())
		{
			if(pCore->TraitManager())
			{
				const int Crit = pCore->TraitManager()->GetMiningLuckBonus(pPlayer);
				if(Crit > 0 && (random_int() % 100) < Crit)
					Dmg *= 2;
			}
		}
	}

	m_CurrentHealth -= Dmg;
	if(m_Kind == ERpgCkKind::PLANT)
		GameServer()->m_World.CreateSound(m_Pos, SOUND_SFX_FARMER);
	else
		GameServer()->m_World.CreateSound(m_Pos, SOUND_SFX_MINER);
	pChr->m_InMining = true;

	if(m_CurrentHealth <= 0)
		GrantLoot(pPlayer);
	else
	{
		const bool SendHud = m_LastHudClientId != CID || Now - m_LastHudTick >= Server()->TickSpeed() / 2;
		if(SendHud)
		{
			m_LastHudClientId = CID;
			m_LastHudTick = Now;
			if(m_Kind == ERpgCkKind::PLANT)
			{
				GameServer()->SendBroadcastLocF(CID, "rpg_plant.progress", "%s — %d / %d HP",
					m_pNode->Name.c_str(), m_CurrentHealth, maximum(1, m_pNode->Health));
			}
			else
			{
				GameServer()->SendBroadcastLocF(CID, "rpg_ck.progress", "%s — %d / %d HP",
					m_pNode->Name.c_str(), m_CurrentHealth, maximum(1, m_pNode->Health));
			}
		}
	}

	pChr->m_MiningTick = maximum(1, Server()->TickSpeed() / 4);
	return true;
}

void CRpgCk::GrantLoot(CPlayer *pPlayer)
{
	if(!pPlayer || !m_pNode)
		return;

	const int ItemId = m_pNode->m_vItems.PickRandomItem();
	if(ItemId < 0)
	{
		Reset();
		return;
	}

	int Amount = 1 + random_int() % 2;
	if(TWorldController *pCore = GameServer()->Core())
	{
		if(pCore->MiniEventsManager())
		{
			const int Bonus = m_Kind == ERpgCkKind::ORE
				? pCore->MiniEventsManager()->GetMiningBonusPercent()
				: pCore->MiniEventsManager()->GetLootBonusPercent();
			if(Bonus > 0)
				Amount = maximum(1, Amount + Amount * Bonus / 100);
		}
	}

	pPlayer->m_MMOInventory.Add(ItemId, Amount, 0);
	pPlayer->m_MMODirty = true;

	const CMMOItemDescription *pDesc = CMMOItemDescription::Get(ItemId);
	const char *pName = pDesc ? pDesc->m_aName : "?";
	if(m_Kind == ERpgCkKind::PLANT)
	{
		GameServer()->SendChatLocF(pPlayer->GetCID(), "rpg_plant.pickup", "获得 %d × %s", Amount, pName);
	}
	else
	{
		GameServer()->SendChatLocF(pPlayer->GetCID(), "rpg_ck.pickup", "获得 %d × %s", Amount, pName);
	}

	if(TWorldController *pCore = GameServer()->Core())
		pCore->Events().EmitPlayerMine(pPlayer, ItemId);

	if(CCharacter *pChr = pPlayer->GetCharacter())
	{
		pChr->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed() / 2);
		GameWorld()->CreateHammerHit(m_Pos);
	}

	m_RespawnEndTick = Server()->Tick() + Server()->TickSpeed() * 20;
	m_CurrentHealth = 0;
}

void CRpgCk::Snap(int SnappingClient)
{
	if(!IsActive() || !m_pNode || m_pNode->m_vItems.empty() || NetworkClipped(SnappingClient))
		return;

	CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, GetID(), sizeof(CNetObj_Pickup)));
	if(!pP)
		return;

	pP->m_X = round_to_int(m_Pos.x);
	pP->m_Y = round_to_int(m_Pos.y);
	pP->m_Type = m_Kind == ERpgCkKind::PLANT ? PICKUP_HEALTH : PICKUP_LASER;
}

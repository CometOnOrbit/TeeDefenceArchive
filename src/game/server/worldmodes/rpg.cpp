#include "rpg.h"

#include <game/collision.h>
#include <game/mapitems.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/core/components/npcs/npc_manager.h>
#include <game/server/entities/rpg_ck.h>
#include <game/server/entities/rpg_fishing.h>
#include <game/server/core/components/mmo/mmo_item.h>
#include <game/server/core/mmo_context.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>
#include <game/server/entities/character.h>
#include <engine/input_events.h>

CGameControllerRPG::CGameControllerRPG(CGameContext *pGameServer)
	: CGameControllerHub(pGameServer)
{
}

void CGameControllerRPG::Tick()
{
	TickLoginReminders();
	DoActivityCheck();
}

int CGameControllerRPG::OnCharacterDeath(CCharacter *pVictim, CPlayer *pKiller, int Weapon)
{
	CPlayer *pVictimPlayer = pVictim->GetPlayer();
	if(!pVictimPlayer) return 0;

	// MMO bot deaths: handled by CCharacterBotAI::OnMobDeath
	if(pVictimPlayer->m_pMMOBotData)
		return 0;

	if(!pVictimPlayer->IsDummy())
	{
		// Check if current world has a jail world — teleport there on death
		const int CurrentWorld = Server()->GetClientWorldID(pVictimPlayer->GetCID());
		const CWorldDetail *pDetail = Server()->GetWorldDetail(CurrentWorld);
		if(pDetail)
		{
			const int JailWorldID = pDetail->GetJailWorldID();
			if(JailWorldID >= 0 && JailWorldID != CurrentWorld)
			{
				// Send to jail world — player will respawn there
				vec2 JailSpawnPos(0, 0);
				pVictimPlayer->ChangeWorld(JailWorldID, &JailSpawnPos);
				GameServer()->SendChatTo(pVictimPlayer->GetCID(), "你被传送到了监狱。死亡后可在监狱中重获自由。");
				return 0;
			}
		}

		// Normal death: respawn timer
		pVictimPlayer->m_RespawnTick = Server()->Tick() + Server()->TickSpeed() * 3;
	}

	return 0;
}

void CGameControllerRPG::OnCharacterSpawn(CCharacter *pChr)
{
	CPlayer *pPlayer = pChr->GetPlayer();
	if(!pPlayer) return;

	// MMO bots: CCharacterBotAI handles its own spawn
	if(pPlayer->m_pMMOBotData)
		return;

	// Player spawn: basic MMO loadout (weapons applied via ApplyEquippedWeapon after spawn)
	pChr->IncreaseHealth(10);

	// First spawn: welcome MOTD + starter items
	if(pPlayer->m_MMOInventory.IsEmpty() || pPlayer->m_MMOInventory.size() <= 1)
	{
		// Welcome message
		GameServer()->SendChatTo(pPlayer->GetCID(), "━━━ 欢迎来到 TeeFun 世界 ━━━");
		GameServer()->SendChatTo(pPlayer->GetCID(), "");
		GameServer()->SendChatTo(pPlayer->GetCID(), "按下 ESC 打开菜单查看任务和背包");
		GameServer()->SendChatTo(pPlayer->GetCID(), "锤子敲 NPC 可对话");
		GameServer()->SendChatTo(pPlayer->GetCID(), "锤子敲矿脉/作物采集资源，钓点按 Fire 钓鱼");
		GameServer()->SendChatTo(pPlayer->GetCID(), "与怪物作战升级吧！");
		GameServer()->SendChatTo(pPlayer->GetCID(), "");
		GameServer()->SendChatTo(pPlayer->GetCID(), "你获得了新手装备");
		GameServer()->SendChatTo(pPlayer->GetCID(), "━━━━━━━━━━━━━━━━━━");

		// Give starter items
		// 新手匕首 (ID 7) - only if not already owned
		if(pPlayer->m_MMOInventory.FindByID(7) == -1)
		{
			pPlayer->m_MMOInventory.Add(7, 1, 0);
		}
		// 小型生命药水 x5 (ID 10)
		pPlayer->m_MMOInventory.Add(10, 5, 0);
		pPlayer->m_MMODirty = true;
	}
}

void CGameControllerRPG::OnEntitySwitch(int EntityIndex, vec2 Pos, int Flags, int Number)
{
	(void)Flags;
	GatheringNode *pNode = nullptr;
	ERpgCkKind Kind = ERpgCkKind::ORE;

	if(EntityIndex == ENTITY_ORE)
		pNode = GameServer()->Collision()->GetOreNode(Number);
	else if(EntityIndex == ENTITY_PLANT)
	{
		pNode = GameServer()->Collision()->GetPlantNode(Number);
		Kind = ERpgCkKind::PLANT;
	}
	else
		return;

	if(!pNode || pNode->m_vItems.empty())
		return;

	new CRpgCk(&GameServer()->m_World, pNode, Pos, Kind);
}

static bool HasFishrodEquipped(CPlayer *pPlayer)
{
	if(!pPlayer)
		return false;

	if(pPlayer->m_EquippedSlots.isEquipped(ItemType::EquipFishrod))
		return true;

	for(const CMMOItem &Item : pPlayer->m_MMOInventory)
	{
		if(const CMMOItemDescription *pDesc = CMMOItemDescription::Get(Item.m_ItemID);
			pDesc && pDesc->GetType() == ItemType::EquipFishrod)
			return true;
	}
	return false;
}

void CGameControllerRPG::HandleCharacterTiles(CCharacter *pChr, vec2 LastPos, vec2 NewPos)
{
	(void)LastPos;
	(void)NewPos;
	if(!pChr || !pChr->GetPlayer() || pChr->GetPlayer()->IsDummy())
		return;

	const bool OnFishingTile = pChr->IsTileActive(TILE_FISHING_MODE);
	const bool EnterFishingTile = pChr->IsTileEnter(TILE_FISHING_MODE);
	const bool ExitFishingTile = pChr->IsTileExit(TILE_FISHING_MODE);
	if(!OnFishingTile && !EnterFishingTile && !ExitFishingTile)
		return;

	CPlayer *pPlayer = pChr->GetPlayer();

	if(EnterFishingTile)
	{
		const bool FishingRodEquipped = HasFishrodEquipped(pPlayer);
		if(!FishingRodEquipped)
			GameServer()->SendBroadcastLoc(pPlayer->GetCID(), "rpg_fish.tile_no_rod", "钓点：请先在 ESC 菜单装备鱼竿。");
		else
			GameServer()->SendBroadcastLoc(pPlayer->GetCID(), "rpg_fish.tile_enter", "钓点 — Fire 抛竿，Vote Yes 自动收线。");
	}

	if(ExitFishingTile)
	{
		pChr->m_AutoFishingEnabled = false;
		if(pChr->m_pFishingRod)
			delete pChr->m_pFishingRod;
		GameServer()->SendBroadcastLoc(pPlayer->GetCID(), "rpg_fish.tile_exit", "已离开钓点。");
		return;
	}

	if(!OnFishingTile)
		return;

	const bool FishingRodEquipped = HasFishrodEquipped(pPlayer);
	if(!FishingRodEquipped)
		return;

	auto ResetWaitingRod = [pChr]()
	{
		if(pChr->m_pFishingRod)
			delete pChr->m_pFishingRod;
	};

	bool FirePressed = false;

	if(!pChr->m_pFishingRod || pChr->m_pFishingRod->IsWaitingState())
	{
		if(Server()->Input()->IsKeyClicked(pPlayer->GetCID(), KEY_EVENT_VOTE_YES))
		{
			if(!pChr->m_AutoFishingEnabled)
			{
				pChr->m_AutoFishingEnabled = true;
				GameServer()->SendChatLoc(pPlayer->GetCID(), "rpg_fish.auto_on", "自动钓鱼已开启 — 收线每秒自动触发。");
			}
			ResetWaitingRod();
		}
		else if(Server()->Input()->IsKeyClicked(pPlayer->GetCID(), KEY_EVENT_FIRE))
		{
			if(pChr->m_AutoFishingEnabled)
			{
				pChr->m_AutoFishingEnabled = false;
				GameServer()->SendChatLoc(pPlayer->GetCID(), "rpg_fish.auto_off", "自动钓鱼已关闭 — 收线需按 Fire。");
			}
			ResetWaitingRod();
			FirePressed = true;
		}
	}

	if(pChr->m_AutoFishingEnabled || FirePressed)
	{
		if(!pChr->m_pFishingRod)
		{
			const float ForceX = pChr->LatestInput().m_TargetX * 0.08f;
			const float ForceY = pChr->LatestInput().m_TargetY * 0.08f;
			new CEntityFishingRod(&GameServer()->m_World, pPlayer->GetCID(), pChr->GetPos(),
				vec2(ForceX, ForceY), pChr->m_AutoFishingEnabled);
		}
	}
}

int CGameControllerRPG::OnCharacterFireWeapon(CCharacter *pChr, vec2 Direction, int Weapon)
{
	// RPG gathering CK (hammer mine/gather, MRPG-style)
	if(Weapon == WEAPON_HAMMER && pChr && pChr->GetPlayer() && !pChr->GetPlayer()->IsDummy())
	{
		array<CEntity *> apEnts;
		GameServer()->m_World.FindEntities(pChr->GetPos(), 48.f, apEnts, CGameWorld::ENTTYPE_RPG_CK);

		vec2 Dir = Direction;
		if(length(Dir) < 0.001f)
			Dir = vec2(pChr->LatestInput().m_TargetX, pChr->LatestInput().m_TargetY);
		if(length(Dir) < 0.001f)
			Dir = vec2(1.f, 0.f);
		Dir = normalize(Dir);

		CRpgCk *pBest = nullptr;
		float BestScore = -1.f;
		for(int i = 0; i < apEnts.size(); i++)
		{
			CRpgCk *pCk = dynamic_cast<CRpgCk *>(apEnts[i]);
			if(!pCk)
				continue;

			const vec2 ToNode = pCk->GetPos() - pChr->GetPos();
			const float Dist = length(ToNode);
			if(Dist > 48.f)
				continue;

			const float Aim = dot(Dir, normalize(ToNode));
			if(Aim < 0.25f)
				continue;

			const float Score = Aim * (1.f / maximum(Dist, 8.f));
			if(Score > BestScore)
			{
				BestScore = Score;
				pBest = pCk;
			}
		}

		if(pBest && pBest->TakeHit(pChr->GetPlayer()))
			return maximum(1, Server()->TickSpeed() / 4);
	}

	// Training dummy / NPC hammer — radius query instead of full character scan
	if(Weapon == WEAPON_HAMMER && pChr && pChr->GetPlayer())
	{
		const vec2 ChrPos = pChr->GetPos();
		const vec2 ProjStartPos = ChrPos + Direction * 40.0f;

		array<CEntity *> apEnts;
		GameServer()->m_World.FindEntities(ChrPos, 64.f, apEnts, CGameWorld::ENTTYPE_CHARACTER);
		for(int i = 0; i < apEnts.size(); i++)
		{
			CCharacter *pTarget = static_cast<CCharacter *>(apEnts[i]);
			if(!pTarget || pTarget == pChr || !pTarget->GetPlayer() || !pTarget->IsAlive())
				continue;

			// Check range
			const float Dist = distance(pTarget->GetPos(), ChrPos);
			if(Dist > 64.0f)
				continue;

			// Check line of sight
			if(GameServer()->Collision()->IntersectLine(ProjStartPos, pTarget->GetPos(), nullptr, nullptr))
				continue;

			// Check if this is a training dummy NPC
			const char *pNpcId = nullptr;
			if(GameServer()->Core() && GameServer()->Core()->NpcManager())
				pNpcId = GameServer()->Core()->NpcManager()->NpcIdForClient(pTarget->GetPlayer()->GetCID());

			if(pNpcId && (str_find_nocase(pNpcId, "train") || str_find_nocase(pNpcId, "dummy")))
			{
				// Track damage
				const int CID = pChr->GetPlayer()->GetCID();
				STrainingDummyTracker &Tracker = m_aDummyTrackers[CID];

				if(Tracker.m_ClientID != CID)
				{
					Tracker.m_ClientID = CID;
					Tracker.m_StartTick = Server()->Tick();
					Tracker.m_LastHitTick = Server()->Tick();
					Tracker.m_TotalDamage = 0;
				}

				// Damage based on player level
				int Damage = 10 + GameServer()->m_apPlayers[CID]->m_MMOLevel * 5;
				Tracker.m_TotalDamage += Damage;
				Tracker.m_LastHitTick = Server()->Tick();

				// Show DPS
				int Elapsed = maximum(1, (Server()->Tick() - Tracker.m_StartTick));
				int DPS = Tracker.m_TotalDamage * Server()->TickSpeed() / Elapsed;

				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "\xe2\x94\x83 \xe2\x9a\x94 Training Dummy  \xe2\x94\x83 Damage: %d  \xe2\x94\x83 DPS: %d  \xe2\x94\x83 Total: %d", Damage, DPS, Tracker.m_TotalDamage);
				GameServer()->SendBroadcast(CID, aBuf);

				// Visual feedback
				pTarget->SetEmote(EMOTE_PAIN, Server()->Tick() + Server()->TickSpeed() / 3);

				return Server()->TickSpeed() / 6; // Fast cooldown for training
			}

			// If it's a regular NPC, delegate to hub for dialog
			if(GameServer()->Core() && GameServer()->Core()->NpcManager())
			{
				if(GameServer()->Core()->NpcManager()->TryHammerTalk(pChr, ProjStartPos))
					return Server()->TickSpeed() / 3;
			}
		}
	}

	return CGameControllerHub::OnCharacterFireWeapon(pChr, Direction, Weapon);
}

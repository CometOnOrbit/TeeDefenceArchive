#include "rpg.h"

#include <game/server/core/tworld_controller.h>
#include <game/server/core/components/npcs/npc_manager.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/entities/character.h>

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
		GameServer()->SendChatTo(pPlayer->GetCID(), "按下 F1 打开菜单查看任务和背包");
		GameServer()->SendChatTo(pPlayer->GetCID(), "锤子敲 NPC 可对话");
		GameServer()->SendChatTo(pPlayer->GetCID(), "消灭史莱姆升级吧！");
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

int CGameControllerRPG::OnCharacterFireWeapon(CCharacter *pChr, vec2 Direction, int Weapon)
{
	// Training dummy detection
	if(Weapon == WEAPON_HAMMER && pChr && pChr->GetPlayer())
	{
		const vec2 ChrPos = pChr->GetPos();
		const vec2 ProjStartPos = ChrPos + Direction * 40.0f;

		// Look for NPCs tagged as training dummies nearby
		for(CGameWorld::TypeRange r = GameServer()->m_World.DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
		{
			CCharacter *pTarget = static_cast<CCharacter *>(r.front());
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

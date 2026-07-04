#include "mmo_world_boss.h"
#include "mmo_manager.h"
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/entities/character.h>
#include <game/commands.h>
#include <game/server/entities/character_bot_ai.h>
#include <game/server/core/tworld_controller.h>
#include <engine/shared/config.h>
#include <base/math.h>

// ─── CWorldBossManager ────────────────────────────────────────────────

CWorldBossManager::CWorldBossManager()
{
}

CWorldBossManager::~CWorldBossManager()
{
	if(Core())
		Core()->Events().Unregister(this);
}

void CWorldBossManager::OnPreInit()
{
	// Nothing to pre-init
}

void CWorldBossManager::OnInitWorld(const char *pWhereLocalWorld)
{
	(void)pWhereLocalWorld;
	if(Core())
	{
		Core()->Events().Register(this);
		m_WorldID = GS()->GetWorldID();
	}

	// Schedule first boss spawn at random offset (5-15 minutes after world init)
	const int TicksUntilSpawn = Server()->TickSpeed() * (300 + random_int() % 600);
	m_NextSpawnTick = Server()->Tick() + TicksUntilSpawn;

	// Find spawn position for the boss
	FindBossSpawnPos();

	dbg_msg("world_boss", "WorldBoss initialized in world %d, first spawn in %d ticks",
		m_WorldID, TicksUntilSpawn);
}

void CWorldBossManager::OnShutdown()
{
	if(Core())
		Core()->Events().Unregister(this);

	if(m_IsAlive)
		DespawnBoss();
}

void CWorldBossManager::OnTick()
{
	// Check if it's time to spawn the boss
	if(!m_IsAlive && Server()->Tick() >= m_NextSpawnTick)
	{
		SpawnBoss();
		return;
	}

	// Boss AI tick
	if(m_IsAlive && m_BossClientID >= 0)
	{
		CPlayer *pBoss = GS()->m_apPlayers[m_BossClientID];
		if(pBoss && pBoss->GetCharacter())
		{
			// Update boss HP from character state
			m_BossHP = maximum(1, pBoss->GetCharacter()->GetHealth());
			m_BossMaxHP = pBoss->GetCharacter()->GetMaxHealth();

			TickBossAI(pBoss->GetCharacter());

			// Periodically broadcast boss HP (every 100 ticks ≈ 10s)
			if((Server()->Tick() % 100) == 0)
				BroadcastBossStatus();
		}
		else if(pBoss && !pBoss->GetCharacter())
		{
			// Boss character was destroyed but we're still "alive" — respawn
			dbg_msg("world_boss", "Boss character lost, despawning and scheduling respawn");
			DespawnBoss();
			m_NextSpawnTick = Server()->Tick() + RESPAWN_INTERVAL;
		}
	}
}

void CWorldBossManager::OnCharacterDeath(CPlayer *pVictim, CPlayer *pKiller, int Weapon)
{
	(void)Weapon;
	if(!pVictim)
		return;

	if(pVictim->m_IsWorldBoss)
	{
		dbg_msg("world_boss", "World Boss killed by CID=%d (Account=%lld)",
			pKiller ? pKiller->GetCID() : -1,
			pKiller ? pKiller->GetAccountId() : 0LL);

		DistributeRewards(pKiller);
		DespawnBoss();
		m_NextSpawnTick = Server()->Tick() + RESPAWN_INTERVAL;

		// Broadcast boss defeat
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf),
			"🐉 世界 Boss 已被击败！击杀者：%s",
			pKiller ? Server()->ClientName(pKiller->GetCID()) : "未知");
		GS()->BroadcastWorldMsg(m_WorldID, CGameContext::BROADCAST_PRIORITY_CRITICAL, 300, aBuf);
	}
}

void CWorldBossManager::RecordDamage(int BossCID, int AttackerCID, int Damage)
{
	if(BossCID != m_BossClientID || !m_IsAlive)
		return;

	CPlayer *pAttacker = GS()->m_apPlayers[AttackerCID];
	if(!pAttacker || pAttacker->IsDummy())
		return;

	// Find existing entry or add new one
	for(auto &Entry : m_aDamage)
	{
		if(Entry.ClientID == AttackerCID)
		{
			Entry.Damage += Damage;
			return;
		}
	}

	// New entry
	SPlayerDamage Entry;
	Entry.ClientID = AttackerCID;
	Entry.Damage = Damage;
	Entry.AccountID = pAttacker->GetAccountId();
	m_aDamage.push_back(Entry);
}

bool CWorldBossManager::IsWorldBoss(CPlayer *pPlayer) const
{
	return pPlayer && pPlayer->m_IsWorldBoss;
}

bool CWorldBossManager::IsWorldBossCharacter(CCharacter *pChar) const
{
	if(!pChar || !pChar->GetPlayer())
		return false;
	return pChar->GetPlayer()->m_IsWorldBoss;
}

int CWorldBossManager::GetNextSpawnInTicks() const
{
	if(m_IsAlive)
		return 0;
	return maximum(0, m_NextSpawnTick - Server()->Tick());
}

// ─── Internal Methods ──────────────────────────────────────────────────

void CWorldBossManager::FindBossSpawnPos()
{
	// Try to find a good spawn position near the world center
	// or use a default position relative to the map
	CGameContext *pGS = GS();
	if(!pGS)
	{
		m_BossSpawnPos = vec2(800, 400);
		return;
	}

	// Try using a fixed position near map center
	// The Collision class gives us map dimensions via width/height
	if(pGS->Collision())
	{
		int MapWidth = pGS->Collision()->GetWidth() * 32;
		int MapHeight = pGS->Collision()->GetHeight() * 32;
		m_BossSpawnPos = vec2((float)(MapWidth / 2), (float)(MapHeight / 2));

		// Adjust spawn to be on solid ground by checking if position is inside a wall
		if(pGS->Collision()->CheckPoint(m_BossSpawnPos))
		{
			// Search for a free spot nearby
			for(int dy = -320; dy <= 320; dy += 64)
			{
				vec2 TestPos = m_BossSpawnPos + vec2(0, (float)dy);
				if(!pGS->Collision()->CheckPoint(TestPos))
				{
					// Check a few tiles below for ground
					if(pGS->Collision()->CheckPoint(TestPos + vec2(0, 48)))
					{
						m_BossSpawnPos = TestPos;
						break;
					}
				}
			}
		}
	}
	else
	{
		m_BossSpawnPos = vec2(800, 400);
	}

	dbg_msg("world_boss", "Boss spawn pos: (%.0f, %.0f)", m_BossSpawnPos.x, m_BossSpawnPos.y);
}

void CWorldBossManager::SpawnBoss()
{
	CGameContext *pGS = GS();
	if(!pGS) return;

	const int BossCID = BOSS_SLOT;

	// Don't spawn if slot is occupied by a real player
	if(pGS->m_apPlayers[BossCID] && !pGS->m_apPlayers[BossCID]->IsDummy())
	{
		dbg_msg("world_boss", "Boss slot %d occupied by real player, delaying spawn", BossCID);
		m_NextSpawnTick = Server()->Tick() + RESPAWN_INTERVAL / 2;
		return;
	}

	// Clean up existing state on this slot
	if(pGS->m_apPlayers[BossCID])
	{
		delete pGS->m_apPlayers[BossCID]->m_pMMOBotData;
		pGS->m_apPlayers[BossCID]->m_pMMOBotData = 0;
		pGS->Server()->DummyRemove(BossCID);
		delete pGS->m_apPlayers[BossCID];
		pGS->m_apPlayers[BossCID] = 0;
	}

	// Create boss player
	pGS->Server()->DummyJoin(BossCID, "🐉 世界 Boss", m_WorldID);
	CPlayer *pBoss = pGS->m_apPlayers[BossCID];
	if(!pBoss)
	{
		dbg_msg("world_boss", "Failed to create boss player");
		m_NextSpawnTick = Server()->Tick() + RESPAWN_INTERVAL / 2;
		return;
	}

	// Mark as world boss
	pBoss->m_IsWorldBoss = true;

	// Set boss appearance
	pBoss->SetTeam(TEAM_BLUE);
	str_copy(pBoss->m_TeeInfos.m_aaSkinPartNames[0], "redstripe", sizeof(pBoss->m_TeeInfos.m_aaSkinPartNames[0]));
	pBoss->m_TeeInfos.m_aUseCustomColors[0] = 1;
	pBoss->m_TeeInfos.m_aSkinPartColors[0] = 0xFF0000; // Red body
	pBoss->m_TeeInfos.m_aUseCustomColors[1] = 1;
	pBoss->m_TeeInfos.m_aSkinPartColors[1] = 0xFF0000; // Red feet

	pGS->BroadcastClientInfo(BossCID, false);

	// Allocate bot data so other systems recognise this as a bot
	SMMOBotData *pData = new SMMOBotData();
	pData->m_Level = 50;
	pData->m_MaxHP = BOSS_MAX_HP;
	pData->m_HP = BOSS_MAX_HP;
	pData->m_Attack = BOSS_BASE_ATTACK;
	pData->m_Defense = 30;
	pData->m_SpawnPos = m_BossSpawnPos;
	pData->m_IsBoss = true;
	pBoss->m_pMMOBotData = pData;

	// Spawn as CCharacterBotAI (subclass of CCharacter, has AI hooks)
	CCharacterBotAI *pChr = new(BossCID) CCharacterBotAI(&pGS->m_World);
	if(!pChr || !pChr->Spawn(pBoss, m_BossSpawnPos))
	{
		dbg_msg("world_boss", "Failed to spawn boss character");
		if(pChr) delete pChr;
		pBoss->m_IsWorldBoss = false;
		delete pBoss->m_pMMOBotData;
		pBoss->m_pMMOBotData = 0;
		pGS->Server()->DummyRemove(BossCID);
		delete pGS->m_apPlayers[BossCID];
		pGS->m_apPlayers[BossCID] = 0;
		m_NextSpawnTick = Server()->Tick() + RESPAWN_INTERVAL / 2;
		return;
	}

	// Give boss weapons
	pChr->GiveWeapon(WEAPON_HAMMER, -1);  // Melee
	pChr->GiveWeapon(WEAPON_GUN, -1);      // Ranged
	pChr->GiveWeapon(WEAPON_GRENADE, -1);  // AoE
	pChr->SetForcedWeapon(WEAPON_HAMMER);

	// Set boss health using public accessors
	pChr->SetBossHealth(BOSS_MAX_HP);
	pChr->SetMaxHealth(BOSS_MAX_HP);

	// Update state
	m_IsAlive = true;
	m_BossClientID = BossCID;
	m_BossHP = BOSS_MAX_HP;
	m_BossMaxHP = BOSS_MAX_HP;
	m_SpawnTick = Server()->Tick();
	m_LastBossAttackTick = 0;

	// Reset damage tracking
	ResetDamageTracking();

	dbg_msg("world_boss", "World Boss spawned at (%.0f, %.0f) with %d HP",
		m_BossSpawnPos.x, m_BossSpawnPos.y, BOSS_MAX_HP);

	// Broadcast boss spawn
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf),
		"🐉 世界 Boss 已降临！HP: %d/%d 前往世界坐标击败它获得丰厚奖励！",
		BOSS_MAX_HP, BOSS_MAX_HP);
	GS()->BroadcastWorldMsg(m_WorldID, CGameContext::BROADCAST_PRIORITY_CRITICAL, 300, aBuf);
}

void CWorldBossManager::DespawnBoss()
{
	if(m_BossClientID < 0)
		return;

	CGameContext *pGS = GS();
	if(!pGS) return;

	// Clean up boss
	if(pGS->m_apPlayers[m_BossClientID])
	{
		delete pGS->m_apPlayers[m_BossClientID]->m_pMMOBotData;
		pGS->m_apPlayers[m_BossClientID]->m_pMMOBotData = 0;
		pGS->Server()->DummyRemove(m_BossClientID);
		delete pGS->m_apPlayers[m_BossClientID];
		pGS->m_apPlayers[m_BossClientID] = 0;
	}

	m_IsAlive = false;
	m_BossClientID = -1;
	m_BossHP = 0;
}

void CWorldBossManager::TickBossAI(CCharacter *pBoss)
{
	if(!pBoss || !pBoss->IsAlive())
		return;

	CPlayer *pBossPlayer = pBoss->GetPlayer();
	if(!pBossPlayer)
		return;

	// Find nearest human player in aggro range
	int NearestCID = -1;
	float NearestDist = BOSS_AGGRO_RANGE;

	for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
	{
		CPlayer *pTarget = GS()->m_apPlayers[i];
		if(!pTarget || pTarget->IsDummy() || pTarget->m_IsWorldBoss)
			continue;

		CCharacter *pTargetChr = pTarget->GetCharacter();
		if(!pTargetChr || !pTargetChr->IsAlive())
			continue;

		float Dist = distance(pBoss->GetPos(), pTargetChr->GetPos());
		if(Dist < NearestDist)
		{
			NearestDist = Dist;
			NearestCID = i;
		}
	}

	// Build bot input
	CNetObj_PlayerInput Input;
	mem_zero(&Input, sizeof(Input));

	if(NearestCID >= 0)
	{
		CPlayer *pTargetPlayer = GS()->m_apPlayers[NearestCID];
		CCharacter *pTarget = pTargetPlayer ? pTargetPlayer->GetCharacter() : nullptr;
		if(!pTarget || !pTarget->IsAlive())
			return;

		vec2 TargetPos = pTarget->GetPos();
		vec2 Dir = TargetPos - pBoss->GetPos();
		float Dist = length(Dir);

		// Move toward target if too far
		if(Dist > BOSS_ATTACK_RANGE)
		{
			// Move horizontally toward target
			if(Dir.x > BOSS_SPEED)
				Input.m_Direction = 1;
			else if(Dir.x < -BOSS_SPEED)
				Input.m_Direction = -1;

			// Jump if target is above or obstacle ahead
			if(Dir.y < -64.f)
				Input.m_Jump = 1;
		}
		else
		{
			// In attack range — face target and attack
			Input.m_Direction = Dir.x > 0 ? 1 : -1;

			// Attack cooldown
			if(Server()->Tick() >= m_LastBossAttackTick + BOSS_ATTACK_COOLDOWN)
			{
				// Fire
				Input.m_Fire = 1;
				// Aim toward target
				Input.m_TargetX = (int)Dir.x;
				Input.m_TargetY = (int)Dir.y;

				// Choose weapon based on distance
				if(Dist > 120.f)
				{
					// Ranged: use grenade
					pBossPlayer->m_ZombAiLastInp.m_NextWeapon = WEAPON_GRENADE;
				}
				else if(Dist > 60.f)
				{
					// Medium range: use gun
					pBossPlayer->m_ZombAiLastInp.m_NextWeapon = WEAPON_GUN;
				}
				else
				{
					// Close range: use hammer (melee)
					pBossPlayer->m_ZombAiLastInp.m_NextWeapon = WEAPON_HAMMER;
				}

				m_LastBossAttackTick = Server()->Tick();
			}
		}
	}
	else
	{
		// No target — idle in place, face random direction
		Input.m_Direction = ((Server()->Tick() / 100) % 3) - 1; // -1, 0, or 1
	}

	// Apply input to the boss character using public accessor
	pBoss->SetInput(Input);
	// Also update core input for physics simulation
	pBoss->GetCore()->m_Input = Input;
}

void CWorldBossManager::BroadcastBossStatus()
{
	if(!m_IsAlive || m_BossClientID < 0)
		return;

	CPlayer *pBoss = GS()->m_apPlayers[m_BossClientID];
	if(!pBoss) return;

	CCharacter *pChr = pBoss->GetCharacter();
	if(!pChr) return;

	// Update boss HP from character state using public accessors
	m_BossHP = pChr->GetHealth();
	m_BossMaxHP = pChr->GetMaxHealth();

	// Update clan tag to show HP
	char aClan[64];
	int pct = m_BossMaxHP > 0 ? (m_BossHP * 100 / m_BossMaxHP) : 0;
	str_format(aClan, sizeof(aClan), "🐉 Boss: %d/%d [%d%%]", m_BossHP, m_BossMaxHP, pct);
	GS()->Server()->SetClientClan(m_BossClientID, aClan);

	// Broadcast to world every 3 seconds (30 ticks)
	if((Server()->Tick() % 30) == 1)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf),
			"🐉 世界 Boss HP: %d/%d (%d%%)",
			m_BossHP, m_BossMaxHP, pct);
		GS()->BroadcastWorldMsg(m_WorldID, CGameContext::BROADCAST_PRIORITY_HIGH, 30, aBuf);
	}
}

void CWorldBossManager::DistributeRewards(CPlayer *pKiller)
{
	if(!pKiller)
		return;

	CMMOManager *pMMO = Core() ? Core()->GetMMOManager() : nullptr;
	if(!pMMO)
	{
		dbg_msg("world_boss", "Cannot distribute rewards: no MMO manager");
		return;
	}

	// Sort damage leaders
	std::sort(m_aDamage.begin(), m_aDamage.end(),
		[](const SPlayerDamage &A, const SPlayerDamage &B) {
			return A.Damage > B.Damage;
		});

	int TotalDamage = 0;
	for(const auto &Entry : m_aDamage)
		TotalDamage += Entry.Damage;

	// ─── Reward distribution ───────────────────────────────────────
	// 1st place: damage leader gets special rewards
	if(m_aDamage.size() >= 1)
	{
		CPlayer *pTop = GS()->m_apPlayers[m_aDamage[0].ClientID];
		if(pTop && pTop->GetAccountId() > 0)
		{
			int Pct = TotalDamage > 0 ? (m_aDamage[0].Damage * 100 / TotalDamage) : 0;
			pMMO->AddGold(pTop, 5000);
			pMMO->AddExperience(pTop, 2000);
			pTop->m_MMOReputation += 50;

			// Give a rare chest as top reward
			pMMO->GiveItem(pTop, 1, 1, 0); // ItemID 1 = 宝箱

			char aMsg[128];
			str_format(aMsg, sizeof(aMsg),
				"🏆 你对世界 Boss 造成了 %d 伤害 (%d%%)，获得 5000金币 + 2000经验 + 50声望 + 宝箱！",
				m_aDamage[0].Damage, Pct);
			GS()->SendChatTo(m_aDamage[0].ClientID, aMsg);
		}
	}

	// 2nd place
	if(m_aDamage.size() >= 2)
	{
		CPlayer *pSecond = GS()->m_apPlayers[m_aDamage[1].ClientID];
		if(pSecond && pSecond->GetAccountId() > 0)
		{
			int Pct = TotalDamage > 0 ? (m_aDamage[1].Damage * 100 / TotalDamage) : 0;
			pMMO->AddGold(pSecond, 3000);
			pMMO->AddExperience(pSecond, 1000);
			pSecond->m_MMOReputation += 30;

			pMMO->GiveItem(pSecond, 1, 1, 0);

			char aMsg[128];
			str_format(aMsg, sizeof(aMsg),
				"🥈 你对世界 Boss 造成了 %d 伤害 (%d%%)，获得 3000金币 + 1000经验 + 30声望 + 宝箱！",
				m_aDamage[1].Damage, Pct);
			GS()->SendChatTo(m_aDamage[1].ClientID, aMsg);
		}
	}

	// 3rd place
	if(m_aDamage.size() >= 3)
	{
		CPlayer *pThird = GS()->m_apPlayers[m_aDamage[2].ClientID];
		if(pThird && pThird->GetAccountId() > 0)
		{
			int Pct = TotalDamage > 0 ? (m_aDamage[2].Damage * 100 / TotalDamage) : 0;
			pMMO->AddGold(pThird, 2000);
			pMMO->AddExperience(pThird, 500);
			pThird->m_MMOReputation += 20;

			pMMO->GiveItem(pThird, 1, 1, 0);

			char aMsg[128];
			str_format(aMsg, sizeof(aMsg),
				"🥉 你对世界 Boss 造成了 %d 伤害 (%d%%)，获得 2000金币 + 500经验 + 20声望 + 宝箱！",
				m_aDamage[2].Damage, Pct);
			GS()->SendChatTo(m_aDamage[2].ClientID, aMsg);
		}
	}

	// All participants get consolation reward
	for(size_t i = 3; i < m_aDamage.size(); i++)
	{
		CPlayer *pPlayer = GS()->m_apPlayers[m_aDamage[i].ClientID];
		if(!pPlayer || pPlayer->GetAccountId() <= 0)
			continue;

		int Pct = TotalDamage > 0 ? (m_aDamage[i].Damage * 100 / TotalDamage) : 0;
		pMMO->AddGold(pPlayer, 500);
		pMMO->AddExperience(pPlayer, 200);
		pPlayer->m_MMOReputation += 5;

		char aMsg[128];
		str_format(aMsg, sizeof(aMsg),
			"💫 你参与了世界 Boss 战，造成 %d 伤害 (%d%%)，获得 500金币 + 200经验 + 5声望！",
			m_aDamage[i].Damage, Pct);
		GS()->SendChatTo(m_aDamage[i].ClientID, aMsg);
	}

	// Kill reward (finishing blow)
	if(pKiller && pKiller->GetAccountId() > 0)
	{
		pKiller->m_MMOReputation += 100;
		pMMO->GiveItem(pKiller, 1, 2, 0); // Extra chest for kill

		GS()->SendChatTo(pKiller->GetCID(),
			"⚔️ 你击杀世界 Boss！额外获得 100声望 + 2个宝箱！");
	}

	// World announcement with damage leaderboard
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf),
		"=== 🐉 世界 Boss 讨伐结果 ===");
	GS()->BroadcastWorldMsg(m_WorldID, CGameContext::BROADCAST_PRIORITY_CRITICAL, 300, aBuf);

	for(size_t i = 0; i < minimum((size_t)5, m_aDamage.size()); i++)
	{
		int Pct = TotalDamage > 0 ? (m_aDamage[i].Damage * 100 / TotalDamage) : 0;
		str_format(aBuf, sizeof(aBuf),
			"#%d. %s — %d 伤害 (%d%%)",
			(int)(i + 1),
			Server()->ClientName(m_aDamage[i].ClientID),
			m_aDamage[i].Damage,
			Pct);
		GS()->BroadcastWorldMsg(m_WorldID, CGameContext::BROADCAST_PRIORITY_CRITICAL, 300, aBuf);
	}

	dbg_msg("world_boss", "Rewards distributed: %d participants, total damage %d",
		(int)m_aDamage.size(), TotalDamage);
}

void CWorldBossManager::ResetDamageTracking()
{
	m_aDamage.clear();
}

void CWorldBossManager::RegisterBossCommands()
{
	CCommandManager *pManager = GS()->CommandManager();
	if(!pManager) return;

	pManager->AddCommand("boss", "查看世界 Boss 状态", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetWorldBossManager()) return;

		CWorldBossManager *pWB = pG->Core()->GetWorldBossManager();
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP) return;

		if(pWB->IsBossAlive())
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf),
				"🐉 世界 Boss 状态：存活中 | HP: %d/%d (%d%%)",
				pWB->GetBossHP(), pWB->GetBossMaxHP(),
				pWB->GetBossMaxHP() > 0 ? (pWB->GetBossHP() * 100 / pWB->GetBossMaxHP()) : 0);
			pG->SendChatTo(pCtx->m_ClientID, aBuf);
		}
		else
		{
			int Remaining = pWB->GetNextSpawnInTicks() / pG->Server()->TickSpeed();
			char aBuf[128];
			if(Remaining > 0)
				str_format(aBuf, sizeof(aBuf), "🐉 世界 Boss 已消失，下次刷新剩余约 %d 秒。", Remaining);
			else
				str_format(aBuf, sizeof(aBuf), "🐉 世界 Boss 正在准备降临...");
			pG->SendChatTo(pCtx->m_ClientID, aBuf);
		}
	}, GS());

	pManager->AddCommand("boss_tp", "传送到世界 Boss 位置", "",
		[](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetWorldBossManager()) return;

		CWorldBossManager *pWB = pG->Core()->GetWorldBossManager();
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || !pP->GetCharacter()) return;

		if(!pWB->IsBossAlive())
		{
			pG->SendChatTo(pCtx->m_ClientID, "❌ 世界 Boss 当前不存在。");
			return;
		}

		// Get boss position from character
		if(pWB->m_BossClientID >= 0)
		{
			CPlayer *pBoss = pG->m_apPlayers[pWB->m_BossClientID];
			if(pBoss && pBoss->GetCharacter())
			{
				vec2 BossPos = pBoss->GetCharacter()->GetPos();
				pP->GetCharacter()->SetCharacterPos(BossPos + vec2(64, 0)); // Spawn slightly to the right
				pG->SendChatTo(pCtx->m_ClientID, "🚀 已传送到世界 Boss 位置！");
			}
		}
	}, GS());
}

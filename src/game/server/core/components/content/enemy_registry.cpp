#include <engine/shared/jsonparser.h>

#include <game/server/account.h>
#include <game/server/core/components/content/enemy_registry.h>
#include <game/server/core/components/content/status_manager.h>
#include <game/server/core/components/meta/mini_events_manager.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/components/vote/vote_menu_types.h>
#include <game/server/entity_manager.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontroller.h>
#include <game/server/worldmodes/defence.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/turret_ammo.h>


CEnemyRegistry::CEnemyRegistry()
{
	m_NumEnemies = 0;
}

bool CEnemyRegistry::OnVoteMenuPage(int ClientID, int Page)
{
	if(Page != PAGE_COMPENDIUM && Page != PAGE_COMPENDIUM_ZOMBIE
		&& Page != PAGE_COMPENDIUM_ITEM && Page != PAGE_COMPENDIUM_TURRET_AMMO)
		return false;
	if(!GS() || !Core() || !Core()->VoteMenuManager())
		return false;
	CPlayer *pP = GS()->m_apPlayers[ClientID];
	if(!pP)
		return false;
	CVoteMenuManager *pVote = Core()->VoteMenuManager();
	SPlayerVote *pSVote = pVote->GetPlayerVote(ClientID);

	if(Page == PAGE_COMPENDIUM)
	{
		pVote->SetVoteLastPage(PAGE_MENU);
		pVote->AddVote_PageHeader(CVoteMenuManager::VL(GS(), pP, "compendium.title", "图鉴"));
		pVote->AddVote_Separator();
		pVote->AddVote_PageSubtitle(CVoteMenuManager::VL(GS(), pP, "compendium.subtitle", "查阅游戏资料"));
		pVote->AddVote_Space();

		pVote->AddVote_Section(CVoteMenuManager::VL(GS(), pP, "compendium.section.zombie", "僵尸种类"));
		pVote->AddVote_Goto(PAGE_COMPENDIUM_ZOMBIE, CVoteMenuManager::VL(GS(), pP, "compendium.goto.zombie", "  ☞ 查看全部僵尸"));
		{
			const int Num = NumEnemies();
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), CVoteMenuManager::VL(GS(), pP, "compendium.count.zombie", "  （共 %d 种）"), Num);
			pVote->AddVote_TextLine(aLine);
		}
		pVote->AddVote_Space();

		pVote->AddVote_Section(CVoteMenuManager::VL(GS(), pP, "compendium.section.item", "物品效果"));
		pVote->AddVote_Goto(PAGE_COMPENDIUM_ITEM, CVoteMenuManager::VL(GS(), pP, "compendium.goto.item", "  ☞ 浏览全部物品"));
		pVote->AddVote_Space();

		pVote->AddVote_Section(CVoteMenuManager::VL(GS(), pP, "compendium.section.turret_ammo", "炮塔子弹配比"));
		pVote->AddVote_Goto(PAGE_COMPENDIUM_TURRET_AMMO, CVoteMenuManager::VL(GS(), pP, "compendium.goto.turret_ammo", "  ☞ 材料效果说明"));
		pVote->AddVote_PageFooter();
	}
	else if(Page == PAGE_COMPENDIUM_ZOMBIE)
	{
		pVote->SetVoteLastPage(PAGE_COMPENDIUM);
		pVote->AddVote_PageHeader(CVoteMenuManager::VL(GS(), pP, "compendium.zombie.title", "僵尸种类"));
		pVote->AddVote_Separator();
		const int Num = NumEnemies();
		if(Num <= 0)
			pVote->AddVote_EmptyHint(CVoteMenuManager::VL(GS(), pP, "compendium.zombie.empty", "暂无数据"));
		else
		{
			for(int i = 0; i < Num; i++)
			{
				const SEnemyDef *pDef = GetEnemy(i);
				if(!pDef)
					continue;

				char aZombKey[48];
				str_format(aZombKey, sizeof(aZombKey), "enemy.%s", pDef->m_aId);
				const char *pZombName = CVoteMenuManager::VL(GS(), pP, aZombKey, pDef->m_aId);

				// Line 1: name + wave
				{
					char aLine[VOTE_DESC_LENGTH];
					if(pDef->m_WaveMin > 0)
					{
						char aWave[24];
						str_format(aWave, sizeof(aWave), CVoteMenuManager::VL(GS(), pP, "compendium.zombie.wave", " 第%d波起"), pDef->m_WaveMin);
						str_format(aLine, sizeof(aLine), "▹ %s%s", pZombName, aWave);
					}
					else
						str_format(aLine, sizeof(aLine), "▹ %s", pZombName);
					pVote->AddVote_TextLine(aLine);
				}

				// Line 2: HP mul
				if(pDef->m_HpMul > 0.01f && pDef->m_HpMul != 1.f)
				{
					char aHp[VOTE_DESC_LENGTH];
					str_format(aHp, sizeof(aHp), CVoteMenuManager::VL(GS(), pP, "compendium.zombie.hp_mul", "  血量×%.1f"), pDef->m_HpMul);
					pVote->AddVote_TextLine(aHp);
				}

				// Line 3: tags (one per line if many)
				if(pDef->m_NumTags > 0)
				{
					for(int t = 0; t < pDef->m_NumTags; t++)
					{
						char aTagKey[48];
						str_format(aTagKey, sizeof(aTagKey), "compendium.tag.%s", pDef->m_aaTagNames[t]);
						const char *pTagDesc = CVoteMenuManager::VL(GS(), pP, aTagKey, pDef->m_aaTagNames[t]);
						char aTag[VOTE_DESC_LENGTH];
						str_format(aTag, sizeof(aTag), CVoteMenuManager::VL(GS(), pP, "compendium.zombie.tag_line", "  ·%s"), pTagDesc);
						pVote->AddVote_TextLine(aTag);
					}
				}

				// Line 4+: loot, one per line
				for(int l = 0; l < pDef->m_NumLoot; l++)
				{
					const SEnemyLootEntry &Entry = pDef->m_aLoot[l];
					const char *pMatName = GS()->LocItemName(ClientID, Entry.m_ItemId);
					char aLoot[VOTE_DESC_LENGTH];
					if(Entry.m_MinNum == Entry.m_MaxNum)
						str_format(aLoot, sizeof(aLoot), "  掉落 %s ×%d", pMatName, Entry.m_MinNum);
					else
						str_format(aLoot, sizeof(aLoot), "  掉落 %s ×%d~%d", pMatName, Entry.m_MinNum, Entry.m_MaxNum);
					pVote->AddVote_TextLine(aLoot);
				}

				if(pDef->m_NumLoot + pDef->m_NumTags > 0)
					pVote->AddVote_Space();
			}
		}
		pVote->AddVote_PageFooter();
	}
	else if(Page == PAGE_COMPENDIUM_ITEM)
	{
		pVote->SetVoteLastPage(PAGE_COMPENDIUM);
		pVote->AddVote_PageHeader(CVoteMenuManager::VL(GS(), pP, "compendium.item.title", "物品效果"));
		pVote->AddVote_Separator();

		CItemHelper *pH = GS()->ItemHelper();
		if(!pH)
			pVote->AddVote_EmptyHint(CVoteMenuManager::VL(GS(), pP, "compendium.item.unavailable", "物品系统尚未加载"));
		else
		{
			for(int t = 0; t < NUM_ITYPE; t++)
			{
				if(pSVote->m_Select[SPlayerVote::ITEMLIST] != t)
				{
					char aCmd[64];
					str_format(aCmd, sizeof(aCmd), "ccv_menuselitem %d %d", SPlayerVote::ITEMLIST, t);
					char aLine[VOTE_DESC_LENGTH];
					str_format(aLine, sizeof(aLine), CVoteMenuManager::VL(GS(), pP, "compendium.item.category", "▹ %s"), CVoteMenuManager::ItemTypeLoc(GS(), pP, t));
					pVote->AddVote(aLine, aCmd, ClientID);
				}
				else
				{
					char aLine[VOTE_DESC_LENGTH];
					str_format(aLine, sizeof(aLine), CVoteMenuManager::VL(GS(), pP, "compendium.item.category.open", "▾ %s"), CVoteMenuManager::ItemTypeLoc(GS(), pP, t));
					pVote->AddVote_TextLine(aLine);

					const int Cat = pSVote->m_Select[SPlayerVote::ITEMLIST];
					for(int i = 0; i < NUM_ITEM; i++)
					{
						if(pH->GetType(i) != Cat)
							continue;
						if(!pH->HasItemDefinition(i))
							continue;

						const char *pName = GS()->LocItemName(ClientID, i);
						const char *pDesc = GS()->LocItemDesc(ClientID, i);
						const int PrefixLen = 4; // "  · "
						const int SepLen = pDesc && pDesc[0] ? 3 : 0; // " — "
						const int NameLen = str_length(pName);
						const int DescAvail = VOTE_DESC_LENGTH - 1 - PrefixLen - NameLen - SepLen;

						char aLine2[VOTE_DESC_LENGTH];
						if(pDesc && pDesc[0] && DescAvail > 2)
						{
							char aShortDesc[VOTE_DESC_LENGTH];
							str_copy(aShortDesc, pDesc, minimum((int)sizeof(aShortDesc), DescAvail + 1));
							if(str_length(aShortDesc) < str_length(pDesc))
							{
								// Truncated — replace last 3 chars with "..."
								const int Dst = str_length(aShortDesc);
								if(Dst >= 4)
								{
									aShortDesc[Dst - 1] = '.';
									aShortDesc[Dst - 2] = '.';
									aShortDesc[Dst - 3] = '.';
								}
							}
							str_format(aLine2, sizeof(aLine2), "  · %s — %s", pName, aShortDesc);
						}
						else
							str_format(aLine2, sizeof(aLine2), "  · %s", pName);
						pVote->AddVote_TextLine(aLine2);
					}
				}
			}
		}
		pVote->AddVote_PageFooter();
	}
	else if(Page == PAGE_COMPENDIUM_TURRET_AMMO)
	{
		pVote->SetVoteLastPage(PAGE_COMPENDIUM);
		pVote->AddVote_PageHeader(CVoteMenuManager::VL(GS(), pP, "compendium.turret_ammo.title", "子弹材料效果"));
		pVote->AddVote_Separator();
		CVoteMenuManager::AddVoteWrappedText(pVote, "炮塔使用材料作子弹，不同材料提供不同效果，配比可在炮塔页面调整。");

		static const struct { int m_Mat; const char *m_pName; const char *m_pEffect; } s_aAmmo[] = {
			{TURRET_AMMO_LOG,      "木材", "基础材料，无特殊加成"},
			{TURRET_AMMO_COAL,     "煤炭", "≥20% 子弹爆炸；主导→定向爆裂弹"},
			{TURRET_AMMO_COPPER,   "铜",   "≥20% 连锁闪电；主导→电弧弹"},
			{TURRET_AMMO_IRON,     "铁",   "≥15% 伤害提升（每25% +1倍）"},
			{TURRET_AMMO_GOLD,     "金",   "≥10% 伤害提升（每20% +1倍）"},
			{TURRET_AMMO_DIAMOND,  "钻石", "≥10% 伤害提升（每10% +1倍）"},
			{TURRET_AMMO_ENEGRY,   "能量", "≥25% 聚变效果；配煤爆裂更大"},
			{TURRET_AMMO_ZOMBIEHEART, "僵尸之心", "无战斗效果，珍贵通货"},
		};

		for(int i = 0; i < 8; i++)
		{
			const char *pMat = CVoteMenuManager::TurretMatLoc(GS(), pP, s_aAmmo[i].m_Mat);

			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), "▹ %s", pMat);
			pVote->AddVote_TextLine(aLine);

			CVoteMenuManager::AddVoteWrappedText(pVote, s_aAmmo[i].m_pEffect);
			pVote->AddVote_Space();
		}

		CVoteMenuManager::AddVoteWrappedText(pVote, "伤害加成可叠加。卡牌特效（爆炸/聚变/电子链）也可与材料效果叠加。");
		pVote->AddVote_PageFooter();
	}
	return true;
}

void CEnemyRegistry::LoadEnemies()
{
	m_NumEnemies = 0;
	if(!Storage())
		return;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_content/td/enemies.json", Storage());
	if(!pRoot)
	{
		dbg_msg("content", "enemies.json: %s", Parser.Error());
		return;
	}

	const json_value &Arr = (*pRoot)["enemies"];
	if(Arr.type != json_array)
		return;

	for(unsigned i = 0; i < Arr.u.array.length && m_NumEnemies < MAX_CONTENT_ENEMIES; i++)
	{
		const json_value &E = Arr[(int)i];
		if(E.type != json_object || E["id"].type != json_string)
			continue;
		SEnemyDef &Def = m_aEnemies[m_NumEnemies++];
		mem_zero(&Def, sizeof(Def));
		str_copy(Def.m_aId, E["id"].u.string.ptr, sizeof(Def.m_aId));
		if(E["legacy_zomb_id"].type == json_integer)
			Def.m_LegacyZombId = (int)E["legacy_zomb_id"].u.integer;
		if(E["wave_min"].type == json_integer)
			Def.m_WaveMin = (int)E["wave_min"].u.integer;
		if(E["spawn_weight"].type == json_integer)
			Def.m_SpawnWeight = (int)E["spawn_weight"].u.integer;
		if(E["hp_mul"].type == json_double)
			Def.m_HpMul = (float)E["hp_mul"].u.dbl;
		else if(E["hp_mul"].type == json_integer)
			Def.m_HpMul = (float)E["hp_mul"].u.integer;
		if(Def.m_HpMul <= 0.f)
			Def.m_HpMul = 1.f;

		const json_value &Tags = E["tags"];
		if(Tags.type == json_array)
		{
			for(unsigned t = 0; t < Tags.u.array.length && Def.m_NumTags < 8; t++)
			{
				const json_value &TV = Tags[(int)t];
				if(TV.type != json_string)
					continue;
				str_copy(Def.m_aaTagNames[Def.m_NumTags], TV.u.string.ptr, sizeof(Def.m_aaTagNames[Def.m_NumTags]));
				Def.m_NumTags++;
			}
		}

		if(E["bonus_hearts"].type == json_integer)
			Def.m_BonusHearts = (int)E["bonus_hearts"].u.integer;
		else
			Def.m_BonusHearts = 1;

		const json_value &Loot = E["loot"];
		if(Loot.type == json_array)
		{
			for(unsigned l = 0; l < Loot.u.array.length && Def.m_NumLoot < MAX_ENEMY_LOOT; l++)
			{
				const json_value &L = Loot[(int)l];
				if(L.type != json_object)
					continue;
				SEnemyLootEntry &Entry = Def.m_aLoot[Def.m_NumLoot++];
				if(L["item"].type == json_integer)
					Entry.m_ItemId = (int)L["item"].u.integer;
				if(L["min"].type == json_integer)
					Entry.m_MinNum = (int)L["min"].u.integer;
				if(L["max"].type == json_integer)
					Entry.m_MaxNum = (int)L["max"].u.integer;
				if(L["weight"].type == json_integer)
					Entry.m_Weight = (int)L["weight"].u.integer;
				if(Entry.m_MaxNum < Entry.m_MinNum)
					Entry.m_MaxNum = Entry.m_MinNum;
			}
		}
	}
}

void CEnemyRegistry::OnInitWorld(const char *pWhereLocalWorld)
{
	(void)pWhereLocalWorld;
	LoadEnemies();
}

const SEnemyDef *CEnemyRegistry::FindByZombId(int ZombId) const
{
	for(int i = 0; i < m_NumEnemies; i++)
	{
		if(m_aEnemies[i].m_LegacyZombId == ZombId)
			return &m_aEnemies[i];
	}
	return nullptr;
}

const SEnemyDef *CEnemyRegistry::FindById(const char *pId) const
{
	if(!pId)
		return nullptr;
	for(int i = 0; i < m_NumEnemies; i++)
	{
		if(str_comp(m_aEnemies[i].m_aId, pId) == 0)
			return &m_aEnemies[i];
	}
	return nullptr;
}

bool CEnemyRegistry::EnemyHasTag(const SEnemyDef &Def, const char *pTag) const
{
	if(!pTag)
		return false;
	for(int i = 0; i < Def.m_NumTags; i++)
	{
		if(str_comp(Def.m_aaTagNames[i], pTag) == 0)
			return true;
	}
	return false;
}

bool CEnemyRegistry::HasTag(int ZombId, const char *pTag) const
{
	const SEnemyDef *pDef = FindByZombId(ZombId);
	return pDef && EnemyHasTag(*pDef, pTag);
}

float CEnemyRegistry::GetHpMul(int ZombId) const
{
	const SEnemyDef *pDef = FindByZombId(ZombId);
	return pDef ? pDef->m_HpMul : 1.f;
}

void CEnemyRegistry::ApplyWaveBoost(CGameControllerDefence *pCtrl, int Wave) const
{
	if(!pCtrl || Wave <= 0)
		return;

	for(int i = 0; i < m_NumEnemies; i++)
	{
		const SEnemyDef &Def = m_aEnemies[i];
		if(Def.m_WaveMin <= 0 || Wave < Def.m_WaveMin || Def.m_LegacyZombId <= 0)
			continue;
		const int Add = maximum(1, Def.m_SpawnWeight > 0 ? Def.m_SpawnWeight / 2 : 1);
		pCtrl->TdAddZombiePool(Def.m_LegacyZombId, Add);
	}
}

void CEnemyRegistry::OnZombieDeath(CGameControllerDefence *pCtrl, CPlayer *pVictim) const
{
	if(!pCtrl || !pVictim || !GS())
		return;

	const int Z = pVictim->GetZomb();
	if(HasTag(Z, "split_on_death"))
		pCtrl->TdAddZombiePool(ZOMB_ZABY, 2);

	if(HasTag(Z, "explode_on_death"))
	{
		CCharacter *pChr = pVictim->GetCharacter();
		if(pChr)
		{
			const vec2 Pos = pChr->GetPos();
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				CPlayer *pP = GS()->m_apPlayers[i];
				if(!pP || pP->IsDummy() || !pP->GetCharacter() || !pP->GetCharacter()->IsAlive())
					continue;
				if(distance(Pos, pP->GetCharacter()->GetPos()) <= 140.f)
					pP->GetCharacter()->TakeDamage(vec2(0, 0), Pos, 3, -1, WEAPON_GRENADE);
			}
		}
	}
}

void CEnemyRegistry::RollLoot(CPlayer *pKiller, int ZombId) const
{
	if(!pKiller || !GS())
		return;

	const SEnemyDef *pDef = FindByZombId(ZombId);
	int Reward = ITEM_LOG;
	int Num = 1;

	if(pDef && pDef->m_NumLoot > 0)
	{
		int TotalWeight = 0;
		for(int i = 0; i < pDef->m_NumLoot; i++)
			TotalWeight += maximum(1, pDef->m_aLoot[i].m_Weight);
		int Roll = rand() % TotalWeight;
		for(int i = 0; i < pDef->m_NumLoot; i++)
		{
			const SEnemyLootEntry &L = pDef->m_aLoot[i];
			const int W = maximum(1, L.m_Weight);
			if(Roll < W)
			{
				Reward = L.m_ItemId;
				if(L.m_MaxNum > L.m_MinNum)
					Num = L.m_MinNum + rand() % (L.m_MaxNum - L.m_MinNum + 1);
				else
					Num = maximum(1, L.m_MinNum);
				break;
			}
			Roll -= W;
		}
	}
	else
	{
		const int Rando = rand() % 100 + 1;
		if(Rando <= 50)
			Reward = ITEM_LOG;
		else if(Rando <= 75)
			Reward = ITEM_COPPER;
		else
			Reward = ITEM_GOLD;
	}

	if(Core() && Core()->MiniEventsManager())
	{
		const int Bonus = Core()->MiniEventsManager()->GetLootBonusPercent();
		if(Bonus > 0)
			Num = maximum(1, Num + Num * Bonus / 100);
	}

	vec2 Pos = vec2(0.f, 0.f);
	if(CCharacter *pChr = pKiller->GetCharacter())
		Pos = pChr->GetPos();
	else
		Pos = pKiller->m_ViewPos;

	if(Core() && Core()->EntityManager())
	{
		Core()->EntityManager()->DropItem(Pos, pKiller->GetCID(), Reward, Num);
		const int Hearts = pDef ? maximum(1, pDef->m_BonusHearts) : 1;
		Core()->EntityManager()->DropItem(Pos, pKiller->GetCID(), ITEM_ZOMBIEHEART, Hearts);
	}
	else
	{
		pKiller->m_AccData.m_aItems[Reward].m_Num += Num;
		const int Hearts = pDef ? maximum(1, pDef->m_BonusHearts) : 1;
		pKiller->m_AccData.m_aItems[ITEM_ZOMBIEHEART].m_Num += Hearts;
		if(GS()->Accounts() && GS()->Accounts()->IsEnabled() && pKiller->GetAccountId() >= 0)
			GS()->Accounts()->RequestSaveItems(pKiller->GetCID());
	}

	pKiller->m_Score++;
	GS()->SendChatLocF(pKiller->GetCID(), "game.loot_drop", "掉落：%s ×%d（僵尸心+%d）",
		GS()->LocItemName(pKiller->GetCID(), Reward), Num, pDef ? maximum(1, pDef->m_BonusHearts) : 1);
}

void CEnemyRegistry::TickZombie(CPlayer *pP) const
{
	if(!pP || !GS() || !Core() || !Core()->StatusManager())
		return;
	CCharacter *pChr = pP ? pP->GetCharacter() : nullptr;
	if(!pP || !pChr || !pChr->IsAlive())
		return;

	const int Z = pP->GetZomb();
	const int Tick = GS()->Server()->Tick();
	CStatusManager *pStatus = Core()->StatusManager();

	if(HasTag(Z, "shield") && pStatus && (Tick / 150) % 2 == 0 && (Tick % 150) == 0)
		pStatus->ApplyStatus(pChr, "shield", 1, GS()->Server()->TickSpeed() * 3, 0.f, 8);

	if(HasTag(Z, "heal_aura"))
	{
		const vec2 Pos = pChr->GetPos();
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *pZ = GS()->m_apPlayers[i];
			if(!pZ || !pZ->IsDummy() || pZ->GetZomb() == ZOMB_NONE || pZ == pP)
				continue;
			CCharacter *pAlly = pZ->GetCharacter();
			if(!pAlly || !pAlly->IsAlive())
				continue;
			if(distance(Pos, pAlly->GetPos()) <= 180.f && (Tick % 75) == 0)
				pAlly->IncreaseHealth(1);
		}
	}

	if(HasTag(Z, "venom_aura") && pStatus && (Tick % 50) == 0)
	{
		const vec2 Pos = pChr->GetPos();
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *pHuman = GS()->m_apPlayers[i];
			if(!pHuman || pHuman->IsDummy() || !pHuman->GetCharacter() || !pHuman->GetCharacter()->IsAlive())
				continue;
			if(distance(Pos, pHuman->GetCharacter()->GetPos()) <= 160.f)
				pStatus->ApplyStatus(pHuman->GetCharacter(), "poison", 1, GS()->Server()->TickSpeed() * 4);
		}
	}
}



void CEnemyRegistry::OnTick()
{
	if(!GS() || !GS()->m_pController)
		return;

	auto *pCtrl = dynamic_cast<CGameControllerDefence *>(GS()->m_pController);
	if(!pCtrl)
		return;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GS()->m_apPlayers[i];
		if(!pP || !pP->IsDummy() || pP->GetZomb() == ZOMB_NONE)
			continue;
		if(pCtrl->TdGetZombieAI(i))
			TickZombie(GS()->m_apPlayers[i]);
	}
}

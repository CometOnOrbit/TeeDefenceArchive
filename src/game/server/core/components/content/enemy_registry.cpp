#include <engine/shared/jsonparser.h>

#include <game/server/account.h>
#include <game/server/core/components/content/enemy_registry.h>
#include <game/server/core/components/content/status_manager.h>
#include <game/server/core/components/meta/mini_events_manager.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entity_manager.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontroller.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/zombie_bot.h>

CEnemyRegistry::CEnemyRegistry()
{
	m_NumEnemies = 0;
}

void CEnemyRegistry::LoadEnemies()
{
	m_NumEnemies = 0;
	if(!Storage())
		return;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_content/enemies.json", Storage());
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

void CEnemyRegistry::ApplyWaveBoost(CGameController *pCtrl, int Wave) const
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

void CEnemyRegistry::OnZombieDeath(CGameController *pCtrl, CPlayer *pVictim) const
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
	GS()->SendChatLocF(pKiller->GetCID(), "game.loot_drop", u8"掉落：%s ×%d（僵尸心+%d）",
		GS()->LocItemName(pKiller->GetCID(), Reward), Num, pDef ? maximum(1, pDef->m_BonusHearts) : 1);
}

void CEnemyRegistry::TickZombie(CZombieBot *pBot) const
{
	if(!pBot || !GS() || !Core() || !Core()->StatusManager())
		return;

	CPlayer *pP = pBot->Player();
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

	CGameController *pCtrl = static_cast<CGameController *>(GS()->m_pController);
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GS()->m_apPlayers[i];
		if(!pP || !pP->IsDummy() || pP->GetZomb() == ZOMB_NONE)
			continue;
		if(CZombieBot *pBot = pCtrl->TdGetZombieBot(i))
			TickZombie(pBot);
	}
}

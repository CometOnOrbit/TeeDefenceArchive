#include <engine/shared/config.h>
#include <engine/shared/jsonparser.h>

#include <game/server/core/components/content/effect_registry.h>
#include <game/server/core/components/content/status_manager.h>
#include <game/server/core/components/content/trait_manager.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/item_system.h>

CEffectRegistry::CEffectRegistry()
{
	m_NumEffects = 0;
}

const SEffectDef *CEffectRegistry::GetEffect(int Idx) const
{
	if(Idx < 0 || Idx >= m_NumEffects)
		return nullptr;
	return &m_aEffects[Idx];
}

const SEffectDef *CEffectRegistry::FindEffect(const char *pId) const
{
	const int Idx = FindEffectIndex(pId);
	return Idx >= 0 ? &m_aEffects[Idx] : nullptr;
}

int CEffectRegistry::FindEffectIndex(const char *pId) const
{
	if(!pId || !pId[0])
		return -1;
	for(int i = 0; i < m_NumEffects; i++)
	{
		if(str_comp(m_aEffects[i].m_aId, pId) == 0)
			return i;
	}
	return -1;
}

void CEffectRegistry::ParseEffectParams(const json_value &Params, SEffectParams &Out)
{
	mem_zero(&Out, sizeof(Out));
	Out.m_SlowMul = 0.86f;
	if(Params.type != json_object)
		return;
#define PARSE_INT(Field, Key) \
	if(Params[Key].type == json_integer) \
		Out.Field = (int)Params[Key].u.integer
	PARSE_INT(m_ReloadPerStack, "reload_per_stack");
	PARSE_INT(m_MineCdPerStack, "mine_cd_per_stack");
	PARSE_INT(m_TurretCdPerStack, "turret_cd_per_stack");
	if(Params["cd_per_stack"].type == json_integer)
		Out.m_TurretCdPerStack = (int)Params["cd_per_stack"].u.integer;
	PARSE_INT(m_DamagePerStack, "damage_per_stack");
	PARSE_INT(m_MineDmgPctPerStack, "mine_dmg_pct_per_stack");
	PARSE_INT(m_RegenPerStack, "regen_per_stack");
	PARSE_INT(m_ForcePerStack, "force_per_stack");
	PARSE_INT(m_RadiusBase, "radius_base");
	PARSE_INT(m_RadiusPerStack, "radius_per_stack");
	PARSE_INT(m_RadiusPerFusionStack, "radius_per_stack");
	PARSE_INT(m_DurationPerStack, "duration_per_stack");
	PARSE_INT(m_DurationTicks, "duration_ticks");
	PARSE_INT(m_TickInterval, "tick_interval");
	PARSE_INT(m_DotPerStack, "dot_per_stack");
	PARSE_INT(m_MoveDotPerStack, "move_dot_per_stack");
	PARSE_INT(m_AbsorbPerStack, "absorb_per_stack");
	PARSE_INT(m_HealPerStack, "heal_per_stack");
	PARSE_INT(m_ArmorPerStack, "armor_per_stack");
	PARSE_INT(m_ChainsPerStack, "chains_per_stack");
	PARSE_INT(m_CritBonus, "crit_bonus");
	PARSE_INT(m_SpreadDegPerStack, "spread_deg_per_stack");
	PARSE_INT(m_AmmoBonusPerStack, "ammo_bonus_per_stack");
	PARSE_INT(m_RangeBonusPerStack, "range_bonus_per_stack");
	PARSE_INT(m_BurnStacks, "burn_stacks");
	if(Params["slow_mul"].type == json_double)
		Out.m_SlowMul = (float)Params["slow_mul"].u.dbl;
	else if(Params["slow_mul"].type == json_integer)
		Out.m_SlowMul = (float)Params["slow_mul"].u.integer;
#undef PARSE_INT
}

void CEffectRegistry::LoadEffects()
{
	m_NumEffects = 0;
	if(!Storage())
		return;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_content/effects.json", Storage());
	if(!pRoot)
	{
		dbg_msg("content", "effects.json: %s", Parser.Error());
		return;
	}

	const json_value &Arr = (*pRoot)["effects"];
	if(Arr.type != json_array)
		return;

	for(unsigned i = 0; i < Arr.u.array.length && m_NumEffects < MAX_CONTENT_EFFECTS; i++)
	{
		const json_value &E = Arr[(int)i];
		if(E.type != json_object || E["id"].type != json_string)
			continue;

		SEffectDef &Def = m_aEffects[m_NumEffects++];
		mem_zero(&Def, sizeof(Def));
		str_copy(Def.m_aId, E["id"].u.string.ptr, sizeof(Def.m_aId));
		if(E["legacy_item"].type == json_integer)
			Def.m_LegacyItem = (int)E["legacy_item"].u.integer;
		Def.m_IsStatus = E["status"].type == json_boolean || (E["status"].type == json_integer && E["status"].u.integer != 0);
		ParseEffectParams(E["params"], Def.m_Params);

		const json_value &Triggers = E["triggers"];
		if(Triggers.type == json_array)
		{
			for(unsigned t = 0; t < Triggers.u.array.length; t++)
			{
				const json_value &TV = Triggers[(int)t];
				if(TV.type != json_string)
					continue;
				const EEffectTrigger Tr = EffectTriggerFromString(TV.u.string.ptr);
				if(Tr >= 0 && Tr < NUM_EFFECT_TRIGGERS)
					Def.m_aTriggers[Tr] = true;
			}
		}
	}
	dbg_msg("content", "loaded %d effects", m_NumEffects);
}

void CEffectRegistry::OnInitWorld(const char *pWhereLocalWorld)
{
	(void)pWhereLocalWorld;
	LoadEffects();
}

void CEffectRegistry::OnConsoleInit()
{
	if(Console())
	{
		Console()->Register("content_reload_effects", "", CFGFLAG_SERVER, [](IConsole::IResult *pResult, void *pUser) {
			(void)pResult;
			CEffectRegistry *pSelf = static_cast<CEffectRegistry *>(pUser);
			pSelf->LoadEffects();
		}, this, "Reload server_content/effects.json");
	}
}

bool CEffectRegistry::EffectHasTrigger(const SEffectDef &Def, EEffectTrigger Trigger) const
{
	return Trigger >= 0 && Trigger < NUM_EFFECT_TRIGGERS && Def.m_aTriggers[Trigger];
}

int CEffectRegistry::LegacyItemToStacks(CItemHelper *pItems, const char *pExtraJson, int LegacyItemId) const
{
	return pItems ? pItems->GetCard(pExtraJson, LegacyItemId) + pItems->GetPart(pExtraJson, LegacyItemId) : 0;
}

int CEffectRegistry::QueryStacks(CItemHelper *pItems, const char *pExtraJson, const char *pEffectId) const
{
	if(!pItems || !pEffectId)
		return 0;

	int Stacks = 0;
	for(int i = 0; i < NUM_ITEM; i++)
		Stacks += pItems->GetEffectStacksFromExtra(pExtraJson, i, pEffectId);

	const SEffectDef *pDef = FindEffect(pEffectId);
	if(pDef && pDef->m_LegacyItem >= 0 && pItems->GetNumItemEffects(pDef->m_LegacyItem) == 0)
		Stacks += LegacyItemToStacks(pItems, pExtraJson, pDef->m_LegacyItem);
	return Stacks;
}

void CEffectRegistry::ApplyTraitModifiers(CEffectContext &Ctx, float DamageMul, float ReloadMul) const
{
	if(DamageMul > 0.001f && DamageMul != 1.f)
		Ctx.m_OutDamage = maximum(1, (int)(Ctx.m_OutDamage * DamageMul + 0.5f));
	if(ReloadMul > 0.001f && ReloadMul != 1.f)
		Ctx.m_OutReloadDelta = (int)(Ctx.m_OutReloadDelta * ReloadMul + 0.5f);
}

void CEffectRegistry::ApplyOne(const SEffectDef &Def, int Stacks, EEffectTrigger Trigger, CEffectContext &Ctx) const
{
	if(Stacks <= 0)
		return;

	const SEffectParams &P = Def.m_Params;
	CItemHelper *pItems = GS() ? GS()->ItemHelper() : nullptr;

	if(str_comp(Def.m_aId, "reload_haste") == 0)
	{
		if(Trigger == TRIGGER_RELOAD)
			Ctx.m_OutReloadDelta += P.m_ReloadPerStack * Stacks;
		else if(Trigger == TRIGGER_MINE)
			Ctx.m_OutMineCd += P.m_MineCdPerStack * Stacks;
		else if(Trigger == TRIGGER_TURRET_FIRE)
			Ctx.m_OutTurretCd += P.m_TurretCdPerStack * Stacks;
	}
	else if(str_comp(Def.m_aId, "haste") == 0)
	{
		if(Trigger == TRIGGER_RELOAD)
			Ctx.m_OutReloadDelta += P.m_ReloadPerStack * Stacks;
		else if(Trigger == TRIGGER_MINE)
			Ctx.m_OutMineCd += P.m_MineCdPerStack * Stacks;
	}
	else if(str_comp(Def.m_aId, "ammo_regen") == 0 && Trigger == TRIGGER_TICK)
	{
		if(pItems && Ctx.m_pPlayer && Ctx.m_pExtraJson)
		{
			const int MaxPlace = pItems->GetMaxPlace(ITEM_CARD_QUICKLY_LOADING_ID);
			int AmmoRegen = 1 + MaxPlace * 500 - Stacks * (P.m_RegenPerStack > 0 ? P.m_RegenPerStack : 500);
			Ctx.m_AmmoRegenTime = clamp(AmmoRegen, 0, 1 + MaxPlace * 500);
		}
	}
	else if(str_comp(Def.m_aId, "damage_bonus") == 0)
	{
		if(Trigger == TRIGGER_MINE && P.m_MineDmgPctPerStack > 0)
		{
			const int Bonus = (Ctx.m_InDamage * P.m_MineDmgPctPerStack * Stacks) / 100;
			Ctx.m_OutDamage += Bonus;
		}
		else
		{
			const int Bonus = P.m_DamagePerStack * Stacks;
			if(Trigger == TRIGGER_WEAPON_FIRE || Trigger == TRIGGER_TURRET_FIRE || Trigger == TRIGGER_MINE)
				Ctx.m_OutDamage += Bonus;
		}
	}
	else if(str_comp(Def.m_aId, "explosive") == 0)
	{
		Ctx.m_Flags |= EFFECT_FLAG_EXPLOSIVE;
		Ctx.m_ExplosionStacks = maximum(Ctx.m_ExplosionStacks, Stacks);
	}
	else if(str_comp(Def.m_aId, "electron_chain") == 0)
	{
		Ctx.m_Flags |= EFFECT_FLAG_ELECTRON;
		Ctx.m_ElectronStacks = maximum(Ctx.m_ElectronStacks, Stacks);
	}
	else if(str_comp(Def.m_aId, "electron_slow") == 0 && Trigger == TRIGGER_TAKE_DAMAGE && Ctx.m_pVictim)
	{
		const int ForceStacks = pItems && Ctx.m_pExtraJson ? pItems->GetCard(Ctx.m_pExtraJson, ITEM_CARD_FORCE_ID) : 0;
		const int SlowStacks = Stacks + minimum(Stacks, ForceStacks);
		if(Core() && Core()->StatusManager() && GS() && GS()->Config()->m_SvContentFramework)
		{
			const int Dur = P.m_DurationPerStack > 0 ? P.m_DurationPerStack * SlowStacks : (GS()->Server()->TickSpeed() * SlowStacks) / 8;
			Core()->StatusManager()->ApplyStatus(Ctx.m_pVictim, "electron_slow", SlowStacks, Dur, P.m_SlowMul);
		}
		else
			Ctx.m_pVictim->ApplyElectronSlow(SlowStacks);
	}
	else if(str_comp(Def.m_aId, "fusion_bloom") == 0)
	{
		Ctx.m_Flags |= EFFECT_FLAG_FUSION;
		Ctx.m_FusionStacks = maximum(Ctx.m_FusionStacks, Stacks);
	}
	else if(str_comp(Def.m_aId, "knockback") == 0)
	{
		Ctx.m_OutForceMul += P.m_ForcePerStack * (float)Stacks;
	}
	else if(str_comp(Def.m_aId, "turret_manual") == 0 && Trigger == TRIGGER_TURRET_FIRE)
	{
		Ctx.m_Flags |= EFFECT_FLAG_MANUAL_TURRET;
		Ctx.m_ManualTurret = true;
	}
	else if(str_comp(Def.m_aId, "turret_cooldown") == 0 && Trigger == TRIGGER_TURRET_FIRE)
	{
		Ctx.m_OutTurretCd += P.m_TurretCdPerStack * Stacks;
	}
	else if(str_comp(Def.m_aId, "barrel_spread") == 0 && Trigger == TRIGGER_TURRET_FIRE)
	{
		Ctx.m_TurretSpreadDeg += P.m_SpreadDegPerStack * Stacks;
	}
	else if(str_comp(Def.m_aId, "stabilizer") == 0 && Trigger == TRIGGER_TURRET_FIRE)
	{
		Ctx.m_TurretRangeBonus += P.m_RangeBonusPerStack * Stacks;
	}
	else if(str_comp(Def.m_aId, "magazine_bonus") == 0 && Trigger == TRIGGER_WEAPON_FIRE)
	{
		(void)P;
		(void)Stacks;
	}
	else if(str_comp(Def.m_aId, "lifesteal") == 0 && Trigger == TRIGGER_DEAL_DAMAGE && Ctx.m_pAttacker)
	{
		Ctx.m_pAttacker->IncreaseHealth(P.m_HealPerStack * Stacks);
	}
	else if(str_comp(Def.m_aId, "armor_shred") == 0 && Trigger == TRIGGER_DEAL_DAMAGE && Ctx.m_pVictim)
	{
		const int Shred = P.m_ArmorPerStack * Stacks;
		if(Shred > 0)
			Ctx.m_pVictim->ReduceArmor(Shred);
	}
	else if(str_comp(Def.m_aId, "chain_lightning") == 0 && Trigger == TRIGGER_PROJECTILE_HIT)
	{
		Ctx.m_ChainLightningStacks = maximum(Ctx.m_ChainLightningStacks, Stacks * maximum(1, P.m_ChainsPerStack));
	}
	else if(str_comp(Def.m_aId, "mining_luck") == 0 && Trigger == TRIGGER_MINE)
	{
		Ctx.m_MiningCritBonus += P.m_CritBonus * Stacks;
	}
	else if(str_comp(Def.m_aId, "burn_on_hit") == 0 && Trigger == TRIGGER_DEAL_DAMAGE && Ctx.m_pVictim && Core() && Core()->StatusManager())
	{
		const int BurnStacks = P.m_BurnStacks > 0 ? P.m_BurnStacks * Stacks : Stacks;
		Core()->StatusManager()->ApplyStatus(Ctx.m_pVictim, "burn", BurnStacks, P.m_DurationTicks > 0 ? P.m_DurationTicks : 150);
	}
	else if(str_comp(Def.m_aId, "burn") == 0 && Def.m_IsStatus && Trigger == TRIGGER_TAKE_DAMAGE && Ctx.m_pVictim && Core() && Core()->StatusManager())
	{
		Core()->StatusManager()->ApplyStatus(Ctx.m_pVictim, "burn", Stacks, P.m_DurationTicks > 0 ? P.m_DurationTicks : 150);
	}
	else if(str_comp(Def.m_aId, "bleed") == 0 && Def.m_IsStatus && Trigger == TRIGGER_TAKE_DAMAGE && Ctx.m_pVictim && Core() && Core()->StatusManager())
	{
		Core()->StatusManager()->ApplyStatus(Ctx.m_pVictim, "bleed", Stacks, P.m_DurationTicks > 0 ? P.m_DurationTicks : 200);
	}
	else if(str_comp(Def.m_aId, "shield") == 0 && Def.m_IsStatus && Trigger == TRIGGER_TAKE_DAMAGE && Ctx.m_pVictim && Core() && Core()->StatusManager())
	{
		Core()->StatusManager()->ApplyStatus(Ctx.m_pVictim, "shield", Stacks, P.m_DurationTicks > 0 ? P.m_DurationTicks : 300, 0.f, P.m_AbsorbPerStack * Stacks);
	}
}

void CEffectRegistry::Apply(EEffectTrigger Trigger, CEffectContext &Ctx) const
{
	if(!GS() || !GS()->Config()->m_SvContentFramework)
		return;

	CItemHelper *pItems = GS() ? GS()->ItemHelper() : nullptr;
	if(!pItems)
		return;

	for(int i = 0; i < m_NumEffects; i++)
	{
		const SEffectDef &Def = m_aEffects[i];
		if(!EffectHasTrigger(Def, Trigger))
			continue;
		const int Stacks = QueryStacks(pItems, Ctx.m_pExtraJson, Def.m_aId);
		ApplyOne(Def, Stacks, Trigger, Ctx);
	}

	if(Core() && Core()->TraitManager() && Ctx.m_pPlayer)
	{
		float DmgMul = 1.f;
		float ReloadMul = 1.f;
		Core()->TraitManager()->GetCombatModifiers(Ctx.m_pPlayer, DmgMul, ReloadMul);
		ApplyTraitModifiers(Ctx, DmgMul, ReloadMul);

		if(Trigger == TRIGGER_DEAL_DAMAGE && Ctx.m_pAttacker && Ctx.m_InDamage > 0)
		{
			const int Ls = Core()->TraitManager()->GetLifestealBonus(Ctx.m_pPlayer);
			if(Ls > 0)
				Ctx.m_pAttacker->IncreaseHealth(Ls);
		}
	}
}

#ifndef GAME_SERVER_CORE_COMPONENTS_CONTENT_EFFECT_REGISTRY_H
#define GAME_SERVER_CORE_COMPONENTS_CONTENT_EFFECT_REGISTRY_H

#include <engine/external/json-parser/json.h>

#include <game/server/core/tworld_component.h>

#include "content_types.h"

class CItemHelper;

class CEffectRegistry : public TWorldComponent
{
	SEffectDef m_aEffects[MAX_CONTENT_EFFECTS];
	int m_NumEffects;

public:
	CEffectRegistry();

	void OnInitWorld(const char *pWhereLocalWorld) override;
	void OnConsoleInit() override;

	int NumEffects() const { return m_NumEffects; }
	const SEffectDef *GetEffect(int Idx) const;
	const SEffectDef *FindEffect(const char *pId) const;
	int FindEffectIndex(const char *pId) const;
	int QueryStacks(CItemHelper *pItems, const char *pExtraJson, const char *pEffectId) const;
	void Apply(EEffectTrigger Trigger, CEffectContext &Ctx) const;
	void ApplyTraitModifiers(CEffectContext &Ctx, float DamageMul, float ReloadMul) const;

private:
	void LoadEffects();
	void ParseEffectParams(const json_value &Params, SEffectParams &Out);
	bool EffectHasTrigger(const SEffectDef &Def, EEffectTrigger Trigger) const;
	void ApplyOne(const SEffectDef &Def, int Stacks, EEffectTrigger Trigger, CEffectContext &Ctx) const;
};

#endif

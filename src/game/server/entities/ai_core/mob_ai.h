#ifndef GAME_SERVER_ENTITIES_AI_CORE_MOB_AI_H
#define GAME_SERVER_ENTITIES_AI_CORE_MOB_AI_H

#include "base_ai.h"

class CGameContext;
class CPlayer;
struct SMMOMobDef;

class CMobAI final : public CBaseAI
{
	int m_LastAttackTick{};
	int m_LastDamageTick{};
	float m_ActiveRadius{800.f};

	// Behavior system (MRPG-style)
	int m_BehaviorPoisonedNextTick{};
	int m_BehaviorSkillNextTick{};
	bool m_BehaviorNeutral{};
	const SMMOMobDef *m_pMobInfo{};

	// Ambient chat
	int m_LastAmbientChatTick{};

	// Zone patrol
	char m_ZoneName[64] = {};
	vec2 m_ZoneBounds[2] = {}; // { {x1,y1}, {x2,y2} }

public:
	CMobAI(CCharacterBotAI *pCharacter, float ActiveRadius = 800.f);

	void Process() override;
	bool CanDamage(CPlayer *pFrom) override;

	void OnSpawn() override;
	void OnTakeDamage(int Dmg, int From, int Weapon) override;
	void OnDie(int Killer, int Weapon) override;
	void OnTargetRules(float Radius) override;
	void OnRewardPlayer(CPlayer *pForPlayer) const override;

	// Behavior interface
	void HandleBehaviors(bool *pbAsleep);
	void HandleSkillBehaviors();
	void HandleAmbientChat();
	void ShowHealth();

	void SetActiveRadius(float R) { m_ActiveRadius = R; }
	void SetMobInfo(const SMMOMobDef *pInfo) { m_pMobInfo = pInfo; }
	void SetZone(const char *pZoneName, vec2 BoundsMin, vec2 BoundsMax);
	bool IsOutsideZone() const;

private:
	void UpdateTarget();
};

#endif

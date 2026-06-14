#ifndef GAME_SERVER_CORE_COMPONENTS_CONTENT_ENEMY_REGISTRY_H
#define GAME_SERVER_CORE_COMPONENTS_CONTENT_ENEMY_REGISTRY_H

#include <game/server/core/tworld_component.h>

#include "content_types.h"

class CCharacter;
class CGameController;
class CPlayer;
class CZombieBot;

class CEnemyRegistry : public TWorldComponent
{
	SEnemyDef m_aEnemies[MAX_CONTENT_ENEMIES];
	int m_NumEnemies;

public:
	CEnemyRegistry();

	int NumEnemies() const { return m_NumEnemies; }
	const SEnemyDef *GetEnemy(int Idx) const { return (Idx >= 0 && Idx < m_NumEnemies) ? &m_aEnemies[Idx] : nullptr; }

	void OnInitWorld(const char *pWhereLocalWorld) override;
	void OnTick() override;

	const SEnemyDef *FindByZombId(int ZombId) const;
	const SEnemyDef *FindById(const char *pId) const;
	bool HasTag(int ZombId, const char *pTag) const;
	float GetHpMul(int ZombId) const;
	void ApplyWaveBoost(CGameController *pCtrl, int Wave) const;
	void OnZombieDeath(CGameController *pCtrl, CPlayer *pVictim) const;
	void RollLoot(CPlayer *pKiller, int ZombId) const;
	void TickZombie(CZombieBot *pBot) const;

private:
	void LoadEnemies();
	bool EnemyHasTag(const SEnemyDef &Def, const char *pTag) const;
};

#endif

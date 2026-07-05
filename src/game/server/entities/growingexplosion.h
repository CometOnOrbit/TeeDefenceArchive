/* (c) Magnus Auvinen / TeeDefenceArchive */
#ifndef GAME_SERVER_ENTITIES_GROWINGEXPLOSION_H
#define GAME_SERVER_ENTITIES_GROWINGEXPLOSION_H

#include <game/server/entity.h>

enum
{
	GROWINGEXPLOSIONEFFECT_BOOM = 0,
	GROWINGEXPLOSIONEFFECT_ELECTRIC,
	GROWINGEXPLOSIONEFFECT_FREEZE,
	GROWINGEXPLOSIONEFFECT_POISON,
	GROWINGEXPLOSIONEFFECT_HEAL,
};

enum EGrowingExplosionTargetMode
{
	GE_TARGET_TD = 0,
	GE_TARGET_MMO_HOSTILE,
	GE_TARGET_MMO_ALLY,
};

class CGrowingExplosion : public CEntity
{
public:
	CGrowingExplosion(CGameWorld *pGameWorld, vec2 Pos, vec2 Dir, int Owner, int Radius, int ExplosionEffect, bool Fusion,
		int TargetMode = GE_TARGET_TD, int CustomDamage = -1);
	~CGrowingExplosion() override;

	void Reset() override;
	void Tick() override;
	void TickPaused() override;

	int GetOwner() const { return m_Owner; }
	int GetActualDamage();

private:
	void ProcessShockwaveHit(class CCharacter *pCharacter);
	bool IsHostileTarget(class CCharacter *pChr);
	bool IsHealTarget(class CCharacter *pChr);

	int m_MaxGrowing;
	int m_GrowingMap_Length;
	int m_GrowingMap_Size;
	int m_VisualizedTiles;
	int m_Owner;
	vec2 m_SeedPos;
	int m_SeedX;
	int m_SeedY;
	int m_StartTick;
	int *m_pGrowingMap;
	vec2 *m_pGrowingMapVec;
	int m_ExplosionEffect;
	int m_TargetMode;
	int m_CustomDamage;
	bool m_Hit[MAX_CLIENTS];
	bool m_Fusion;
};

#endif

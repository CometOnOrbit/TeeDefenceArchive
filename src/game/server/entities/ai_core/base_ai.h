#ifndef GAME_SERVER_ENTITIES_AI_CORE_BASE_AI_H
#define GAME_SERVER_ENTITIES_AI_CORE_BASE_AI_H

#include "target_ai.h"
#include <base/vmath.h>
#include <base/system.h>
#include <engine/shared/protocol.h>
#include <game/server/core/tools/path_finder_result.h>
#include <functional>

class CPlayer;
class CGameContext;
class CCharacterBotAI;
class CPathFinder;
class IServer;

class CBaseAI
{
public:
	CBaseAI(CCharacterBotAI *pCharacter);
	virtual ~CBaseAI() {}

	virtual void Process() = 0;
	virtual bool CanDamage(CPlayer *pFrom) { return true; }

	virtual void OnSpawn() {}
	virtual void OnTakeDamage(int Dmg, int From, int Weapon) {}
	virtual void OnDie(int Killer, int Weapon) {}
	virtual void OnRewardPlayer(CPlayer *pForPlayer) const {}
	virtual void OnTargetRules(float Radius) {}
	virtual bool IsConversational() { return false; }

	int GetClientID() const { return m_ClientID; }
	CTargetAI *GetTarget() { return &m_Target; }

protected:
	int m_ClientID{};
	vec2 m_SpawnPoint{};
	CCharacterBotAI *m_pCharacter{};
	CTargetAI m_Target{};
	PathRequestHandle m_PathHandle{};

	CGameContext *GS() const;
	IServer *Server() const;
	CPathFinder *PathFinder() const;
	CPlayer *GetPlayer(int ClientID, bool CheckCharacter = true, bool CheckAlive = false);

	int SearchNearestPlayer(float Radius, bool SkipBots = true);
	int SearchPlayerByCondition(float Radius, bool (*Condition)(CPlayer *));
	CPlayer *SearchPlayerCondition(float Radius, const std::function<bool(CPlayer*)> &Condition);

	void MoveToward(vec2 TargetPos);
	void SmartMove(vec2 TargetPos);
	void SetWeaponIfAvailable(int Weapon);

	// Request path and move along it
	void FollowPath(const vec2 &TargetPos, float AttackDist = 100.f, int *pOutFire = nullptr);
};

#endif

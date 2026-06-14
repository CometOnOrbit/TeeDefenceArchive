/* (c) TeeDefenceArchive - 2026 */
#ifndef GAME_SERVER_ENTITIES_SPIDER_BOSS_H
#define GAME_SERVER_ENTITIES_SPIDER_BOSS_H

#include <game/server/entity.h>

class CCharacter;
class CGameController;

enum
{
	NUM_SPIDER_LEGS = 4,
	NUM_SPIDER_SEGS = 2,
	SPIDER_SEG_KNEE = 0,
	SPIDER_SEG_FOOT = 1,

	SPIDER_LEG_RF = 0,
	SPIDER_LEG_LF = 1,
	SPIDER_LEG_LH = 2,
	SPIDER_LEG_RH = 3,

	SPIDER_LEG_STANCE = 0,
	SPIDER_LEG_SWING = 1,

	SPIDER_TARGET_TOWER = 0,
	SPIDER_TARGET_PLAYER = 1,
};

class CSpiderBoss;

class CSpiderLegPart : public CHitableEntity
{
	CSpiderBoss *m_pBoss;
	int m_Leg;
	int m_Seg;

public:
	CSpiderLegPart(CGameWorld *pGameWorld, CSpiderBoss *pBoss, int Leg, int Seg);
	bool TakeHit(vec2 Force, vec2 Source, int Dmg, CEntity *pFrom, int Weapon) override;
	void SetSegmentPos(vec2 Pos);
};

class CSpiderBoss : public CEntity
{
	friend class CSpiderLegPart;

	CGameController *m_pCtrl;
	int m_OwnerCid;
	CCharacter *m_pCore;

	int m_aLegHealth[NUM_SPIDER_LEGS];
	int m_aLegState[NUM_SPIDER_LEGS];
	vec2 m_aKnee[NUM_SPIDER_LEGS];
	vec2 m_aFoot[NUM_SPIDER_LEGS];
	vec2 m_aPlantedFoot[NUM_SPIDER_LEGS];
	vec2 m_aSwingFrom[NUM_SPIDER_LEGS];
	vec2 m_aSwingTo[NUM_SPIDER_LEGS];
	float m_aSwingT[NUM_SPIDER_LEGS];
	vec2 m_aFootKnock[NUM_SPIDER_LEGS];
	CSpiderLegPart *m_apSeg[NUM_SPIDER_LEGS][NUM_SPIDER_SEGS];

	int m_LastGrenadeTick;
	int m_LastStompTick;
	int m_LastTowerHitTick;
	int m_LastBodySlamTick;
	int m_LastLeapTick;
	int m_LastWebTick;
	int m_JumpPhase;
	int m_JumpStartTick;
	float m_JumpPeakY;
	vec2 m_LastSafePos;
	int m_Wave;
	int m_SpawnCoreHealth;
	vec2 m_BodyVel;
	vec2 m_MoveDir;
	int m_StuckTicks;
	vec2 m_LastProgressPos;
	enum
	{
		SPIDER_JUMP_IDLE = 0,
		SPIDER_JUMP_RISE,
		SPIDER_JUMP_FALL,
	};

	struct
	{
		int m_Type;
		int m_PlayerCid;
		vec2 m_Pos;
		bool m_NeedPathUpdate;
	} m_ChaseTarget;

	float TickDt();
	float LegOutward(int Leg) const;
	vec2 HipLocal(int Leg) const;
	vec2 HipWorld(int Leg) const;
	vec2 LegHome(int Leg);
	vec2 SolveKnee(int Leg, vec2 Hip, vec2 Foot) const;
	bool RayDown(vec2 From, vec2 *pFoot);
	bool RayUp(vec2 From, vec2 *pHit);
	bool RayToWall(int Side, vec2 From, vec2 *pHit);
	bool IsVerticalWall(int Side, vec2 From, vec2 *pHit);
	bool ProbeFootSpot(vec2 From, vec2 *pFoot);
	float GroundBodyY(float X);
	float GroundBodyYAt(float X, float RayFromY);
	float FindStandY(vec2 WishDir);
	bool HasGroundSupport();
	vec2 SpiderBodySize() const;
	void ApplySpiderMove(vec2 Vel);
	void ApplySpiderGroundMove(vec2 WishVel, vec2 WishDir);
	void ClampSpiderToMap();
	void RecoverIfInvalid();
	void UpdateLeapStomp();
	void TryLeapStomp();
	void TrySpitWeb();
	int LegGroup(int Leg) const;
	bool GroupIsStepping(int Group) const;
	bool CanLegStep(int Leg) const;
	void TryStartLegStep(int Leg);
	void UpdateLeg(int Leg);
	void PlaceLeg(int Leg);
	void PlaceAllLegs();
	void InitPose(vec2 Spawn);
	void UpdateBody();
	vec2 TowerGoal();
	void UpdateChaseTarget();
	vec2 GetChaseGoal();
	bool HasLineOfSight(vec2 Target);
	void InvalidatePath();
	vec2 ComputeMoveWish(vec2 Goal, vec2 Waypoint);
	vec2 PathWaypoint(vec2 Goal);
	void UpdateLegHitboxes();
	void ApplyFootKnockback();
	void OnLegPlanted(int Leg);
	void TryMeleeAttacks();
	void TryGrenade();
	void TryAttackTower();
	bool IsHumanTarget(CCharacter *pChr) const;
	int SpiderAttackDamage(int Base) const;
	CCharacter *NearestHuman(float MaxDist);
	CCharacter *NearestVisibleHuman(float MaxDist);

public:
	CSpiderBoss(CGameWorld *pGameWorld, CCharacter *pCore, CGameController *pCtrl, int Wave);
	~CSpiderBoss() override;

	void Tick() override;
	void TickDefered() override;
	void Snap(int SnappingClient) override;

	void DamageLeg(int Leg, int Dmg, vec2 Force, vec2 Source, int Weapon);
	bool IsCoreAlive() const;
	int GetCoreHealth() const;
	int GetCoreMaxHealth() const;
	int GetLegsAlive() const;
	int GetOwnerCid() const { return m_OwnerCid; }
};

#endif

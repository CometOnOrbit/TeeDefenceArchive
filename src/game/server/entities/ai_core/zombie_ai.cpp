#include "zombie_ai.h"
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/worldmodes/defence.h>
#include <game/server/player.h>
#include <game/server/botengine.h>
#include <game/server/entities/character.h>
#include <game/server/entities/turret.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <game/mapitems.h>
#include <engine/server.h>
#include <engine/shared/config.h>
#include <cmath>

// CZombieAI only runs in defence context, so m_pCtrl is always CGameControllerDefence*
static CGameControllerDefence *Ctrl(CGameController *p) { return static_cast<CGameControllerDefence *>(p); }

#define GS() m_pGS

static constexpr float HUMAN_AGGRO_RADIUS = 900.0f;
static constexpr float HUMAN_COMBAT_RADIUS = 720.0f;
static constexpr float HAMMER_ATTACK_RANGE = 85.0f;
static constexpr float ZOMB_CROWD_RADIUS = 72.0f;
static constexpr float ZOMB_SEPARATION_RADIUS = 58.0f;
static constexpr float TOWER_SEEK_HOOK_DIST = 180.0f;
static constexpr float TURRET_SEEK_RADIUS = 960.0f;

static CTurret *NearestLivingTurret(CGameWorld *pWorld, vec2 Pos, float MaxDist)
{
	CTurret *pBest = nullptr;
	float BestDist = MaxDist;
	for(auto r = pWorld->DoTypeRange(CGameWorld::ENTTYPE_TURRET); !r.empty(); r.pop_front())
	{
		CTurret *pT = static_cast<CTurret *>(r.front());
		if(!pT || pT->IsBroken()) continue;
		float d = distance(Pos, pT->GetPos());
		if(d < BestDist) { BestDist = d; pBest = pT; }
	}
	return pBest;
}


// ─── Construction ───────────────────────────────────────────────────

CZombieAI::CZombieAI(CPlayer *pPlayer, CGameController *pCtrl, CGameContext *pGS)
	: m_pPlayer(pPlayer), m_pCtrl(pCtrl), m_pGS(pGS)
{
	m_LowSpeedTicks = 0;
	m_McJumpTried = false;
	m_StuckFlipCooldown = 0;
	m_HookCooldown = 0;
	mem_zero(&m_InputData, sizeof(m_InputData));
	m_LastData = m_InputData;
	m_TargetInfo.m_Type = TARGET_MARCH;
	m_TargetInfo.m_PlayerCID = -1;
	m_TargetInfo.m_NeedUpdate = true;
}

// ─── Tile helpers ───────────────────────────────────────────────────

int CZombieAI::GetTile(vec2 Pos) const
{
	CBotEngine *pBE = GS()->BotEngine();
	return pBE ? pBE->GetTile(Pos) : 0;
}

int CZombieAI::ZombieFirstSlot() const
{
	return minimum((int)MAX_HUMAN_CLIENTS, GS()->Config()->m_SvMaxClients);
}

int CZombieAI::CountNearbyZombies(vec2 Pos, float Radius) const
{
	int Count = 0;
	int Z0 = ZombieFirstSlot();
	int CID = m_pPlayer->GetCID();
	for(int i = Z0; i < MAX_CLIENTS; i++)
	{
		if(i == CID) continue;
		CPlayer *pZ = GS()->m_apPlayers[i];
		if(!pZ || !pZ->IsDummy() || pZ->GetZomb() == ZOMB_NONE) continue;
		CCharacter *pC = pZ->GetCharacter();
		if(!pC || !pC->IsAlive()) continue;
		if(distance(Pos, pC->GetPos()) < Radius) Count++;
	}
	return Count;
}

void CZombieAI::ApplyCrowdSteering(vec2 Pos, vec2 *pTargetOff) const
{
	vec2 Sep(0, 0);
	int Z0 = ZombieFirstSlot();
	int CID = m_pPlayer->GetCID();
	for(int i = Z0; i < MAX_CLIENTS; i++)
	{
		if(i == CID) continue;
		CPlayer *pZ = GS()->m_apPlayers[i];
		if(!pZ || !pZ->IsDummy() || pZ->GetZomb() == ZOMB_NONE) continue;
		CCharacter *pC = pZ->GetCharacter();
		if(!pC || !pC->IsAlive()) continue;
		vec2 Delta = Pos - pC->GetPos();
		float d = length(Delta);
		if(d < 1.0f || d > ZOMB_SEPARATION_RADIUS) continue;
		Sep += normalize(Delta) * (ZOMB_SEPARATION_RADIUS - d);
	}
	if(length(Sep) > 1.0f) *pTargetOff += Sep * 0.85f;
}

bool CZombieAI::ZombieBlockedAhead(vec2 Pos, int Dir) const
{
	if(Dir == 0) return false;
	vec2 Ahead = Pos + vec2((float)Dir * 34.0f, -4.0f);
	vec2 Box(CCharacterCore::PHYS_SIZE, CCharacterCore::PHYS_SIZE);
	if(GS()->Collision()->TestBox(Ahead, Box)) return true;

	array<CEntity *> Ents;
	Ents.hint_size(8);
	GS()->m_World.FindEntities(Ahead, 24.0f, Ents, CGameWorld::ENTTYPE_CHARACTER);
	int CID = m_pPlayer->GetCID();
	for(int i = 0; i < Ents.size(); i++)
	{
		CCharacter *pC = static_cast<CCharacter *>(Ents[i]);
		if(!pC || !pC->GetPlayer() || pC->GetPlayer()->GetCID() == CID) continue;
		if(pC->GetPlayer()->IsDummy() && pC->GetPlayer()->GetZomb() != ZOMB_NONE) return true;
	}
	return false;
}

// ─── Personal March Goal ────────────────────────────────────────────

vec2 CZombieAI::GetPersonalMarchGoal() const
{
	if(m_pPlayer->GetZomb() == ZOMB_SPIDER_BOSS) return m_MarchGoal;

	int CID = m_pPlayer->GetCID();
	float Slot = (float)(CID % 24);
	float Ang = Slot * (2.0f * pi / 24.0f);
	float Rad = 96.0f + (float)((CID / 24) % 5) * 36.0f;
	vec2 Personal = m_MarchGoal + vec2(cosf(Ang) * Rad, sinf(Ang) * Rad);

	CBotEngine *pBE = GS()->BotEngine();
	if(pBE)
	{
		CGraph *pG = pBE->GetGraph();
		if(pG && pG->m_NumVertices > 0 && pG->m_pVertices)
		{
			int V = pBE->GetClosestVertex(Personal);
			if(V >= 0 && V < pG->m_NumVertices)
				Personal = pG->m_pVertices[V].m_Pos;
		}
	}
	return Personal;
}

// ─── Target Selection ───────────────────────────────────────────────

void CZombieAI::UpdateTarget()
{
	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pChr) return;

	const vec2 Pos = pChr->GetPos();
	CGameControllerDefence *pCtrl = Ctrl(m_pCtrl);
	const vec2 MarchGoal = GetPersonalMarchGoal();
	const bool HasTower = pCtrl->GetTower() != nullptr;
	const float DistMarch = distance(Pos, MarchGoal);

	int BestCid = -1;
	float BestDist = 1.0e12f;
	vec2 BestPos(0, 0);

	int CID = m_pPlayer->GetCID();
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i == CID) continue;
		CPlayer *pPl = GS()->m_apPlayers[i];
		if(!pPl || pPl->IsDummy() || pPl->GetTeam() == TEAM_SPECTATORS) continue;
		CCharacter *pH = pPl->GetCharacter();
		if(!pH || !pH->IsAlive()) continue;
		float d = distance(Pos, pH->GetPos());
		if(d < BestDist) { BestDist = d; BestPos = pH->GetPos(); BestCid = i; }
	}

	const bool HasHuman = BestCid >= 0;
	const bool HumanLos = HasHuman && !GS()->Collision()->FastIntersectLine(Pos, BestPos, nullptr, nullptr);
	const float Scale = GetAiScale();
	const bool HumanCombat = HumanLos && BestDist < GetCombatRadius();
	const bool PreferHuman = HumanCombat && (Scale >= 0.55f || BestDist < 300.0f) && (!HasTower || BestDist < DistMarch);

	int NewType = TARGET_MARCH;
	vec2 NewPos = MarchGoal;
	int NewCid = -1;

	CTurret *pTurret = NearestLivingTurret(&GS()->m_World, Pos, TURRET_SEEK_RADIUS);
	if(pTurret)
	{
		float d = distance(Pos, pTurret->GetPos());
		if(d < DistMarch) NewPos = pTurret->GetPos();
	}

	if(PreferHuman) { NewType = TARGET_PLAYER; NewPos = BestPos; NewCid = BestCid; }

	SZTarget &T = m_TargetInfo;
	if(T.m_Type != NewType || T.m_PlayerCID != NewCid || distance(T.m_Pos, NewPos) > 48.0f)
	{ T.m_Type = NewType; T.m_PlayerCID = NewCid; T.m_Pos = NewPos; T.m_NeedUpdate = true; }

	if(m_LowSpeedTicks > 30)
	{
		m_pPlayer->m_ZombNavLen = 0;
		T.m_NeedUpdate = true;
		m_LowSpeedTicks = 0;
	}
}

void CZombieAI::UpdateNavigation()
{
	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pChr) return;

	vec2 Pos = pChr->GetPos();
	vec2 Goal = m_TargetInfo.m_Pos;
	if(m_TargetInfo.m_Type == TARGET_MARCH) Goal = GetPersonalMarchGoal();

	m_TargetInfo.m_NeedUpdate = false;
	m_Target = Goal - Pos;

	if(length(m_Target) <= 48.0f) { ApplyCrowdSteering(Pos, &m_Target); return; }
	ApplyCrowdSteering(Pos, &m_Target);
	EnsureMarchDrive(Pos, Goal);
}

void CZombieAI::EnsureMarchDrive(vec2 Pos, vec2 Goal)
{
	vec2 ToGoal = Goal - Pos;
	if(length(ToGoal) < 96.0f) return;
	if(std::abs(m_Target.x) >= 28.0f) return;

	m_Target = ToGoal;
	int Lane = (m_pPlayer->GetCID() % 7) - 3;
	if(std::abs(m_Target.x) < 28.0f)
		m_Target.x = ToGoal.x >= 0.0f ? 72.0f : -72.0f;
	m_Target.y = std::min(m_Target.y, -8.0f) + (float)Lane * 10.0f;
}

// ─── Movement ───────────────────────────────────────────────────────

void CZombieAI::MakeChoice()
{
	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pChr) return;
	CCharacterCore *pCore = pChr->GetCore();
	vec2 Pos = pChr->GetPos();
	const bool Grounded = pChr->IsGrounded();

	CCharacterCore TempChar = *pCore;
	TempChar.m_Input = m_InputData;
	TempChar.m_Input.m_Direction = (m_Target.x > 28.f) ? 1 : (m_Target.x < -28.f) ? -1 : 0;
	{
		CWorldCore TempWorld;
		TempWorld.m_Tuning = *GS()->Tuning();
		TempChar.Init(&TempWorld, GS()->Collision());
		TempChar.Tick(true);
		TempChar.Move();
		TempChar.Quantize();
	}

	const int CurTile = GetTile(Pos);
	const int NextTile = GetTile(TempChar.m_Pos);

	int Flags = 0;
	if(TempChar.m_Input.m_Direction > 0) Flags |= 2;
	if(TempChar.m_Input.m_Direction < 0) Flags |= 1;

	if(m_Target.y < 0)
	{
		if((CurTile & 4) && (NextTile & 2) && (Grounded || pCore->m_Vel.y > 0)) Flags |= 4;
		if((CurTile & 4) && (NextTile & 4))
		{
			if(std::abs(Pos.x - TempChar.m_Pos.x) < 1.0f && TempChar.m_Input.m_Direction)
			{
				if(Grounded) { Flags |= 4; m_McJumpTried = true; }
				else if(m_McJumpTried && !(pCore->m_Jumped & 1) && pCore->m_Vel.y > 0) Flags |= 4;
				else if(m_McJumpTried && (pCore->m_Jumped & 2) && pCore->m_Vel.y > 0) Flags ^= 3;
			}
			else m_McJumpTried = false;
		}
		if(!(pCore->m_Jumped & 1))
		{
			vec2 Vel(pCore->m_Vel.x, std::min(pCore->m_Vel.y, 0.0f));
			if(GS()->Collision()->FastIntersectLine(Pos, Pos + Vel * 10.0f, nullptr, nullptr)
				&& !GS()->Collision()->FastIntersectLine(Pos, Pos + (Vel - vec2(0, GS()->Tuning()->m_AirJumpImpulse)) * 10.0f, nullptr, nullptr))
				Flags |= 4;
			if(std::abs(m_Target.x) < 28.f && pCore->m_Vel.y > -1.f) Flags |= 4;
		}
	}
	m_Flags = Flags;
}

void CZombieAI::HandleStuck(int *pMoveDir)
{
	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pChr) return;
	CCharacterCore *pCore = pChr->GetCore();

	if(pMoveDir && *pMoveDir != 0 && ZombieBlockedAhead(pChr->GetPos(), *pMoveDir))
	{
		m_Flags |= 4;
		if(m_StuckFlipCooldown <= 0 && m_LowSpeedTicks > 12) { m_Flags ^= 3; m_StuckFlipCooldown = 20; }
	}
	if(pChr->IsGrounded() && !(m_Flags & 4) && std::abs(pCore->m_Vel.x) < 0.4f && std::abs(m_Target.x) > 40.0f)
		m_Flags |= 4;
	if(pChr->IsGrounded() && !(m_Flags & 4) && m_Target.y < -48.0f)
		m_Flags |= 4;

	if(std::abs(pCore->m_Vel.x) < 0.5f) m_LowSpeedTicks++;
	else m_LowSpeedTicks = 0;
}

// ─── Hook ────────────────────────────────────────────────────────────

bool CZombieAI::AllowTerrainHook(int CurTile, int MoveDir) const
{
	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pChr) return false;
	if(!pChr->IsGrounded()) return true;
	if(CurTile & 2) return true;
	if(MoveDir != 0 && ZombieBlockedAhead(pChr->GetPos(), MoveDir) && m_Target.y < -24.0f) return true;
	if(m_LowSpeedTicks > 12 && std::abs(m_Target.x) > 36.0f) return true;
	if(m_Target.y < -36.0f) return true;
	return false;
}

bool CZombieAI::WantSpeedHook(int CurTile, float DistMarch) const
{
	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pChr || pChr->IsGrounded() || (CurTile & 2)) return false;
	return DistMarch > TOWER_SEEK_HOOK_DIST;
}

bool CZombieAI::ShouldTryTerrainHook(int CurTile, int MoveDir, float DistMarch) const
{
	if(m_HookCooldown > 0) return false;
	if(!AllowTerrainHook(CurTile, MoveDir) && !WantSpeedHook(CurTile, DistMarch)) return false;
	if(CurTile & 2) return random_int() % 4 == 0;
	if(WantSpeedHook(CurTile, DistMarch)) return random_int() % 5 == 0;
	if(m_Target.y < -32.0f) return random_int() % 6 == 0;
	if(m_LowSpeedTicks > 10) return random_int() % 6 == 0;
	if(CountNearbyZombies(m_pPlayer->GetCharacter()->GetPos(), ZOMB_CROWD_RADIUS) >= 2) return random_int() % 5 == 0;
	if(!m_pPlayer->GetCharacter()->IsGrounded()) return random_int() % 7 == 0;
	return random_int() % 9 == 0;
}

void CZombieAI::HandleHook(bool SeeTarget, int MoveDir, float DistMarch)
{
	CCharacter *pChr = m_pPlayer->GetCharacter();
	CCharacterCore *pMe = pChr ? pChr->GetCore() : nullptr;
	if(!pChr || !pMe) { m_InputData.m_Hook = 0; return; }

	if(m_pPlayer->GetZomb() != ZOMB_ZOOKER) { m_InputData.m_Hook = 0; return; }

	const int CurTile = GetTile(pMe->m_Pos);
	if(pMe->m_HookState == HOOK_FLYING) { m_InputData.m_Hook = 1; return; }

	if(SeeTarget && m_TargetInfo.m_PlayerCID >= 0)
	{
		CPlayer *pT = GS()->m_apPlayers[m_TargetInfo.m_PlayerCID];
		if(pT && pT->GetCharacter())
		{
			const CCharacterCore *pTC = pT->GetCharacter()->GetCore();
			float d = distance(pTC->m_Pos, pMe->m_Pos);
			if(pMe->m_HookState == HOOK_GRABBED && pMe->m_HookedPlayer == m_TargetInfo.m_PlayerCID)
				m_InputData.m_Hook = 1;
			else if(!m_InputData.m_Fire)
			{
				if(d < GS()->Tuning()->m_HookLength * 0.9f) m_InputData.m_Hook = m_LastData.m_Hook ^ 1;
				SeeTarget = d < GS()->Tuning()->m_HookLength * 0.9f;
			}
		}
	}

	if(!SeeTarget)
	{
		if(pMe->m_HookState == HOOK_GRABBED && pMe->m_HookedPlayer == -1)
		{
			vec2 ToHook = pMe->m_HookPos - pMe->m_Pos;
			if(pChr->IsGrounded() && ToHook.y > 8.0f && m_Target.y > -28.0f)
			{ m_InputData.m_Hook = 0; m_HookCooldown = 14; return; }

			vec2 HV = normalize(ToHook) * GS()->Tuning()->m_HookDragAccel;
			if(HV.y > 0) HV.y *= 0.3f;
			int IDir = MoveDir != 0 ? MoveDir : pMe->m_Input.m_Direction;
			if((HV.x < 0 && IDir < 0) || (HV.x > 0 && IDir > 0)) HV.x *= 0.95f;
			else HV.x *= 0.75f;
			HV += vec2(0, 1) * GS()->Tuning()->m_Gravity;
			float Ps = dot(m_Target, HV);
			if(Ps > 0 || (CurTile & 2 && m_Target.y < 0 && pMe->m_Vel.y > 0.f
				&& pMe->m_HookTick < SERVER_TICK_SPEED + SERVER_TICK_SPEED / 2))
				m_InputData.m_Hook = 1;
			if(pMe->m_HookTick > 4 * SERVER_TICK_SPEED || length(ToHook) < 20.0f)
				m_InputData.m_Hook = 0;
		}
		if(pMe->m_HookState == HOOK_FLYING) m_InputData.m_Hook = 1;

		if(GetAiScale() >= 0.4f && !m_InputData.m_Fire && m_LastData.m_Hook == 0
			&& pMe->m_HookState == HOOK_IDLE && ShouldTryTerrainHook(CurTile, MoveDir, DistMarch))
		{
			constexpr int NDIR = 32;
			vec2 HookDir(0, 0);
			float MaxForce = (CurTile & 2) ? -10000.0f : 0.0f;
			int IDir = MoveDir != 0 ? MoveDir : (m_Target.x > 0.0f ? 1 : -1);
			for(int i = 0; i < NDIR; i++)
			{
				float A = 2 * i * pi / NDIR;
				vec2 D = direction(A);
				vec2 HP = pMe->m_Pos + D * GS()->Tuning()->m_HookLength;
				if(GS()->Collision()->FastIntersectLine(pMe->m_Pos, HP, &HP, nullptr) & CCollision::COLFLAG_SOLID)
				{
					vec2 HV2 = D * GS()->Tuning()->m_HookDragAccel;
					if(HV2.y > 0) HV2.y *= 0.3f;
					if((HV2.x < 0 && IDir < 0) || (HV2.x > 0 && IDir > 0)) HV2.x *= 0.95f;
					else HV2.x *= 0.75f;
					HV2 += vec2(0, 1) * GS()->Tuning()->m_Gravity;
					float Ps = dot(m_Target, HV2);
					if(Ps > MaxForce) { MaxForce = Ps; HookDir = HP - pMe->m_Pos; }
				}
			}
			if(length(HookDir) > 32.f) { m_Target = HookDir; m_InputData.m_Hook = 1; }
		}
	}

	if(pChr->IsGrounded() && m_Target.y > -20.0f
		&& (pMe->m_HookState == HOOK_FLYING || pMe->m_HookState == HOOK_GRABBED) && m_InputData.m_Hook)
	{
		vec2 ToHook = pMe->m_HookPos - pMe->m_Pos;
		if(length(ToHook) > 1.0f)
		{
			vec2 PD = normalize(ToHook);
			if(PD.y > 0.35f && std::abs(PD.x) < 0.5f) { m_InputData.m_Hook = 0; m_HookCooldown = 12; }
		}
	}
}

// ─── Difficulty ─────────────────────────────────────────────────────

float CZombieAI::GetAiScale() const
{
	int Wave = std::max(1, Ctrl(m_pCtrl)->GetTdWave());
	if(m_pPlayer->GetZomb() == ZOMB_SPIDER_BOSS)
		return std::min(1.0f, 0.70f + (Wave - 1) * 0.04f) * m_AiMul;
	return std::min(1.0f, 0.30f + (Wave - 1) * 0.078f) * m_AiMul;
}

float CZombieAI::GetCombatRadius() const
{
	return HUMAN_COMBAT_RADIUS * (0.35f + 0.65f * GetAiScale());
}

float CZombieAI::GetAggroRadius() const
{
	return HUMAN_AGGRO_RADIUS * (0.35f + 0.65f * GetAiScale());
}

int CZombieAI::GetActiveZombType() const
{
	int Z = m_pPlayer->GetZomb();
	if(Z != ZOMB_ZEATER) return Z;
	for(int i = 0; i < NUM_ZOMB_SUB; i++)
	{
		int S = m_pPlayer->GetZombSub(i);
		if(S != ZOMB_NONE) return S;
	}
	return Z;
}

float CZombieAI::GetTypeAttackRange(int Z) const
{
	switch(Z)
	{
	case ZOMB_ZUNNER: case ZOMB_FLOMBIE: return 4000.0f;
	case ZOMB_ZENADE: return 800.0f;
	case ZOMB_SPIDER_BOSS: return 900.0f;
	case ZOMB_ZOOMER: return GS()->Tuning()->m_LaserReach;
	case ZOMB_ZOOKER: return GS()->Tuning()->m_HookLength;
	case ZOMB_ZOTTER: return 500.0f;
	default: return HAMMER_ATTACK_RANGE;
	}
}

// ─── Special Abilities ──────────────────────────────────────────────

bool CZombieAI::TryZamerDetonate(float DistTower, float DistHuman, bool InSight)
{
	if(m_pPlayer->GetZomb() != ZOMB_ZAMER) return false;
	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive()) return false;

	float Trigger = 70.0f + 15.0f * GetAiScale();
	if(!(DistTower < Trigger) && !(InSight && DistHuman < Trigger)) return false;

	int Wave = std::max(1, Ctrl(m_pCtrl)->GetTdWave());
	int Dmg = std::max(2, 3 + Wave / 2);
	vec2 Offsets[4] = {{5,5},{-5,5},{-5,-5},{5,-5}};
	m_pPlayer->m_ZamerDetonating = true;
	for(int i = 0; i < 4; i++)
		GS()->m_World.CreateExplosion(pChr->GetPos() + Offsets[i], pChr, WEAPON_GRENADE, Dmg);
	m_pPlayer->m_ZamerDetonating = false;
	if(pChr->IsAlive()) pChr->Die(m_pPlayer->GetCID(), WEAPON_SELF);
	return true;
}

void CZombieAI::ApplySpecialMovement(bool InSight, float DistHuman)
{
	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive() || m_pPlayer->GetZomb() != ZOMB_FLOMBIE || !InSight) return;

	CCharacterCore *pCore = pChr->GetCore();
	if(m_RealTarget.y < pCore->m_Pos.y)
		pCore->m_Vel.y -= 0.25f + GS()->Tuning()->m_Gravity;
	else if(m_RealTarget.y > pCore->m_Pos.y)
		pCore->m_Vel.y += 0.25f;
}

void CZombieAI::TryZeleTeleport(bool InSight, float DistHuman)
{
	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive() || m_pPlayer->GetZomb() != ZOMB_ZELE || !InSight) return;
	if(DistHuman <= 200.0f || DistHuman > 500.0f) return;

	vec2 Dest = m_RealTarget;
	if(!GS()->Collision()->CheckPoint(Dest + vec2(0, 32))) Dest += vec2(0, 32);
	else if(!GS()->Collision()->CheckPoint(Dest - vec2(0, 32))) Dest -= vec2(0, 32);
	else return;

	pChr->GetCore()->m_Pos = Dest;
	pChr->GetCore()->m_Vel.y = -0.1f;
}

void CZombieAI::TryZeaterConsume()
{
	if(m_pPlayer->GetZomb() != ZOMB_ZEATER) return;
	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive()) return;

	vec2 Pos = pChr->GetPos();
	int Z0 = ZombieFirstSlot();
	int CID = m_pPlayer->GetCID();
	for(int i = Z0; i < MAX_CLIENTS; i++)
	{
		if(i == CID) continue;
		CPlayer *pZ = GS()->m_apPlayers[i];
		if(!pZ || !pZ->IsDummy() || pZ->GetZomb() == ZOMB_NONE || pZ->GetZomb() == ZOMB_ZEATER) continue;
		CCharacter *pV = pZ->GetCharacter();
		if(!pV || !pV->IsAlive() || distance(Pos, pV->GetPos()) > 65.0f) continue;

		int Slot = -1;
		for(int s = 0; s < NUM_ZOMB_SUB; s++) { if(m_pPlayer->GetZombSub(s) == ZOMB_NONE) { Slot = s; break; } }
		if(Slot < 0) return;

		int VT = pZ->GetZomb();
		m_pPlayer->SetZombSub(Slot, VT);
		if(VT == ZOMB_ZASTER) pChr->SetHealthDirect(100);
		else pChr->IncreaseHealth(10);
		pV->Die(CID, WEAPON_GAME);
		return;
	}
}

void CZombieAI::UpdateZinvisState(bool HumanCombat, bool HumanAggro)
{
	if(m_pPlayer->GetZomb() == ZOMB_ZINVIS)
		m_pPlayer->SetZombVisible(HumanCombat || HumanAggro);
}

// ─── Weapon Handling ────────────────────────────────────────────────

void CZombieAI::HandleWeapon(bool InSight, bool HumanCombat, bool HumanAggro,
	bool HasStructure, float DistStructure, vec2 StructurePos, float DistHuman)
{
	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pChr) return;

	int Z = GetActiveZombType();
	float Scale = GetAiScale();
	float AR = GetTypeAttackRange(Z);
	vec2 Pos = pChr->GetPos();
	vec2 AimOff = m_Target;

	if(Z == ZOMB_ZINJA && HumanCombat && DistHuman < HAMMER_ATTACK_RANGE && pChr->GetActiveWeapon() == WEAPON_HAMMER)
		pChr->GiveNinja();

	switch(Z)
	{
	case ZOMB_ZUNNER: case ZOMB_FLOMBIE:
		pChr->SetWeapon((HumanCombat || HumanAggro) && InSight ? WEAPON_GUN : WEAPON_HAMMER); break;
	case ZOMB_ZOOKER: pChr->SetWeapon(WEAPON_HAMMER); break;
	case ZOMB_ZOTTER:
		pChr->SetWeapon(InSight && (HumanAggro || (HasStructure && DistStructure < AR)) ? WEAPON_SHOTGUN : WEAPON_HAMMER); break;
	case ZOMB_ZENADE:
		pChr->SetWeapon(InSight && (DistHuman > 100.0f || DistStructure > 100.0f) && (HumanAggro || (HasStructure && DistStructure < AR)) ? WEAPON_GRENADE : WEAPON_HAMMER); break;
	case ZOMB_SPIDER_BOSS:
		pChr->SetWeapon(InSight && (DistHuman > 80.0f || DistStructure > 80.0f) && (HumanCombat || HumanAggro || (HasStructure && DistStructure < AR)) ? WEAPON_GRENADE : WEAPON_HAMMER); break;
	case ZOMB_ZOOMER:
		pChr->SetWeapon(InSight && (HumanAggro || (HasStructure && DistStructure < AR)) ? WEAPON_LASER : WEAPON_HAMMER); break;
	default:
		pChr->SetWeapon(pChr->GetActiveWeapon() == WEAPON_NINJA ? WEAPON_NINJA : WEAPON_HAMMER); break;
	}
	m_InputData.m_WantedWeapon = pChr->GetActiveWeapon() + 1;

	if(HumanCombat && InSight) AimOff = m_RealTarget - Pos;
	else if(HasStructure) AimOff = StructurePos - Pos;

	int FireDiv = 3;
	switch(Z)
	{
	case ZOMB_ZAMER: FireDiv = 2; break;
	case ZOMB_ZOOKER: FireDiv = 4; break;
	case ZOMB_ZUNNER: case ZOMB_FLOMBIE: FireDiv = 3; break;
	case ZOMB_ZASTER: FireDiv = 5; break;
	case ZOMB_ZOTTER: FireDiv = 4; break;
	case ZOMB_ZENADE: FireDiv = 6; break;
	case ZOMB_SPIDER_BOSS: FireDiv = 4; break;
	case ZOMB_ZOOMER: FireDiv = 4; break;
	case ZOMB_ZINJA: FireDiv = 3; break;
	default: FireDiv = 3; break;
	}
	FireDiv = std::max(1, (int)(FireDiv / (0.45f + 0.55f * Scale)));
	int FP = std::max(1, GS()->Server()->TickSpeed() / std::max(1, FireDiv));
	int AW = pChr->GetActiveWeapon();

	if(AW == WEAPON_NINJA && HumanCombat && DistHuman < 500.0f)
		m_InputData.m_Fire = (GS()->Server()->Tick() % (FP * 2)) < FP ? 1 : 0;
	else if(AW == WEAPON_HAMMER && ((HumanCombat && DistHuman < HAMMER_ATTACK_RANGE) || (HasStructure && DistStructure < HAMMER_ATTACK_RANGE)))
		m_InputData.m_Fire = (GS()->Server()->Tick() % (FP * 2)) < FP ? 1 : 0;
	else if(AW == WEAPON_GUN && pChr->WeaponAmmo(WEAPON_GUN) > 0 && (HumanAggro || (HasStructure && DistStructure < AR)))
		m_InputData.m_Fire = (GS()->Server()->Tick() % (FP * 2)) < FP ? 1 : 0;
	else if(AW == WEAPON_SHOTGUN && InSight && (HumanAggro || (HasStructure && DistStructure < AR)))
		m_InputData.m_Fire = (GS()->Server()->Tick() % (FP * 2)) < FP ? 1 : 0;
	else if(AW == WEAPON_GRENADE && InSight && (HumanAggro || (HasStructure && DistStructure < AR)))
		m_InputData.m_Fire = (GS()->Server()->Tick() % (FP * 3)) < FP ? 1 : 0;
	else if(AW == WEAPON_LASER && InSight && (HumanAggro || (HasStructure && DistStructure < AR)))
		m_InputData.m_Fire = (GS()->Server()->Tick() % (FP * 2)) < FP ? 1 : 0;

	if(m_InputData.m_Fire)
	{
		m_Target = AimOff;
		int Spread = (int)((1.0f - Scale) * 96.0f + 8.0f);
		float Ang = angle(m_Target) + (float)(random_int() % (Spread * 2 + 1) - Spread) * pi / 1024.0f;
		m_Target = direction(Ang) * length(m_Target);
	}
}

// ─── Main Tick ──────────────────────────────────────────────────────

void CZombieAI::TickBrain()
{
	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive()) return;

	if(m_StuckFlipCooldown > 0) m_StuckFlipCooldown--;
	if(m_HookCooldown > 0) m_HookCooldown--;

	UpdateTarget();

	mem_zero(&m_InputData, sizeof(m_InputData));
	m_InputData.m_WantedWeapon = m_LastData.m_WantedWeapon;

	const vec2 Pos = pChr->GetPos();
	CGameControllerDefence *pCtrl = Ctrl(m_pCtrl);
	const bool HasTower = pCtrl->GetTower() != nullptr;
	const vec2 TP = pCtrl->TdGetZombieMarchGoal();
	const float DistTower = HasTower ? distance(Pos, TP) : 1.0e12f;
	CTurret *pTurret = NearestLivingTurret(&GS()->m_World, Pos, TURRET_SEEK_RADIUS);
	const float DistTurret = pTurret ? distance(Pos, pTurret->GetPos()) : 1.0e12f;
	const bool AttackTurret = pTurret && DistTurret < DistTower;
	const bool HasStructure = HasTower || AttackTurret;
	const float DistStructure = AttackTurret ? DistTurret : DistTower;
	const vec2 StructPos = AttackTurret ? pTurret->GetPos() : TP;

	bool InSight = false;
	float DistHuman = 1.0e12f;
	bool HumanCombat = false;
	bool HumanAggro = false;

	if(m_TargetInfo.m_Type == TARGET_PLAYER && m_TargetInfo.m_PlayerCID >= 0)
	{
		CPlayer *pTarget = GS()->m_apPlayers[m_TargetInfo.m_PlayerCID];
		if(pTarget && pTarget->GetCharacter())
		{
			const CCharacterCore *pC = pTarget->GetCharacter()->GetCore();
			InSight = !GS()->Collision()->FastIntersectLine(Pos, pC->m_Pos, nullptr, nullptr);
			m_RealTarget = pC->m_Pos;
			DistHuman = distance(Pos, pC->m_Pos);
			HumanCombat = InSight && DistHuman < GetCombatRadius();
			HumanAggro = InSight && DistHuman < GetAggroRadius();
			if(InSight) m_Target = pC->m_Pos - Pos;
			else UpdateNavigation();
		}
		else m_TargetInfo.m_NeedUpdate = true;
	}

	if(m_TargetInfo.m_Type != TARGET_PLAYER) UpdateNavigation();
	else if(!InSight) { ApplyCrowdSteering(Pos, &m_Target); EnsureMarchDrive(Pos, m_TargetInfo.m_Pos); }

	m_RealTarget = m_Target + Pos;
	MakeChoice();

	int MoveDir = 0;
	if(m_Flags & 2) MoveDir = 1;
	else if(m_Flags & 1) MoveDir = -1;

	HandleStuck(&MoveDir);
	TryZeaterConsume();
	UpdateZinvisState(HumanCombat, HumanAggro);
	TryZeleTeleport(InSight, DistHuman);
	ApplySpecialMovement(InSight, DistHuman);
	if(TryZamerDetonate(DistTower, DistHuman, InSight)) return;

	HandleWeapon(InSight, HumanCombat, HumanAggro, HasStructure, DistStructure, StructPos, DistHuman);
	HandleHook(InSight, MoveDir, DistTower);

	if(m_Flags & 1) m_InputData.m_Direction = -1;
	if(m_Flags & 2) m_InputData.m_Direction = 1;
	if(m_Flags & 4) m_InputData.m_Jump = 1;

	m_InputData.m_TargetX = m_LastData.m_TargetX;
	m_InputData.m_TargetY = m_LastData.m_TargetY;
	if(m_InputData.m_Hook || m_InputData.m_Fire)
	{ m_InputData.m_TargetX = (int)m_Target.x; m_InputData.m_TargetY = (int)m_Target.y; }
	if(m_InputData.m_TargetX == 0 && m_InputData.m_TargetY == 0)
		m_InputData.m_TargetY = -1;

	m_LastData = m_InputData;
}

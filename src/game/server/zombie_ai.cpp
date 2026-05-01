// Comet: AI wrote this AI
/* TeeDefense — zombie brain: path + MakeChoice + HandleHook from Teeworlds-Alchemist bot.cpp; TeeDefense weapons. */

#include <base/math.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/collision.h>
#include <game/gamecore.h>
#include <generated/server_data.h>

#include "botengine.h"
#include "entities/character.h"
#include "entities/tower-main.h"
#include "gamecontext.h"
#include "gamecontroller.h"
#include "player.h"
#include "zombie_nav.h"

namespace
{
constexpr float HUMAN_AGGRO_RADIUS = 900.0f;
constexpr float HUMAN_MELEE_THREAT = 80.0f;
constexpr float HAMMER_ATTACK_RANGE = 85.0f;
constexpr float ZOOKER_RETREAT = 200.0f;
constexpr float ZOOKER_SHOOT_MIN = 180.0f;
constexpr float ZOOKER_IDEAL = 340.0f;
constexpr float ZOMB_GUN_AI_REACH = 800.0f;
constexpr int BOT_HOOK_DIRS = 32;

enum
{
	BFLAG_LEFT = 1,
	BFLAG_RIGHT = 2,
	BFLAG_JUMP = 4,
};

static bool FindNearestDefender(CGameContext *pGame, int ZombId, vec2 ZombPos, vec2 *pOutPos, float *pOutDist,
	int *pOutCid)
{
	float Best = 1.0e12f;
	vec2 BestPos(0.0f, 0.0f);
	int BestCid = -1;
	bool Found = false;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i == ZombId)
			continue;
		CPlayer *pPl = pGame->m_apPlayers[i];
		if(!pPl || pPl->IsDummy() || pPl->GetTeam() == TEAM_SPECTATORS)
			continue;
		CCharacter *pChr = pPl->GetCharacter();
		if(!pChr || !pChr->IsAlive())
			continue;

		const float D = distance(ZombPos, pChr->GetPos());
		if(D < Best)
		{
			Best = D;
			BestPos = pChr->GetPos();
			BestCid = i;
			Found = true;
		}
	}

	if(!Found)
		return false;
	if(pOutDist)
		*pOutDist = Best;
	if(pOutPos)
		*pOutPos = BestPos;
	if(pOutCid)
		*pOutCid = BestCid;
	return true;
}

static void ZombieEnsurePath(CGameContext *pGame, CPlayer *pP, vec2 Pos, vec2 MarchGoal)
{
	CBotEngine *pBE = pGame->BotEngine();
	if(!pBE)
		return;
	CBotEngine::CPath *pPath = &pBE->m_aPaths[pP->GetCID()];
	const float Eps = 48.0f;
	if(pPath->m_Size == 0 || distance(pP->m_ZombAiPathGoal, MarchGoal) > Eps)
	{
		pBE->GetPath(Pos, MarchGoal, pPath);
		pP->m_ZombAiPathGoal = MarchGoal;
	}
}

static void ZombieMakeChoice(CGameContext *pGame, CPlayer *pP, CCharacter *pChr, vec2 MarchGoal, vec2 &TargetOff,
	bool UseTarget, int &Flags)
{
	CBotEngine *pBE = pGame->BotEngine();
	CCollision *pCol = pGame->Collision();
	CCharacterCore *pMe = pChr->GetCore();
	const vec2 Pos = pChr->GetPos();

	if(!UseTarget)
	{
		CBotEngine::CPath *pPath = pBE ? &pBE->m_aPaths[pP->GetCID()] : nullptr;
		if(pBE && pPath && pPath->m_Size)
		{
			vec2 PT;
			const int dist = pBE->FarestPointOnEdge(pPath, Pos, &PT);
			if(dist >= 0)
			{
				UseTarget = true;
				TargetOff = PT - Pos;
			}
			else
				TargetOff = pBE->NextPoint(Pos, MarchGoal) - Pos;
		}
		else if(pBE)
			TargetOff = pBE->NextPoint(Pos, MarchGoal) - Pos;
		else
			TargetOff = MarchGoal - Pos;
	}

	Flags = 0;
	CCharacterCore TempChar = *pMe;
	TempChar.m_Input = pP->m_ZombAiLastInp;

	const vec2 CurPos = TempChar.m_Pos;
	const int CurTile = pBE ? pBE->GetTile(TempChar.m_Pos) : 0;

	const bool Grounded = pChr->IsGrounded();

	TempChar.m_Input.m_Direction = (TargetOff.x > 28.f) ? 1 : (TargetOff.x < -28.f) ? -1 : 0;
	CWorldCore TempWorld;
	TempWorld.m_Tuning = *pGame->Tuning();
	TempChar.Init(&TempWorld, pCol);
	TempChar.Tick(true);
	TempChar.Move();
	TempChar.Quantize();

	const int NextTile = pBE ? pBE->GetTile(TempChar.m_Pos) : 0;
	const vec2 NextPos = TempChar.m_Pos;

	if(TempChar.m_Input.m_Direction > 0)
		Flags |= BFLAG_RIGHT;
	if(TempChar.m_Input.m_Direction < 0)
		Flags |= BFLAG_LEFT;

	if(TargetOff.y < 0)
	{
		if(CurTile & BTILE_SAFE && NextTile & BTILE_HOLE && (Grounded || TempChar.m_Vel.y > 0))
			Flags |= BFLAG_JUMP;
		if(CurTile & BTILE_SAFE && NextTile & BTILE_SAFE)
		{
			if(absolute(CurPos.x - NextPos.x) < 1.0f && TempChar.m_Input.m_Direction)
			{
				if(Grounded)
				{
					Flags |= BFLAG_JUMP;
					pP->m_ZombAiMcJumpTried = true;
				}
				else if(pP->m_ZombAiMcJumpTried && !(TempChar.m_Jumped) && TempChar.m_Vel.y > 0)
					Flags |= BFLAG_JUMP;
				else if(pP->m_ZombAiMcJumpTried && TempChar.m_Jumped & 2 && TempChar.m_Vel.y > 0)
					Flags ^= BFLAG_RIGHT | BFLAG_LEFT;
			}
			else
				pP->m_ZombAiMcJumpTried = false;
		}

		if(!(pMe->m_Jumped))
		{
			vec2 Vel(pMe->m_Vel.x, minimum(pMe->m_Vel.y, 0.0f));
			if(pCol->FastIntersectLine(pMe->m_Pos, pMe->m_Pos + Vel * 10.0f, nullptr, nullptr) &&
				!pCol->FastIntersectLine(pMe->m_Pos,
					pMe->m_Pos + (Vel - vec2(0, TempWorld.m_Tuning.m_AirJumpImpulse)) * 10.0f, nullptr, nullptr))
				Flags |= BFLAG_JUMP;
			if(absolute(TargetOff.x) < 28.f && pMe->m_Vel.y > -1.f)
				Flags |= BFLAG_JUMP;
		}
	}
}

static void ZombieHandleHook(CGameContext *pGame, CPlayer *pP, CCharacter *pChr, vec2 &TargetOff, bool SeeHuman,
	int HumanCid, CNetObj_PlayerInput &Inp, const CNetObj_PlayerInput &LastInp)
{
	CCharacterCore *pMe = pChr->GetCore();
	CCollision *pCol = pGame->Collision();
	CTuningParams *pTuning = pGame->Tuning();
	CBotEngine *pBE = pGame->BotEngine();
	const int CurTile = pBE ? pBE->GetTile(pMe->m_Pos) : 0;

	if(pMe->m_HookState == HOOK_FLYING)
	{
		Inp.m_Hook = 1;
		return;
	}

	bool SeeHookHuman = SeeHuman && HumanCid >= 0;
	if(SeeHookHuman)
	{
		CPlayer *pHum = pGame->m_apPlayers[HumanCid];
		if(pHum && pHum->GetCharacter())
		{
			CCharacterCore *pHC = pHum->GetCharacter()->GetCore();
			const float dist = distance(pHC->m_Pos, pMe->m_Pos);
			if(pMe->m_HookState == HOOK_GRABBED && pMe->m_HookedPlayer == HumanCid)
				Inp.m_Hook = 1;
			else if(!Inp.m_Fire)
			{
				if(dist < pTuning->m_HookLength * 0.9f)
					Inp.m_Hook = LastInp.m_Hook ^ 1;
				SeeHookHuman = dist < pTuning->m_HookLength * 0.9f;
			}
		}
		else
			SeeHookHuman = false;
	}

	if(!SeeHookHuman)
	{
		if(pMe->m_HookState == HOOK_GRABBED && pMe->m_HookedPlayer == -1)
		{
			vec2 HookVel = normalize(pMe->m_HookPos - pMe->m_Pos) * pTuning->m_HookDragAccel;
			if(HookVel.y > 0)
				HookVel.y *= 0.3f;
			if((HookVel.x < 0 && pMe->m_Input.m_Direction < 0) || (HookVel.x > 0 && pMe->m_Input.m_Direction > 0))
				HookVel.x *= 0.95f;
			else
				HookVel.x *= 0.75f;

			HookVel += vec2(0, 1) * pTuning->m_Gravity;

			vec2 Target = TargetOff;
			const float ps = dot(Target, HookVel);
			if(ps > 0 || (CurTile & BTILE_HOLE && TargetOff.y < 0 && pMe->m_Vel.y > 0.f &&
							 pMe->m_HookTick < SERVER_TICK_SPEED + SERVER_TICK_SPEED / 2))
				Inp.m_Hook = 1;
			if(pMe->m_HookTick > 4 * SERVER_TICK_SPEED || length(pMe->m_HookPos - pMe->m_Pos) < 20.0f)
				Inp.m_Hook = 0;
		}
		if(pMe->m_HookState == HOOK_FLYING)
			Inp.m_Hook = 1;

		if(!Inp.m_Fire && LastInp.m_Hook == 0 && pMe->m_HookState == HOOK_IDLE &&
			(rand() % 10 == 0 || (CurTile & BTILE_HOLE && rand() % 4 == 0)))
		{
			vec2 HookDir(0.0f, 0.0f);
			float MaxForce = (CurTile & BTILE_HOLE) ? -10000.0f : 0;
			vec2 Target = TargetOff;
			for(int i = 0; i < BOT_HOOK_DIRS; i++)
			{
				const float a = 2 * i * pi / BOT_HOOK_DIRS;
				vec2 dir = direction(a);
				vec2 HPos = pMe->m_Pos + dir * pTuning->m_HookLength;

				if((pCol->FastIntersectLine(pMe->m_Pos, HPos, &HPos, nullptr) &
						(CCollision::COLFLAG_SOLID | CCollision::COLFLAG_UNHOOKABLE)) == CCollision::COLFLAG_SOLID)
				{
					vec2 HookVel = dir * pTuning->m_HookDragAccel;
					if(HookVel.y > 0)
						HookVel.y *= 0.3f;
					if((HookVel.x < 0 && pMe->m_Input.m_Direction < 0) ||
						(HookVel.x > 0 && pMe->m_Input.m_Direction > 0))
						HookVel.x *= 0.95f;
					else
						HookVel.x *= 0.75f;

					HookVel += vec2(0, 1) * pTuning->m_Gravity;

					const float ps = dot(Target, HookVel);
					if(ps > MaxForce)
					{
						MaxForce = ps;
						HookDir = HPos - pMe->m_Pos;
					}
				}
			}
			if(length(HookDir) > 32.f)
			{
				TargetOff = HookDir;
				Inp.m_Hook = 1;
			}
		}
	}
}

/** Aim / weapon pick (Alchemist HandleWeapon); does not set m_Fire — wave cadence does. */
static void ZombieHandleWeaponAim(CGameContext *pGame, CCharacter *pChr, vec2 Pos, const vec2 *apHumPos, int NumHum,
	bool HasTower, vec2 TowerPos, vec2 &AimOff, CNetObj_PlayerInput &Inp)
{
	CCharacterCore *apTarget[MAX_CLIENTS + 2];
	CCharacterCore aHumTmp[MAX_CLIENTS];
	int Count = 0;

	for(int h = 0; h < NumHum; h++)
	{
		aHumTmp[h].m_Pos = apHumPos[h];
		aHumTmp[h].m_Vel = vec2(0, 0);
		apTarget[Count++] = &aHumTmp[h];
	}

	CCharacterCore TowerCore;
	if(HasTower)
	{
		TowerCore.m_Pos = TowerPos;
		TowerCore.m_Vel = vec2(0, 0);
		apTarget[Count++] = &TowerCore;
	}

	if(Count == 0)
		return;

	int Weapon = -1;
	vec2 WTarget(0, 0);

	for(int c = 0; c < Count; c++)
	{
		const float ClosestRange = distance(Pos, apTarget[c]->m_Pos);
		const float Close = 65.0f;
		WTarget = apTarget[c]->m_Pos - Pos;
		if(ClosestRange < Close)
		{
			Weapon = WEAPON_HAMMER;
			break;
		}
		else if(pChr->WeaponAmmo(WEAPON_GUN) != 0 && ClosestRange < ZOMB_GUN_AI_REACH &&
				!pGame->Collision()->FastIntersectLine(Pos, apTarget[c]->m_Pos, nullptr, nullptr))
		{
			Weapon = WEAPON_GUN;
			break;
		}
	}

	if(Weapon < 0 && pChr->WeaponAmmo(WEAPON_GUN) != 0)
	{
		const int Weapons[] = {WEAPON_GUN};
		for(int j = 0; j < 1 && Weapon < 0; j++)
		{
			if(!pChr->WeaponAmmo(Weapons[j]))
				continue;
			float Curvature = 0, Speed = 0, Time = 0;
			switch(Weapons[j])
			{
			case WEAPON_GUN:
				Curvature = pGame->Tuning()->m_GunCurvature;
				Speed = pGame->Tuning()->m_GunSpeed;
				Time = pGame->Tuning()->m_GunLifetime;
				break;
			default:
				continue;
			}
			const int NbLoops = 10;
			vec2 aProjectilePos[BOT_HOOK_DIRS];
			vec2 aTargetPos[MAX_CLIENTS + 2];
			vec2 aTargetVel[MAX_CLIENTS + 2];

			const int DTick = maximum(1, (int)(Time * pGame->Server()->TickSpeed() / NbLoops));

			for(int c = 0; c < Count; c++)
			{
				aTargetPos[c] = apTarget[c]->m_Pos;
				aTargetVel[c] = apTarget[c]->m_Vel * DTick;
			}

			for(int i = 0; i < BOT_HOOK_DIRS; i++)
			{
				vec2 dir = direction(2 * i * pi / BOT_HOOK_DIRS);
				aProjectilePos[i] = Pos + dir * 28.f * 0.75f;
			}

			int aIsDead[BOT_HOOK_DIRS] = {0};
			int GoodDir = -1;

			for(int k = 0; k < NbLoops && GoodDir == -1; k++)
			{
				for(int i = 0; i < BOT_HOOK_DIRS; i++)
				{
					if(aIsDead[i])
						continue;
					vec2 dir = direction(2 * i * pi / BOT_HOOK_DIRS);
					vec2 NextPos =
						CalcPos(Pos + dir * 28.f * 0.75f, dir, Curvature, Speed, (k + 1) * Time / NbLoops);
					aIsDead[i] = pGame->Collision()->FastIntersectLine(aProjectilePos[i], NextPos, &NextPos, nullptr);
					for(int c = 0; c < Count; c++)
					{
						vec2 InterPos = closest_point_on_line(aProjectilePos[i], NextPos, aTargetPos[c]);
						if(distance(aTargetPos[c], InterPos) < 28)
						{
							GoodDir = i;
							break;
						}
					}
					aProjectilePos[i] = NextPos;
				}
				for(int c = 0; c < Count; c++)
				{
					pGame->Collision()->FastIntersectLine(aTargetPos[c], aTargetPos[c] + aTargetVel[c], nullptr,
						&aTargetPos[c]);
					aTargetVel[c].y += pGame->Tuning()->m_Gravity * DTick * DTick;
				}
			}
			if(GoodDir != -1)
			{
				WTarget = direction(2 * GoodDir * pi / BOT_HOOK_DIRS) * 50.0f;
				Weapon = Weapons[j];
				break;
			}
		}
	}

	if(Weapon > -1)
	{
		Inp.m_WantedWeapon = Weapon + 1;
		AimOff = WTarget;
	}
	else if(pChr->WeaponAmmo(WEAPON_GUN) > 0)
		Inp.m_WantedWeapon = WEAPON_GUN + 1;

	if(length(AimOff) > 1.0f)
	{
		const float AngleJ = angle(AimOff) + (float)(rand() % 64 - 32) * pi / 1024.0f;
		AimOff = direction(AngleJ) * length(AimOff);
	}
}
} // namespace

void CGameController::TdRunZombieBrain(CPlayer *pP)
{
	CGameContext *pGame = GameServer();
	CCharacter *pChr = pP->GetCharacter();
	if(!pChr || !pChr->IsAlive())
		return;

	if(pP->m_ZombAiHookCooldown > 0)
		pP->m_ZombAiHookCooldown--;

	const vec2 Pos = pChr->GetPos();

	constexpr int HumanScanPeriod = 5;
	const bool NeedHumanScan = (Server()->Tick() - pP->m_ZombAiHumanScanTick) >= HumanScanPeriod;
	vec2 HumanPos(0.0f, 0.0f);
	float DistHuman = 1.0e12f;
	bool HasHuman = false;
	int HumanCid = -1;
	if(NeedHumanScan)
	{
		HasHuman = FindNearestDefender(pGame, pP->GetCID(), Pos, &HumanPos, &DistHuman, &HumanCid);
		pP->m_ZombAiCachedHasHuman = HasHuman;
		pP->m_ZombAiCachedHumanPos = HumanPos;
		pP->m_ZombAiCachedHumanDist = DistHuman;
		pP->m_ZombAiCachedHumanCid = HumanCid;
		pP->m_ZombAiHumanScanTick = Server()->Tick();
	}
	else
	{
		HasHuman = pP->m_ZombAiCachedHasHuman;
		HumanPos = pP->m_ZombAiCachedHumanPos;
		HumanCid = pP->m_ZombAiCachedHumanCid;
		if(HasHuman)
			DistHuman = distance(Pos, HumanPos);
		else
			DistHuman = 1.0e12f;
	}

	if(HasHuman && HumanCid >= 0)
	{
		CPlayer *pHM = pGame->m_apPlayers[HumanCid];
		CCharacter *pHChr = pHM ? pHM->GetCharacter() : nullptr;
		if(!pHChr || !pHChr->IsAlive())
		{
			HasHuman = false;
			HumanCid = -1;
			DistHuman = 1.0e12f;
			pP->m_ZombAiCachedHasHuman = false;
			pP->m_ZombAiCachedHumanCid = -1;
		}
		else
		{
			HumanPos = pHChr->GetPos();
			DistHuman = distance(Pos, HumanPos);
			pP->m_ZombAiCachedHumanPos = HumanPos;
			pP->m_ZombAiCachedHumanDist = DistHuman;
		}
	}

	const bool HasTower = m_pTower != nullptr;
	vec2 RallyPos = TdGetZombieRallyPos();
	const vec2 TowerPos = HasTower ? m_pTower->GetPos() : RallyPos;
	const float DistMarch = distance(Pos, TowerPos);
	const vec2 MarchGoal = TowerPos;

	const int Z = pP->GetZomb();
	const bool IsZooker = Z == ZOMB_ZOOKER;
	const bool IsZaber = Z == ZOMB_ZABER;

	const bool HumanLos =
		HasHuman && !pGame->Collision()->FastIntersectLine(Pos, HumanPos, nullptr, nullptr);

	ZombieEnsurePath(pGame, pP, Pos, MarchGoal);

	vec2 TargetOff(0.0f, 0.0f);
	bool UseHumanSteer = HumanLos;
	if(UseHumanSteer)
		TargetOff = HumanPos - Pos;

	int Flags = 0;
	ZombieMakeChoice(pGame, pP, pChr, MarchGoal, TargetOff, UseHumanSteer, Flags);

	if(IsZooker && HasHuman && DistHuman < HUMAN_AGGRO_RADIUS && DistHuman < ZOOKER_RETREAT && DistHuman > 1.0f)
	{
		vec2 Flee = Pos - HumanPos;
		Flags &= ~(BFLAG_LEFT | BFLAG_RIGHT);
		if(absolute(Flee.x) > 10.0f)
		{
			if(Flee.x < 0.0f)
				Flags |= BFLAG_LEFT;
			else
				Flags |= BFLAG_RIGHT;
		}
		pChr->SetWeapon(WEAPON_HAMMER);
	}
	else if(IsZooker && pChr->WeaponAmmo(WEAPON_GUN) > 0 &&
		((HasTower && DistMarch > ZOOKER_SHOOT_MIN) || (!HasTower && HasHuman && DistHuman > ZOOKER_SHOOT_MIN)))
	{
		pChr->SetWeapon(WEAPON_GUN);
	}
	else
	{
		pChr->SetWeapon(WEAPON_HAMMER);
	}

	const int FireDiv = IsZaber ? 2 : (IsZooker ? 4 : 3);
	const int FirePeriod = maximum(1, Server()->TickSpeed() / maximum(1, FireDiv));

	int MoveDir = 0;
	if(Flags & BFLAG_RIGHT)
		MoveDir = 1;
	else if(Flags & BFLAG_LEFT)
		MoveDir = -1;
	pP->m_ZombAiLastMoveDir = MoveDir;

	const vec2 ColBox(CCharacterCore::PHYS_SIZE, CCharacterCore::PHYS_SIZE);
	const vec2 Ahead = Pos + vec2((float)MoveDir * 34.0f, -6.0f);
	const bool BlockedAhead = GameServer()->Collision()->TestBox(Ahead, ColBox);

	if(absolute(pChr->GetVelocity().x) < 0.5f)
		pP->m_ZombAiLowSpeedTicks++;
	else
		pP->m_ZombAiLowSpeedTicks = 0;

	if(pP->m_ZombAiLowSpeedTicks > 30)
	{
		ZombieNavClear(pP);
		if(CBotEngine *pBE = pGame->BotEngine())
			pBE->m_aPaths[pP->GetCID()].m_Size = 0;
		pP->m_ZombAiPathGoal = vec2(1.0e9f, 1.0e9f);
		pP->m_ZombAiLastMoveDir = -pP->m_ZombAiLastMoveDir;
		MoveDir = pP->m_ZombAiLastMoveDir;
		pP->m_ZombAiLowSpeedTicks = 0;
		Flags &= ~(BFLAG_LEFT | BFLAG_RIGHT);
		if(MoveDir < 0)
			Flags |= BFLAG_LEFT;
		else if(MoveDir > 0)
			Flags |= BFLAG_RIGHT;
	}

	CNetObj_PlayerInput Inp;
	mem_zero(&Inp, sizeof(Inp));
	Inp.m_WantedWeapon = pChr->GetActiveWeapon() + 1;

	vec2 AimOff = TargetOff;
	const vec2 *pHumList = HasHuman ? &HumanPos : nullptr;
	const int NumHum = HasHuman ? 1 : 0;
	ZombieHandleWeaponAim(pGame, pChr, Pos, pHumList, NumHum, HasTower, TowerPos, AimOff, Inp);

	if(IsZooker && pChr->GetActiveWeapon() == WEAPON_GUN && HasTower && DistMarch > ZOOKER_IDEAL &&
		DistMarch > ZOOKER_SHOOT_MIN)
	{
		vec2 Side(normalize(vec2(-(TowerPos.y - Pos.y), TowerPos.x - Pos.x)));
		if((Server()->Tick() / 40) % 2 == 0)
			Side *= -1.0f;
		if(length(AimOff) > 1.0f)
			AimOff = normalize(AimOff + Side * 0.35f) * length(AimOff);
		if(absolute(AimOff.x) > 0.2f)
		{
			MoveDir = AimOff.x < 0.0f ? -1 : 1;
			pP->m_ZombAiLastMoveDir = MoveDir;
			Flags &= ~(BFLAG_LEFT | BFLAG_RIGHT);
			if(MoveDir < 0)
				Flags |= BFLAG_LEFT;
			else if(MoveDir > 0)
				Flags |= BFLAG_RIGHT;
		}
	}

	if(Flags & BFLAG_LEFT)
		Inp.m_Direction = -1;
	if(Flags & BFLAG_RIGHT)
		Inp.m_Direction = 1;
	if(Flags & BFLAG_JUMP)
		Inp.m_Jump = 1;

	ZombieHandleHook(pGame, pP, pChr, TargetOff, HumanLos, HumanCid, Inp, pP->m_ZombAiLastInp);

	if(Inp.m_Hook || Inp.m_Fire)
	{
		Inp.m_TargetX = (int)AimOff.x;
		Inp.m_TargetY = (int)AimOff.y;
	}
	else
	{
		Inp.m_TargetX = (int)AimOff.x;
		Inp.m_TargetY = (int)AimOff.y;
	}

	if(Inp.m_TargetX == 0 && Inp.m_TargetY == 0)
		Inp.m_TargetY = -1;

	const bool Hammering = pChr->GetActiveWeapon() == WEAPON_HAMMER;
	const bool InTowerHammer = HasTower && DistMarch < HAMMER_ATTACK_RANGE;
	const bool InHumanHammer = HasHuman && DistHuman < HAMMER_ATTACK_RANGE;
	const bool ShootGun = pChr->GetActiveWeapon() == WEAPON_GUN && pChr->WeaponAmmo(WEAPON_GUN) > 0 &&
		(!HasHuman || DistHuman > ZOOKER_RETREAT * 0.65f) && (HasTower ? DistMarch > ZOOKER_RETREAT * 0.5f : true);

	if(Hammering && (InTowerHammer || InHumanHammer))
		Inp.m_Fire = (Server()->Tick() % (FirePeriod * 2)) < FirePeriod ? 1 : 0;
	else if(ShootGun)
		Inp.m_Fire = (Server()->Tick() % (FirePeriod * 2)) < FirePeriod ? 1 : 0;
	else
		Inp.m_Fire = 0;

	if(Inp.m_Fire)
	{
		Inp.m_TargetX = (int)AimOff.x;
		Inp.m_TargetY = (int)AimOff.y;
	}

	if(pP->m_ZombAiJumpCooldown > 0)
		pP->m_ZombAiJumpCooldown--;
	else if(pChr->IsGrounded())
	{
		const bool WantJump = BlockedAhead || (TargetOff.y < -40.0f && DistMarch < 500.0f) ||
			(pP->m_ZombAiLowSpeedTicks > 14);
		if(WantJump)
		{
			Inp.m_Jump = 1;
			pP->m_ZombAiJumpCooldown = 11;
			if(pP->m_ZombAiLowSpeedTicks > 14)
				pP->m_ZombAiLowSpeedTicks = 0;
		}
	}

	const int HS = pChr->HookState();
	vec2 ToMove = TargetOff;
	if(absolute(pChr->GetVelocity().x) < 0.5f && length(ToMove) < 1.0f)
		ToMove = vec2((float)pP->m_ZombAiLastMoveDir, 0.0f);

	if(HS == HOOK_FLYING || HS == HOOK_GRABBED)
	{
		const bool LetGo = pChr->IsGrounded() && ToMove.y > -38.0f && !BlockedAhead;
		if(LetGo)
		{
			Inp.m_Hook = 0;
			pP->m_ZombAiHookCooldown = 10;
		}
	}

	pP->m_ZombAiLastInp = Inp;
	pP->OnPredictedInput(&Inp);
	pP->OnDirectInput(&Inp);
}

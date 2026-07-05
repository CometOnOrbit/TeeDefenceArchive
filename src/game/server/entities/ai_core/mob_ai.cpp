#include "mob_ai.h"
#include "mob_ability_executor.h"
#include "mob_combat.h"
#include <game/server/entities/character_bot_ai.h>
#include <vector>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/core/tools/path_finder.h>
#include <game/server/core/components/mmo/mmo_types.h>
#include <game/collision.h>
#include <base/math.h>

CMobAI::CMobAI(CCharacterBotAI *pCharacter, float ActiveRadius)
	: CBaseAI(pCharacter), m_ActiveRadius(ActiveRadius) {}

void CMobAI::SetZone(const char *pZoneName, vec2 BoundsMin, vec2 BoundsMax)
{
	if(pZoneName)
		str_copy(m_ZoneName, pZoneName, sizeof(m_ZoneName));
	m_ZoneBounds[0] = BoundsMin;
	m_ZoneBounds[1] = BoundsMax;
}

bool CMobAI::IsOutsideZone() const
{
	if(!m_pCharacter) return false;
	vec2 Pos = m_pCharacter->GetPos();
	return (Pos.x < m_ZoneBounds[0].x || Pos.x > m_ZoneBounds[1].x ||
			Pos.y < m_ZoneBounds[0].y || Pos.y > m_ZoneBounds[1].y);
}

void CMobAI::SetMobInfo(const SMMOMobDef *pInfo)
{
	m_pMobInfo = pInfo;
	m_aAbilityCooldownEnd.clear();
	if(pInfo)
		m_aAbilityCooldownEnd.assign(pInfo->m_vAbilities.size(), 0);
}

bool CMobAI::IsBusyCasting() const
{
	return Server()->Tick() < m_CastingUntilTick;
}

void CMobAI::OnSpawn()
{
	m_LastAttackTick = Server()->Tick();
	m_LastDamageTick = 0;
	m_BehaviorPoisonedNextTick = Server()->Tick() + Server()->TickSpeed() * 5;
	m_BehaviorSkillNextTick = Server()->Tick() + Server()->TickSpeed() * 3;
	m_BehaviorNeutral = false;
	m_LastAmbientChatTick = Server()->Tick();
	m_AggroAbilityUsed = false;
	m_CastingUntilTick = 0;
	m_HoldPosition = false;
	if(m_pMobInfo)
		m_aAbilityCooldownEnd.assign(m_pMobInfo->m_vAbilities.size(), 0);
}

bool CMobAI::CanDamage(CPlayer *pFrom)
{
	if(!pFrom)
		return false;
	if(pFrom->m_pMMOBotData)
		return false;

	// NEUTRAL behavior: don't attack unless provoked
	if(m_BehaviorNeutral)
	{
		int NeutralTimeout = Server()->TickSpeed() * 5;
		if(Server()->Tick() - m_LastDamageTick > NeutralTimeout)
			return false;
	}

	return true;
}

void CMobAI::OnTakeDamage(int Dmg, int From, int Weapon)
{
	m_LastDamageTick = Server()->Tick();

	// NEUTRAL: wake up on attack
	if(m_BehaviorNeutral)
		m_BehaviorNeutral = false;

	// SLEEPY: wake up on attack
	// (handled by HandleBehaviors checking m_LastDamageTick)

	if(m_Target.IsEmpty() || m_Target.GetType() == ETargetType::Lost)
		m_Target.Set(From, 200);
}

void CMobAI::OnDie(int Killer, int Weapon)
{
	// Rewards handled by CCharacterBotAI::Die
}

void CMobAI::OnRewardPlayer(CPlayer *pForPlayer) const
{
	// Boss rewards are handled by CCharacterBotAI::Die
	// This hook is available for custom per-mob reward logic.
	(void)pForPlayer;
}

void CMobAI::UpdateTarget()
{
	CGameContext *pGS = GS();
	if(!pGS) return;

	if(!m_Target.IsEmpty())
	{
		int TargetCID = m_Target.GetCID();
		CPlayer *pTarget = pGS->m_apPlayers[TargetCID];
		if(!pTarget || !pTarget->GetCharacter() || !pTarget->GetCharacter()->IsAlive())
		{
			m_Target.SetType(ETargetType::Lost);
			m_pCharacter->m_BotTargetPos.reset();
			return;
		}

		// Zone constraint: only chase targets within zone bounds
		vec2 TargetPos = pTarget->GetCharacter()->GetPos();
		if(m_ZoneName[0] && (TargetPos.x < m_ZoneBounds[0].x || TargetPos.x > m_ZoneBounds[1].x ||
			TargetPos.y < m_ZoneBounds[0].y || TargetPos.y > m_ZoneBounds[1].y))
		{
			m_Target.SetType(ETargetType::Lost);
			m_pCharacter->m_BotTargetPos.reset();
			return;
		}

		float d = distance(m_pCharacter->GetPos(), TargetPos);
		if(d > m_ActiveRadius * 1.5f)
		{
			m_Target.SetType(ETargetType::Lost);
			m_pCharacter->m_BotTargetPos.reset();
			return;
		}

		bool Intersected = pGS->Collision()->IntersectLine(m_pCharacter->GetPos(), TargetPos, nullptr, nullptr);
		m_Target.UpdateCollided(Intersected);

		m_Target.SetType(ETargetType::Active);
		m_pCharacter->m_BotTargetPos = TargetPos;
		return;
	}

	int CID = SearchNearestPlayer(m_ActiveRadius, true);
	if(CID >= 0)
	{
		// Check that we can actually damage this target (protects NPCs)
		CPlayer *pTarget = pGS->m_apPlayers[CID];
		if(pTarget && pTarget->GetCharacter())
		{
			// Zone constraint: only aggro players within zone bounds
			if(m_ZoneName[0])
			{
				vec2 PlayerPos = pTarget->GetCharacter()->GetPos();
				if(PlayerPos.x < m_ZoneBounds[0].x || PlayerPos.x > m_ZoneBounds[1].x ||
					PlayerPos.y < m_ZoneBounds[0].y || PlayerPos.y > m_ZoneBounds[1].y)
				{
					m_pCharacter->m_BotTargetPos.reset();
					return;
				}
			}

			// NPCs and invulnerable bot targets are skipped
			if(pTarget->IsQuestNpc())
			{
				m_pCharacter->m_BotTargetPos.reset();
				return;
			}
			CCharacterBotAI *pBotTarget = dynamic_cast<CCharacterBotAI*>(pTarget->GetCharacter());
			if(pBotTarget && !pBotTarget->IsAllowedPVP(m_ClientID))
			{
				// Can't damage this target — skip it
				m_pCharacter->m_BotTargetPos.reset();
				return;
			}
			m_Target.Set(CID, 200);
			m_pCharacter->m_BotTargetPos = pTarget->GetCharacter()->GetPos();
		}
	}
	else
	{
		m_pCharacter->m_BotTargetPos.reset();
	}
}

void CMobAI::HandleBehaviors(bool *pbAsleep)
{
	if(!m_pMobInfo || !m_pCharacter || !m_pCharacter->IsAlive())
		return;

	CGameContext *pGS = GS();
	if(!pGS) return;

	const int Flags = m_pMobInfo->m_BehaviorFlags;
	const int Now = Server()->Tick();
	const int TickSpeed = Server()->TickSpeed();

	// ── POISONOUS: Create death (poison cloud) visual effect ──
	if(Flags & MOBFLAG_BEHAVIOR_POISONOUS)
	{
		if(Now >= m_BehaviorPoisonedNextTick)
		{
			// Random position around the mob
			vec2 Pos = m_pCharacter->GetPos();
			float Angle = random_float() * 2.0f * pi;
			float Dist = 32.0f + random_float() * 64.0f;
			vec2 CloudPos = Pos + vec2(cos(Angle) * Dist, sin(Angle) * Dist);

			// Create death visual effect (TDA doesn't have MRPG Effect system)
			pGS->m_World.CreateDeath(CloudPos, -1);

			m_BehaviorPoisonedNextTick = Now + TickSpeed * (3 + random_int() % 4);
		}
	}

	// ── NEUTRAL: passive until attacked ──
	if(Flags & MOBFLAG_BEHAVIOR_NEUTRAL)
	{
		int NeutralTimeout = TickSpeed * 5;
		if(Now - m_LastDamageTick > NeutralTimeout && !m_BehaviorNeutral)
		{
			m_BehaviorNeutral = true;
			m_Target.SetType(ETargetType::Lost);
			m_pCharacter->m_BotTargetPos.reset();
		}
	}

	// ── SLEEPY: idle with ZZZ emotes until attacked ──
	if(Flags & MOBFLAG_BEHAVIOR_SLEEPY)
	{
		int SleepTimeout = TickSpeed * 5;
		if(Now - m_LastDamageTick > SleepTimeout)
		{
			*pbAsleep = true;

			// Every 1 second, show ZZZ emoticon
			if(Now % TickSpeed == 0)
			{
				m_pCharacter->SetEmote(EMOTE_BLINK, Now + TickSpeed / 2);
			}
		}
	}

	// ── SLOWER: handled in OnHandleTunning via character tuning ──
	(void)Flags;
}

void CMobAI::HandleAmbientChat()
{
	if(!m_pMobInfo || !(m_pMobInfo->m_BehaviorFlags & MOBFLAG_BEHAVIOR_AMBIENT_CHAT))
		return;

	if(!m_pCharacter || !m_pCharacter->IsAlive())
		return;

	CGameContext *pGS = GS();
	if(!pGS) return;

	const int Now = Server()->Tick();
	const int TickSpeed = Server()->TickSpeed();

	// Once per second, 1.5% chance
	if(Now - m_LastAmbientChatTick < TickSpeed)
		return;
	m_LastAmbientChatTick = Now;

	if(random_int() % 100 >= 1)
		return;

	// Find a nearby player to target the chat at
	int TargetCID = SearchNearestPlayer(600.0f, true);
	if(TargetCID < 0)
		return;

	// Random template messages
	static const char * const aChats[] = {
		"...",
		"谁在那里？",
		"离我远点...",
		"这片区域不太平。",
		"你听到了吗？",
		"...别靠近。",
		"呼...呼...",
		"你看起来很好吃。",
		"嘎——！",
		"嗯？是什么声音？",
	};
	const int NumChats = (int)(sizeof(aChats) / sizeof(aChats[0]));
	int Idx = random_int() % NumChats;

	pGS->SendChat(TargetCID, CHAT_ALL, -1, aChats[Idx]);
}

void CMobAI::HandleSkillBehaviors()
{
	TryMobAbilities();
}

void CMobAI::TryMobAbilities()
{
	if(!m_pMobInfo || m_pMobInfo->m_vAbilities.empty())
		return;
	if(!m_pCharacter || !m_pCharacter->IsAlive())
		return;

	CPlayer *pMobPlayer = m_pCharacter->GetPlayer();
	SMMOBotData *pData = pMobPlayer ? pMobPlayer->m_pMMOBotData : nullptr;
	if(!pData)
		return;

	const int Now = Server()->Tick();
	if(Now < m_CastingUntilTick)
		return;

	CGameContext *pGS = GS();
	if(!pGS)
		return;

	m_HoldPosition = false;

	for(size_t i = 0; i < m_pMobInfo->m_vAbilities.size(); i++)
	{
		const SMMOMobAbilityDef &Ability = m_pMobInfo->m_vAbilities[i];
		if(i >= m_aAbilityCooldownEnd.size())
			break;
		if(Now < m_aAbilityCooldownEnd[i])
			continue;

		bool ShouldCast = false;
		vec2 TargetPos = m_pCharacter->GetPos();
		float DistToTarget = 0.f;

		CPlayer *pTarget = nullptr;
		if(!m_Target.IsEmpty())
		{
			const int TargetCID = m_Target.GetCID();
			if(TargetCID >= 0 && TargetCID < MAX_CLIENTS)
				pTarget = pGS->m_apPlayers[TargetCID];
		}

		switch(Ability.m_Trigger)
		{
		case MOB_ABILITY_IN_RANGE:
			if(pTarget && pTarget->GetCharacter() && pTarget->GetCharacter()->IsAlive())
			{
				TargetPos = pTarget->GetCharacter()->GetPos();
				DistToTarget = distance(m_pCharacter->GetPos(), TargetPos);
				ShouldCast = DistToTarget <= Ability.m_Range;
			}
			break;
		case MOB_ABILITY_ON_AGGRO:
			if(!m_AggroAbilityUsed && pTarget && pTarget->GetCharacter())
			{
				TargetPos = pTarget->GetCharacter()->GetPos();
				ShouldCast = true;
			}
			break;
		case MOB_ABILITY_ON_LOW_HP:
			if(pData->m_MaxHP > 0 && pData->GetHPPct() * 100.f <= Ability.m_ThresholdPct)
				ShouldCast = true;
			break;
		case MOB_ABILITY_PERIODIC:
			ShouldCast = true;
			break;
		}

		if(!ShouldCast)
			continue;

		if(length(TargetPos - m_pCharacter->GetPos()) > 0.01f)
			m_pCharacter->SetAim(TargetPos - m_pCharacter->GetPos());

		if(ExecuteMobAbility(m_pCharacter, Ability, pData->m_Attack, pData->m_Level))
		{
			m_aAbilityCooldownEnd[i] = Now + maximum(1, Ability.m_CooldownTicks);
			if(Ability.m_Trigger == MOB_ABILITY_ON_AGGRO)
				m_AggroAbilityUsed = true;
			if(Ability.m_CastTicks > 0)
				m_CastingUntilTick = Now + Ability.m_CastTicks;
			if(m_pMobInfo->m_Archetype == MOB_ARCHETYPE_CASTER && Ability.m_Trigger == MOB_ABILITY_IN_RANGE)
				m_HoldPosition = true;
			return;
		}
	}
}

void CMobAI::ShowHealth()
{
	if(!m_pCharacter || !m_pCharacter->IsAlive())
		return;

	CPlayer *pMyPlayer = m_pCharacter->GetPlayer();
	if(!pMyPlayer) return;

	CGameContext *pGS = GS();
	if(!pGS) return;

	SMMOBotData *pData = pMyPlayer->m_pMMOBotData;
	if(!pData || pData->m_MaxHP <= 0)
		return;

	int pct = clamp((int)(pData->GetHPPct() * 100.0f), 0, 100);
	char aClan[64];

	if(pData->m_IsBoss)
		str_format(aClan, sizeof(aClan), "[BOSS] %d/%d [%d%%]", pData->m_HP, pData->m_MaxHP, pct);
	else
		str_format(aClan, sizeof(aClan), "[HP: %d/%d %d%%]", pData->m_HP, pData->m_MaxHP, pct);

	pGS->Server()->SetClientClan(pMyPlayer->GetCID(), aClan);
}

void CMobAI::Process()
{
	if(!m_pCharacter || !m_pCharacter->IsAlive()) return;

	CGameContext *pGS = GS();
	if(!pGS) return;

	// ── Step 1: Behavior system ──
	bool bAsleep = false;
	HandleBehaviors(&bAsleep);

	// ── Step 2: Ambient chat ──
	HandleAmbientChat();

	// ── Step 3: Skill behaviors ──
	HandleSkillBehaviors();

	// ── Step 4: If asleep or casting, return early ──
	if(bAsleep || IsBusyCasting())
		return;

	// ── Zone patrol: if outside zone, return to zone center ──
	if(m_ZoneName[0] && IsOutsideZone())
	{
		vec2 ZoneCenter = vec2(
			(m_ZoneBounds[0].x + m_ZoneBounds[1].x) / 2.0f,
			(m_ZoneBounds[0].y + m_ZoneBounds[1].y) / 2.0f
		);

		// Clear target — stop fighting, go back to zone
		m_Target.Reset();
		m_pCharacter->m_BotTargetPos = ZoneCenter;
		m_pCharacter->Move();
		return;
	}

	m_Target.Tick();
	UpdateTarget();

	// Update HP display
	ShowHealth();

	// No target - idle
	if(m_Target.IsEmpty())
		return;

	CPlayer *pTarget = pGS->m_apPlayers[m_Target.GetCID()];
	if(!pTarget || !pTarget->GetCharacter() || !pTarget->GetCharacter()->IsAlive())
	{
		m_Target.SetType(ETargetType::Lost);
		m_pCharacter->m_BotTargetPos.reset();
		return;
	}

	// Neutral behavior: don't attack
	if(m_BehaviorNeutral)
	{
		m_pCharacter->m_BotTargetPos.reset();
		return;
	}

	if(m_HoldPosition && m_pMobInfo && m_pMobInfo->m_Archetype == MOB_ARCHETYPE_CASTER)
	{
		m_pCharacter->m_BotTargetPos.reset();
		m_pCharacter->Fire();
		return;
	}

	// ── Step 5: Update target position + fire + move ──
	m_pCharacter->m_BotTargetPos = pTarget->GetCharacter()->GetPos();
	m_pCharacter->Fire();
	m_pCharacter->Move();
}

void CMobAI::OnTargetRules(float Radius)
{
	(void)Radius;
}

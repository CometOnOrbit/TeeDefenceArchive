#include "strategy.h"

#include <base/math.h>

CStrategyPosition::CStrategyPosition(CBotEngine* pBotEngine) : m_pBotEngine(pBotEngine)
{
	m_Zone = GetDefaultZone();
}

bool CStrategyPosition::AttackPlayer(const CCharacter *pBot, const CCharacter *pEnemy)
{
	if(!pBot || !pEnemy)
		return false;

	const vec2 BotPos = pBot->GetPos();
	const vec2 EnemyPos = pEnemy->GetPos();
	const float Dist = distance(BotPos, EnemyPos);

	const bool BotInZone = IsInsideZone(BotPos);
	const bool EnemyInZone = IsInsideZone(EnemyPos);
	const float EngageRange = maximum(280.0f, (float)m_Zone.Range() * 0.55f);

	return BotInZone && (EnemyInZone || Dist < EngageRange);
}

bool CStrategyPosition::FollowPlayer(const CCharacter *pBot, const CCharacter *pEnemy)
{
	if(!pBot || !pEnemy)
		return false;

	const vec2 BotPos = pBot->GetPos();
	const vec2 EnemyPos = pEnemy->GetPos();

	if(IsInsideZone(EnemyPos))
		return true;

	const float PatrolRange = (float)m_Zone.Range() * 1.35f;
	return IsInsideZone(BotPos) && distance(BotPos, EnemyPos) < PatrolRange;
}

bool CStrategyPosition::IsInsideZone(const vec2& Pos)
{
	return distance(Pos, m_Zone.Center()) < m_Zone.Range();
}

CZone CStrategyPosition::GetDefaultZone()
{
	CZone Zone;
	Zone.m_Center = vec2(BotEngine()->GetWidth(), BotEngine()->GetHeight())*16;
	Zone.m_Range = maximum(BotEngine()->GetWidth(), BotEngine()->GetHeight())*16;
	return Zone;
}

/* Adapted from Teeworlds-MRPG-0.6 character_bot.h */

#ifndef GAME_SERVER_ENTITIES_CHARACTER_BOT_AI_H
#define GAME_SERVER_ENTITIES_CHARACTER_BOT_AI_H

#include "character.h"
#include "ai_core/base_ai.h"
#include <optional>
#include <unordered_map>

class CCharacterBotAI : public CCharacter
{
	public:
	static bool PoolSlotFree(int id);
	MACRO_ALLOC_POOL_ID()

	std::unique_ptr<CBaseAI> m_pAI{};
	CPlayer *m_pBotPlayer{};

public:
	std::optional<vec2> m_BotTargetPos{};
	PathRequestHandle m_BotPathHandle{};
	vec2 m_PrevPos{};

	int m_MoveTick{};
	int m_StrafeDirection {};
	int m_LastStrafeChangeTick {};
	int m_IntervalChangeWeapon {};
	int m_PrevDirection{};
	vec2 m_DieForce {};
	std::optional<int> m_ForcedActiveWeapon {};
	std::unordered_map< int, int > m_aDamageByPlayer {};
	bool m_WantHook{ false };
	char m_ZoneName[64] = {}; // what zone this bot was spawned in

public:
	CCharacterBotAI(CGameWorld* pWorld);

	CBaseAI* AI() const { return m_pAI.get(); }
	void SetAI(std::unique_ptr<CBaseAI> pAI) { m_pAI = std::move(pAI); }
	void SetForcedWeapon(int WeaponID);
	void ClearForcedWeapon();

	bool Spawn(CPlayer *pPlayer, vec2 Pos);
	void Tick() override;
	void TickDefered() override;
	void Snap(int SnappingClient) override;
	bool TakeHit(vec2 Force, vec2 Source, int Dmg, CEntity *pFrom, int Weapon) override;
	void Die(int Killer, int Weapon);

	void SelectWeaponAtRandomInterval();
	void SelectEmoteAtRandomInterval();

	bool GiveWeapon(int Weapon, int GiveAmmo);
	bool IsAllowedPVP(int FromID);

	void SetBotInput(const CNetObj_PlayerInput &Input) { m_Input = Input; }
	void SetAim(vec2 Dir);
	void Move();
	void Fire();
	void ProcessBot();
};

#endif

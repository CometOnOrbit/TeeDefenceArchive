#ifndef GAME_SERVER_CORE_COMPONENTS_META_DURABILITY_MANAGER_H
#define GAME_SERVER_CORE_COMPONENTS_META_DURABILITY_MANAGER_H

#include <game/server/core/tworld_component.h>
#include <game/server/core/tools/event_listener.h>

class CCommandManager;
class CItemHelper;
class CPlayer;

class CDurabilityManager : public TWorldComponent, public IGameEventListener
{
public:
	CDurabilityManager();

	void OnInitWorld(const char *pWhereLocalWorld) override;
	void OnShutdown() override;

	void OnCharacterDeath(CPlayer *pVictim, CPlayer *pKiller, int Weapon) override;
	void OnPlayerMine(CPlayer *pPlayer, int MatId) override;

	void RegisterVoteCommands(CCommandManager *pMgr);
	bool OnPlayerVoteCommand(CPlayer *pPlayer, const char *pCmd, const char *pArgs);

	static int GetDurability(CItemHelper *pH, int ItemId, const char *pExtra);
	static void SetDurability(char *pExtra, int ExtraSize, int Dur);
	static bool ItemHasDurability(CItemHelper *pH, int ItemId);
	static void EnsureDurability(char *pExtra, int ExtraSize);

	void DamageEquipped(CPlayer *pPlayer, int Amount);
	void DamageHoldingTool(CPlayer *pPlayer, int HoldKind, int Amount);
	bool TryRepair(CPlayer *pPlayer, int ItemId, char *pErr, int ErrSize, char *pErrKey, int KeySize);
};

#endif

#ifndef GAME_SERVER_CORE_TOOLS_EVENT_LISTENER_H
#define GAME_SERVER_CORE_TOOLS_EVENT_LISTENER_H

#include <base/tl/array.h>

class CPlayer;

class IGameEventListener
{
public:
	virtual ~IGameEventListener() = default;

	virtual void OnCharacterDeath(CPlayer *pVictim, CPlayer *pKiller, int Weapon) { (void)pVictim; (void)pKiller; (void)Weapon; }
	virtual void OnCharacterSpawn(CPlayer *pPlayer) { (void)pPlayer; }
	virtual void OnPlayerLogin(CPlayer *pPlayer) { (void)pPlayer; }
	virtual void OnPlayerCraft(CPlayer *pPlayer, int ItemId, int Amount) { (void)pPlayer; (void)ItemId; (void)Amount; }
	virtual void OnPlayerEquip(CPlayer *pPlayer, int ItemId) { (void)pPlayer; (void)ItemId; }
	virtual void OnPlayerGotItem(CPlayer *pPlayer, int ItemId, int Amount) { (void)pPlayer; (void)ItemId; (void)Amount; }
	virtual void OnPlayerKill(CPlayer *pKiller, int ZombId) { (void)pKiller; (void)ZombId; }
	virtual void OnPlayerMine(CPlayer *pPlayer, int MatId) { (void)pPlayer; (void)MatId; }
	virtual void OnWaveComplete(int Wave) { (void)Wave; }
};

class CEventListenerHub
{
	array<IGameEventListener *> m_apListeners;

public:
	void Register(IGameEventListener *pListener);
	void Unregister(IGameEventListener *pListener);

	void EmitCharacterDeath(CPlayer *pVictim, CPlayer *pKiller, int Weapon);
	void EmitCharacterSpawn(CPlayer *pPlayer);
	void EmitPlayerLogin(CPlayer *pPlayer);
	void EmitPlayerCraft(CPlayer *pPlayer, int ItemId, int Amount);
	void EmitPlayerEquip(CPlayer *pPlayer, int ItemId);
	void EmitPlayerGotItem(CPlayer *pPlayer, int ItemId, int Amount);
	void EmitPlayerKill(CPlayer *pKiller, int ZombId);
	void EmitPlayerMine(CPlayer *pPlayer, int MatId);
	void EmitWaveComplete(int Wave);
};

#endif

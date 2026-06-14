#include "event_listener.h"

void CEventListenerHub::Register(IGameEventListener *pListener)
{
	if(!pListener)
		return;
	for(int i = 0; i < m_apListeners.size(); i++)
		if(m_apListeners[i] == pListener)
			return;
	m_apListeners.add(pListener);
}

void CEventListenerHub::Unregister(IGameEventListener *pListener)
{
	m_apListeners.remove_fast(pListener);
}

void CEventListenerHub::EmitCharacterDeath(CPlayer *pVictim, CPlayer *pKiller, int Weapon)
{
	for(int i = 0; i < m_apListeners.size(); i++)
		m_apListeners[i]->OnCharacterDeath(pVictim, pKiller, Weapon);
}

void CEventListenerHub::EmitCharacterSpawn(CPlayer *pPlayer)
{
	for(int i = 0; i < m_apListeners.size(); i++)
		m_apListeners[i]->OnCharacterSpawn(pPlayer);
}

void CEventListenerHub::EmitPlayerLogin(CPlayer *pPlayer)
{
	for(int i = 0; i < m_apListeners.size(); i++)
		m_apListeners[i]->OnPlayerLogin(pPlayer);
}

void CEventListenerHub::EmitPlayerCraft(CPlayer *pPlayer, int ItemId, int Amount)
{
	for(int i = 0; i < m_apListeners.size(); i++)
		m_apListeners[i]->OnPlayerCraft(pPlayer, ItemId, Amount);
}

void CEventListenerHub::EmitPlayerEquip(CPlayer *pPlayer, int ItemId)
{
	for(int i = 0; i < m_apListeners.size(); i++)
		m_apListeners[i]->OnPlayerEquip(pPlayer, ItemId);
}

void CEventListenerHub::EmitPlayerGotItem(CPlayer *pPlayer, int ItemId, int Amount)
{
	for(int i = 0; i < m_apListeners.size(); i++)
		m_apListeners[i]->OnPlayerGotItem(pPlayer, ItemId, Amount);
}

void CEventListenerHub::EmitPlayerKill(CPlayer *pKiller, int ZombId)
{
	for(int i = 0; i < m_apListeners.size(); i++)
		m_apListeners[i]->OnPlayerKill(pKiller, ZombId);
}

void CEventListenerHub::EmitPlayerMine(CPlayer *pPlayer, int MatId)
{
	for(int i = 0; i < m_apListeners.size(); i++)
		m_apListeners[i]->OnPlayerMine(pPlayer, MatId);
}

void CEventListenerHub::EmitWaveComplete(int Wave)
{
	for(int i = 0; i < m_apListeners.size(); i++)
		m_apListeners[i]->OnWaveComplete(Wave);
}

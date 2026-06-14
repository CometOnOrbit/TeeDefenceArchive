#ifndef GAME_SERVER_CORE_TWORLD_COMPONENT_H
#define GAME_SERVER_CORE_TWORLD_COMPONENT_H

#include <base/tl/array.h>

class CGameContext;
class CCharacter;
class CPlayer;
class IConsole;
class IEngine;
class IStorage;
class IServer;
class TWorldController;

/** Base component — mirrors kurosio/MRPG MmoComponent lifecycle. */
class TWorldComponent
{
public:
	class CStack
	{
	public:
		~CStack()
		{
			for(int i = 0; i < m_apComponents.size(); i++)
				delete m_apComponents[i];
			m_apComponents.clear();
		}

		void Add(TWorldComponent *pComponent)
		{
			if(pComponent)
				m_apComponents.add(pComponent);
		}

		array<TWorldComponent *> m_apComponents;
	};

	virtual ~TWorldComponent() = default;

protected:
	friend class TWorldController;

	CGameContext *m_GameServer;
	IServer *m_pServer;
	IConsole *m_pConsole;
	IStorage *m_pStorage;
	IEngine *m_pEngine;
	TWorldController *m_Core;

	CGameContext *GS() const { return m_GameServer; }
	IServer *Server() const { return m_pServer; }
	IConsole *Console() const { return m_pConsole; }
	IStorage *Storage() const { return m_pStorage; }
	IEngine *Engine() const { return m_pEngine; }
	TWorldController *Core() const { return m_Core; }

	virtual void OnPreInit() {}
	virtual void OnInitWorld(const char *pWhereLocalWorld) {}
	virtual void OnPostInit() {}
	virtual void OnConsoleInit() {}
	virtual void OnTick() {}
	virtual void OnShutdown() {}
	virtual void OnClientReset(int ClientID) {}
	virtual void OnPlayerLogin(CPlayer *pPlayer) {}
	virtual void OnCharacterSpawn(CPlayer *pPlayer) {}
	virtual bool OnPlayerVoteCommand(CPlayer *pPlayer, const char *pCmd, const char *pArgs) { (void)pPlayer; (void)pCmd; (void)pArgs; return false; }
};

#endif

#include "move_action.h"
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/entities/character.h>

CEntityMoveAction::CEntityMoveAction(CGameContext* pGameServer, vec2 Position, int WorldID, int ClientID, int Cooldown, unsigned TypeFlags)
: CEntity(&pGameServer->m_World, 0, 0, Position),
m_Position(Position),
m_ClientID(ClientID),
m_WorldID(WorldID),
m_Cooldown(Cooldown),
m_TypeFlags(TypeFlags)
{
m_SpawnTick = Server()->Tick();
GameWorld()->InsertEntity(this);
}

void CEntityMoveAction::Reset()
{
}

void CEntityMoveAction::Tick()
{
if(Server()->Tick() - m_SpawnTick < m_Cooldown)
return;

CPlayer* pPlayer = GameServer()->m_apPlayers[m_ClientID];
if(!pPlayer || !pPlayer->GetCharacter())
return;

if(distance(pPlayer->GetCharacter()->GetPos(), m_Position) <= 64.0f)
{
m_SpawnTick = Server()->Tick() + m_Cooldown;
}
}

void CEntityMoveAction::Snap(int SnappingClient)
{
if(SnappingClient != m_ClientID)
return;

CPlayer* pPlayer = GameServer()->m_apPlayers[m_ClientID];
if(!pPlayer || !pPlayer->GetCharacter())
return;

CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, GetID(), sizeof(CNetObj_Pickup)));
if(!pP)
return;

pP->m_X = (int)m_Position.x;
pP->m_Y = (int)m_Position.y;
pP->m_Type = PICKUP_HEALTH;
}

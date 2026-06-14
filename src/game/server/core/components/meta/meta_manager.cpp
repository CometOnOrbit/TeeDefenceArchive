#include <game/server/account.h>
#include <game/server/core/components/meta/meta_manager.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CMetaManager::CMetaManager()
{
	mem_zero(m_aMeta, sizeof(m_aMeta));
	mem_zero(m_aAchProgress, sizeof(m_aAchProgress));
}

SPlayerMetaData &CMetaManager::Get(int ClientID)
{
	static SPlayerMetaData s_Empty;
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return s_Empty;
	return m_aMeta[ClientID];
}

int CMetaManager::GetAchProgress(int ClientID, int Idx) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || Idx < 0 || Idx >= MAX_META_ACHIEVEMENTS)
		return 0;
	return m_aAchProgress[ClientID][Idx];
}

void CMetaManager::SetAchProgress(int ClientID, int Idx, int Value)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || Idx < 0 || Idx >= MAX_META_ACHIEVEMENTS)
		return;
	m_aAchProgress[ClientID][Idx] = Value;
	m_aMeta[ClientID].m_aAchProgress[Idx] = Value;
}

void CMetaManager::Load(int ClientID, const char *pJson)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	PlayerMeta_Parse(pJson, &m_aMeta[ClientID]);
	for(int i = 0; i < MAX_META_ACHIEVEMENTS; i++)
		m_aAchProgress[ClientID][i] = m_aMeta[ClientID].m_aAchProgress[i];
}

void CMetaManager::Persist(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !GS() || !GS()->Accounts())
		return;
	for(int i = 0; i < MAX_META_ACHIEVEMENTS; i++)
		m_aMeta[ClientID].m_aAchProgress[i] = m_aAchProgress[ClientID][i];
	char aBuf[META_DATA_MAX];
	PlayerMeta_Serialize(&m_aMeta[ClientID], aBuf, sizeof(aBuf));
	GS()->Accounts()->SetMetaData(ClientID, aBuf);
	GS()->Accounts()->RequestSaveMetaData(ClientID);
}

const char *CMetaManager::GetTraitId(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_aMeta[ClientID].m_aTraitId[0])
		return nullptr;
	return m_aMeta[ClientID].m_aTraitId;
}

bool CMetaManager::GetTraitLocked(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;
	return m_aMeta[ClientID].m_TraitLocked;
}

void CMetaManager::SetTrait(int ClientID, const char *pTraitId, bool Locked)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	if(!pTraitId || !pTraitId[0])
	{
		m_aMeta[ClientID].m_aTraitId[0] = 0;
		m_aMeta[ClientID].m_TraitLocked = false;
	}
	else
	{
		str_copy(m_aMeta[ClientID].m_aTraitId, pTraitId, sizeof(m_aMeta[ClientID].m_aTraitId));
		m_aMeta[ClientID].m_TraitLocked = Locked;
	}
	Persist(ClientID);
}

void CMetaManager::OnPlayerLogin(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->IsDummy())
		return;
	char aBuf[META_DATA_MAX];
	if(GS()->Accounts() && GS()->Accounts()->GetMetaData(pPlayer->GetCID(), aBuf, sizeof(aBuf)))
		Load(pPlayer->GetCID(), aBuf);
	else
		Load(pPlayer->GetCID(), nullptr);
}

void CMetaManager::OnClientReset(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	PlayerMeta_Reset(&m_aMeta[ClientID]);
	mem_zero(m_aAchProgress[ClientID], sizeof(m_aAchProgress[ClientID]));
}

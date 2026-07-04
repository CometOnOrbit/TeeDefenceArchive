#include "guild_data.h"

bool SGuildData::IsMember(int ClientID) const
{
	for(int i = 0; i < m_Members.size(); i++)
		if(m_Members[i].m_ClientID == ClientID)
			return true;
	return false;
}

EGuildRank SGuildData::GetRank(int ClientID) const
{
	for(int i = 0; i < m_Members.size(); i++)
		if(m_Members[i].m_ClientID == ClientID)
			return m_Members[i].m_Rank;
	return GUILDRANK_APPLICANT;
}

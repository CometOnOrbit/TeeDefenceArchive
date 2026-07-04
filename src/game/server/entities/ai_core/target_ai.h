#ifndef GAME_SERVER_ENTITIES_AI_CORE_TARGET_AI_H
#define GAME_SERVER_ENTITIES_AI_CORE_TARGET_AI_H

#include <base/system.h>
#include <engine/shared/protocol.h>

class CCharacterBotAI;

enum class ETargetType
{
	Empty,
	Active,
	Lost
};

class CTargetAI
{
	int m_TargetID { -1 };
	ETargetType m_Type { ETargetType::Empty };
	bool m_IsCollided {};
	int m_Aggression {};
	CCharacterBotAI *m_pCharacter {};

public:
	void Init(CCharacterBotAI *pCharacter) { m_pCharacter = pCharacter; }

	void Reset()
	{
		m_TargetID = -1;
		m_Aggression = 0;
		m_IsCollided = false;
		m_Type = ETargetType::Empty;
	}

	void Tick()
	{
		if(m_Type == ETargetType::Lost && m_Aggression)
		{
			m_Aggression--;
			if(!m_Aggression)
				Reset();
		}
	}

	ETargetType GetType() const { return m_Type; }
	int GetCID() const { return m_TargetID; }
	int GetAggression() const { return m_Aggression; }

	void Set(int ClientID, int Aggression)
	{
		if(ClientID >= 0 && ClientID < MAX_CLIENTS)
		{
			m_TargetID = ClientID;
			m_Aggression = Aggression;
			m_Type = ETargetType::Active;
		}
	}

	bool SetType(ETargetType TargetType)
	{
		if(m_Type != TargetType)
		{
			m_Type = TargetType;
			return true;
		}
		return false;
	}

	bool IsEmpty() const { return m_TargetID <= -1; }
	bool IsCollided() const { return m_IsCollided; }
	void UpdateCollided(bool Collided) { m_IsCollided = Collided; }
};

#endif

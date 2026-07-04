#ifndef GAME_SERVER_CORE_TOOLS_COOLDOWN_H
#define GAME_SERVER_CORE_TOOLS_COOLDOWN_H

class IServer;

class CCooldown
{
	IServer *m_pServer;
	int m_StartTick;
	int m_DurationTicks;

public:
	CCooldown(IServer *pServer) : m_pServer(pServer), m_StartTick(0), m_DurationTicks(0) {}

	void Start(int DurationTicks)
	{
		m_StartTick = m_pServer->Tick();
		m_DurationTicks = DurationTicks;
	}

	void Reset()
	{
		m_StartTick = 0;
		m_DurationTicks = 0;
	}

	bool IsReady() const
	{
		return m_StartTick == 0 || (m_pServer->Tick() - m_StartTick) >= m_DurationTicks;
	}

	int RemainingTicks() const
	{
		if(IsReady()) return 0;
		return m_DurationTicks - (m_pServer->Tick() - m_StartTick);
	}

	float Progress() const
	{
		if(m_DurationTicks <= 0) return 1.0f;
		return 1.0f - (float)RemainingTicks() / (float)m_DurationTicks;
	}

	bool IsRunning() const { return m_StartTick > 0 && !IsReady(); }
};

#endif

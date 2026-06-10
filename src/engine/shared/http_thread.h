#ifndef ENGINE_SHARED_HTTP_THREAD_H
#define ENGINE_SHARED_HTTP_THREAD_H

#include <base/system.h>
#include <base/tl/array.h>

class CHttpRequest;

class CHttpThread
{
	void *m_pThread;
	LOCK m_Lock;
	SEMAPHORE m_Wake;
	bool m_Running;
	bool m_ShutdownRequested;

	void *m_pMulti;

	struct SActive
	{
		CHttpRequest *m_pRequest;
		void *m_pEasy;
	};

	array<CHttpRequest *> m_aQueue;
	array<SActive> m_aActive;

	static void ThreadFunc(void *pUser);
	void RunLoop();

public:
	CHttpThread();
	~CHttpThread();

	bool Init();
	void Shutdown();
	bool IsRunning() const { return m_Running; }

	void RunBlocking(CHttpRequest *pRequest);
};

void HttpThreadInit();
void HttpThreadShutdown();
CHttpThread *HttpThread();

#endif

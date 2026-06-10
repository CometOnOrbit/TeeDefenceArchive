#include "http_thread.h"
#include "http_request.h"

#include <curl/curl.h>

static CHttpThread *s_pHttpThread = nullptr;

CHttpThread::CHttpThread()
{
	m_pThread = nullptr;
	m_Lock = lock_create();
	sphore_init(&m_Wake);
	m_Running = false;
	m_ShutdownRequested = false;
	m_pMulti = nullptr;
}

CHttpThread::~CHttpThread()
{
	Shutdown();
	sphore_destroy(&m_Wake);
	lock_destroy(m_Lock);
}

bool CHttpThread::Init()
{
	if(m_pThread)
		return m_Running;

	m_ShutdownRequested = false;
	m_pThread = thread_init(ThreadFunc, this);
	if(!m_pThread)
		return false;

	for(int i = 0; i < 500; i++)
	{
		if(m_Running)
			return true;
		thread_sleep(1);
	}
	return m_Running;
}

void CHttpThread::Shutdown()
{
	if(!m_pThread)
		return;

	lock_wait(m_Lock);
	m_ShutdownRequested = true;
	lock_unlock(m_Lock);
	sphore_signal(&m_Wake);

	thread_wait(m_pThread);
	m_pThread = nullptr;
	m_Running = false;
}

void CHttpThread::ThreadFunc(void *pUser)
{
	static_cast<CHttpThread *>(pUser)->RunLoop();
}

void CHttpThread::RunLoop()
{
	m_pMulti = curl_multi_init();
	if(!m_pMulti)
	{
		dbg_msg("http", "curl_multi_init failed");
		return;
	}

	m_Running = true;

	while(true)
	{
		if(m_ShutdownRequested && m_aQueue.size() == 0 && m_aActive.size() == 0)
			break;

		array<CHttpRequest *> aNewQueue;
		lock_wait(m_Lock);
		for(int q = 0; q < m_aQueue.size(); q++)
			aNewQueue.add(m_aQueue[q]);
		m_aQueue.clear();
		lock_unlock(m_Lock);

		for(int i = 0; i < aNewQueue.size(); i++)
		{
			CHttpRequest *pReq = aNewQueue[i];
			CURL *pEasy = curl_easy_init();
			if(!pEasy)
			{
				pReq->CompleteRequest(CURLE_FAILED_INIT);
				continue;
			}
			if(!pReq->ConfigureEasy(pEasy))
			{
				curl_easy_cleanup(pEasy);
				pReq->CompleteRequest(CURLE_ABORTED_BY_CALLBACK);
				continue;
			}
			curl_multi_add_handle((CURLM *)m_pMulti, pEasy);
			SActive Active;
			Active.m_pRequest = pReq;
			Active.m_pEasy = pEasy;
			m_aActive.add(Active);
		}

		int StillRunning = 0;
		curl_multi_poll((CURLM *)m_pMulti, nullptr, 0, 100, nullptr);
		curl_multi_perform((CURLM *)m_pMulti, &StillRunning);

		int MsgCount = 0;
		CURLMsg *pMsg;
		while((pMsg = curl_multi_info_read((CURLM *)m_pMulti, &MsgCount)))
		{
			if(pMsg->msg != CURLMSG_DONE)
				continue;

			CHttpRequest *pReq = nullptr;
			for(int i = 0; i < m_aActive.size(); i++)
			{
				if(m_aActive[i].m_pEasy == pMsg->easy_handle)
				{
					pReq = m_aActive[i].m_pRequest;
					m_aActive.remove_index(i);
					break;
				}
			}

			if(pReq)
				pReq->CompleteRequest(pMsg->data.result, pMsg->easy_handle);

			curl_multi_remove_handle((CURLM *)m_pMulti, pMsg->easy_handle);
			curl_easy_cleanup(pMsg->easy_handle);
		}
	}

	for(int i = 0; i < m_aActive.size(); i++)
	{
		curl_multi_remove_handle((CURLM *)m_pMulti, (CURL *)m_aActive[i].m_pEasy);
		curl_easy_cleanup((CURL *)m_aActive[i].m_pEasy);
	}
	m_aActive.clear();

	curl_multi_cleanup((CURLM *)m_pMulti);
	m_pMulti = nullptr;
	m_Running = false;
}

void CHttpThread::RunBlocking(CHttpRequest *pRequest)
{
	if(!pRequest || !m_Running)
		return;

	pRequest->PrepareBlocking();

	lock_wait(m_Lock);
	m_aQueue.add(pRequest);
	lock_unlock(m_Lock);
	sphore_signal(&m_Wake);

	pRequest->WaitBlocking();
}

void HttpThreadInit()
{
	if(s_pHttpThread)
		return;
	s_pHttpThread = new CHttpThread();
	if(!s_pHttpThread->Init())
	{
		delete s_pHttpThread;
		s_pHttpThread = nullptr;
		dbg_msg("http", "HTTP thread init failed");
	}
}

void HttpThreadShutdown()
{
	if(!s_pHttpThread)
		return;
	s_pHttpThread->Shutdown();
	delete s_pHttpThread;
	s_pHttpThread = nullptr;
}

CHttpThread *HttpThread()
{
	return s_pHttpThread;
}

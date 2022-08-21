///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"

#include "../include/control-thread.h"
LOCAL_LOGGER_ENABLE(L"f2fs.thread", LOGGER_LEVEL_DEBUGINFO);


CControlThread::CControlThread(void)
	: m_thread(NULL), m_running(0), m_thread_id(0)
{
	m_que_event = CreateEvent(NULL, FALSE, FALSE, NULL);
}

CControlThread::~CControlThread(void)
{
	if (m_thread) CloseHandle(m_thread);
	if (m_que_event) CloseHandle(m_que_event);
}

bool CControlThread::Start(int priority)
{
	LPVOID data = (LPVOID)(static_cast<CControlThread*>(this));
	m_thread = CreateThread(NULL, 0, InternalRun, data, 0, &m_thread_id);
	if (m_thread == NULL || m_thread == INVALID_HANDLE_VALUE)
	{
		THROW_WIN32_ERROR(L"failed on creating flush control thread");
	}
	LOG_DEBUG(L"control thread stared, this=%p, data=%llX, thread id = %d", this, (UINT64)(data), m_thread_id);
	while (1)
	{	// 确保control线程已经开始运行
		Sleep(1);
		if (InterlockedAdd(&m_running,0)) break;
	}
	SetThreadPriority(m_thread, priority);
	return true;
}

void CControlThread::Stop(void)
{
	LOG_STACK_TRACE();
	if (!IsRunning()) return;
	InterlockedExchange(&m_running, 0);
	LOG_DEBUG(L"stop thread, m_running=%d", m_running);
	SetEvent(m_que_event);
	DWORD ir = WaitForSingleObject(m_thread, 1000);
	if (ir == WAIT_TIMEOUT)	
	{
		LOG_ERROR(L"[err] wait thread stopping time out");	
		TerminateThread(m_thread, -1);
		WaitForSingleObject(m_thread, 1000);
	}
	if (ir != 0 && ir != WAIT_TIMEOUT)	THROW_WIN32_ERROR(L"failed on stopping flush control thread");
	CloseHandle(m_thread);
	m_thread = NULL;
}

DWORD __stdcall CControlThread::InternalRun(LPVOID data)
{
	CControlThread* control = reinterpret_cast<CControlThread*>(data);
	LOG_DEBUG(L"start control thread: %s, control=%p, data=%llX", control->m_thread_name.c_str(), control, (UINT64)data);
	JCASSERT(control);
	InterlockedExchange(&control->m_running, 1);
	return control->Run();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once


class CControlThread
{
public:
	CControlThread(void);
	virtual ~CControlThread(void);
public:
	bool Start(int priority = THREAD_PRIORITY_NORMAL);
	void Stop(void);
	bool IsRunning(void) { return (m_thread /*&& m_running*/); }
	bool kthread_should_stop(void) { return (m_thread && !m_running); }
	const HANDLE GetHandle(void) const { return m_thread; }
//	void WakeUp(void) { SetEvent(m_que_event); }
	DWORD GetThreadId(void) const { return m_thread_id; }

protected:
	static DWORD WINAPI InternalRun(LPVOID data);
	virtual DWORD Run(void) =0;

protected:
	HANDLE	m_thread;
	HANDLE	m_que_event;
	DWORD	m_thread_id;
	long	m_running;		// ���߳�֪ͨ�����߳��Ƿ�Ҫ����������
	long	m_started;		// �����߳�ָʾ�Ƿ��Ѿ���ʼ������
#ifdef _DEBUG
	std::wstring m_thread_name;
#endif
};
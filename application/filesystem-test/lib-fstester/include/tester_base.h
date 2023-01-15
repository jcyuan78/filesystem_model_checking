///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once
#include <dokanfs-lib.h>
#include <boost/property_tree/json_parser.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

//enum class OP_ID
//{						// action,		P1,				P2,	P3, P4
//	OP_NONE = 0,
//	OP_NO_EFFECT,		//���ڲ�����Ӱ��Ĳ����������лع�
//	CREATE_FILE,		//�����ļ�,		���ļ���,		na
//	CREATE_DIR,			//����Ŀ¼,		��Ŀ¼��,		na
//	DELETE_FILE,		//
//	DELETE_DIR,
//	MOVE,				//�ƶ������,	Ŀ��·�����ļ���,	na
//	APPEND_FILE,
//	OVER_WRITE,			//д�ļ�,		na,				na,	ƫ����,	����
//	DEMOUNT_MOUNT,
//	POWER_OFF_RECOVERY,
//};

enum ERROR_CODE
{
	ERR_OK = 0,
	OK_ALREADY_EXIST,	// �ļ�����Ŀ¼�Ѿ����ڣ����ǽ������true
	ERR_CREATE_EXIST,	// �����Ѿ����ڵ��ļ����ظ�������
	ERR_CREATE,			// �ļ�����Ŀ¼�����ڣ����Ǵ���ʧ��
	ERR_OPEN_FILE,		// ��ͼ��һ���Ѿ����ڵ��ļ��ǳ���
	ERR_GET_INFOMATION,	// ��ȡFile Informaitonʱ����
	ERR_DELETE_FILE,	// ɾ���ļ�ʱ����
	ERR_DELETE_DIR,		// ɾ��Ŀ¼ʱ����
	ERR_READ_FILE,		// ���ļ�ʱ����
	ERR_WRONG_FILE_SIZE,	// 
	ERR_WRONG_FILE_DATA,
};

enum OP_CODE
{
	OP_NOP, OP_NO_EFFECT,
	OP_THREAD_CREATE, OP_THREAD_EXIT,
	OP_FILE_CREATE, OP_FILE_CLOSE, OP_FILE_READ, OP_FILE_WRITE, OP_FILE_FLUSH,
	OP_FILE_OPEN, OP_FILE_DELETE, OP_FILE_OVERWRITE,
	OP_DIR_CREATE, OP_DIR_DELETE, OP_MOVE, 
	OP_DEMOUNT_MOUNT, OP_POWER_OFF_RECOVER,
};

struct FILE_FILL_DATA
{
	UINT64 offset;
	DWORD fid;
	WORD rev;
	BYTE dummy;
	BYTE checksum;
};

#define FILE_BUF_SIZE	(256*1024)

class TRACE_ENTRY
{
public:
	UINT64 ts;
	OP_CODE op_code = OP_CODE::OP_NOP;
	DWORD thread_id;
	DWORD fid;
	std::wstring file_path;
	UINT64 duration = 0;	// �������õ�ʱ��
	UINT op_sn;
	union {
		struct {	// for thread
			DWORD new_thread_id;
		};
		struct {	// for create, open, delete
			//OP_CODE mode;
			bool is_dir;
			bool is_async;
		};
		struct {	// for read write
			size_t offset;
			size_t length;
		};
		//struct {	// for move
		//	std::wstring target_path;
		//};
	};
};


class CTesterBase
{
public:
	CTesterBase(IFileSystem * fs, IVirtualDisk *disk);
	virtual ~CTesterBase(void);
public:
	virtual void Config(const boost::property_tree::wptree& pt, const std::wstring& root);
	int StartTest(void);
public:
	static int CompareData(const char* src, const char* tar, size_t size);
	static void FillFile(IFileInfo* file, DWORD fid, DWORD revision, size_t start, size_t len);
	static void FillBuffer(void* buf, DWORD fid, DWORD revision, size_t offset, size_t len);
	void FileVerify(const std::wstring & fn, DWORD fid, DWORD revision, size_t len);

protected:
	virtual int PrepareTest(void) = 0;
	virtual int RunTest(void) = 0;
	virtual int FinishTest(void) = 0;
	virtual void ShowTestFailure(FILE* log) = 0;

protected:
	void SetLogFile(const std::wstring& log_fn);



protected:
	bool PrintProgress(INT64 ts);
	DWORD Monitor(void);
	static DWORD WINAPI _Monitor(PVOID p)
	{
		CTesterBase* tester = (CTesterBase*)p;
		return tester->Monitor();
	}


	std::wstring m_root;	// ���Եĸ�Ŀ¼�����в����ٴ�Ŀ¼�½���
	boost::posix_time::ptime m_ts_start;

	// ���ڼ���ļ������Ƿ�ʱ
	long m_running;
	DWORD m_timeout;
	DWORD m_message_interval = 30;
	HANDLE m_monitor_thread;
	HANDLE m_monitor_event;
	UINT m_op_sn;

	HANDLE m_fsinfo_file = nullptr;		// �ļ�ϵͳ�������ļ������ڶ�ȡfile system��һЩ״̬

	// log support
	FILE* m_log_file = nullptr;
	wchar_t m_log_buf[1024];

	IFileSystem* m_fs = nullptr;
	IVirtualDisk* m_disk = nullptr;

	// configurations
protected:
	bool m_mount, m_format;		// �ڲ���ǰ�Ƿ�Ҫformat��mount fs
	size_t m_volume_size;		
};

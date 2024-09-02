///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <stdext.h>
#include <map>

typedef UINT FSIZE;


#define	MAX_DEPTH		(500)
#define MAX_PATH_SIZE	(63)
#define MAX_FILE_NUM	(256)
#define MAX_CHILD_NUM	(64)

#define MAX_ENCODE_SIZE (MAX_FILE_NUM *2)

enum OP_CODE
{
	OP_NOP, OP_NO_EFFECT,
	OP_THREAD_CREATE, OP_THREAD_EXIT,
	OP_FILE_CREATE, OP_FILE_CLOSE, OP_FILE_READ, OP_FILE_WRITE, OP_FILE_FLUSH,
	OP_FILE_OPEN, OP_FILE_DELETE, OP_FILE_OVERWRITE,
	OP_DIR_CREATE, OP_DIR_DELETE, OP_MOVE,
	OP_DEMOUNT_MOUNT, OP_POWER_OFF_RECOVER,
	OP_FILE_VERIFY,
	OP_MARK_TRACE_BEGIN,
};

class TRACE_ENTRY
{
public:
	UINT64 ts;
	OP_CODE op_code = OP_CODE::OP_NOP;
	DWORD thread_id;
	std::string file_path;
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
			FSIZE offset;
			FSIZE length;
		};
		struct {
			WORD trace_id;
			WORD test_cycle;
		};
	};
};

OP_CODE StringToOpId(const std::wstring& str);


class CReferenceFs
{
public:
	class CRefFile
	{
	protected:
		FSIZE size;				// �����ļ���size��ʾ�ļ���С������Ŀ¼��size��ʾ���������
		UINT checksum;			// �����ļ����ļ���checksum������Ŀ¼��-1; ���ļ���Чʱ��checksum��Ϊfree����ָ��ʹ�ã�
		UINT fid;

		char m_fn[MAX_PATH_SIZE+1];
		// ����ͬ������
		char m_encode[MAX_ENCODE_SIZE];
		int m_encode_size;
		// �������ṹ��������ṹ��Ҫ���ƣ�����ֻ��ʹ��index����ָ��
		UINT m_children[MAX_CHILD_NUM];
		UINT m_parent;		// ���ڵ�id;
		UINT m_depth;		// Ŀ¼���
		int m_write_count;	// �����ļ���д��������汾��

		friend class CReferenceFs;
	public:
		void InitFile(UINT parent_id, CRefFile * parent, const std::string& path, bool isdir, UINT fid);
		void RemoveChild(UINT fid);
		void UpdateEncode(CRefFile* files);
		static int CompareEncode(void*, const void* e1, const void* e2);
		void GetEncodeString(std::string& str) const { str = m_encode; }
		inline bool isdir(void) const { return checksum == (UINT)(-1); }
		inline int depth(void) const { return m_depth; }
		inline int write_count(void) const { return m_write_count; }
		inline UINT child_num(void) const { return size; }
		inline UINT get_fid(void) const { return fid; }
	};
public:
	typedef std::map<std::string, UINT>::iterator ITERATOR;
	typedef std::map<std::string, UINT>::const_iterator CONST_ITERATOR;

public:
	void Initialize(const std::string & root_path = "\\");
	void CopyFrom(const CReferenceFs & src);
	bool IsExist(const std::string & path);		// �ж�·���Ƿ����
	bool AddPath(const std::string & path, bool dir, UINT fid);		// ���һ��·��
	void GetFileInfo(const CRefFile & file, DWORD & checksum, FSIZE &len) const;
	void GetFilePath(const CRefFile& file, std::string & path) const;
	void UpdateFile(const std::string & path, DWORD checksum, size_t len);
	void UpdateFile(CRefFile & file, DWORD checksum, size_t len);
	int GetDirDepth(const CRefFile& dir) const {
		return dir.m_depth;
	}
	size_t GetFileNumber(void) const {
		return (MAX_FILE_NUM - m_free_num);
	}
	//void AddRoot(void);

	inline bool IsDir(const CRefFile& file) const { return file.isdir(); }
	CRefFile * FindFile(const std::string & path);
	void RemoveFile(const std::string & path);

	// ��������ͬ�����룬�����ж�����ͬ����
	template <size_t S>
	int Encode(DWORD(&code)[S]) const
	{
		const char* encode = m_files[0].m_encode;
		int len = m_files[0].m_encode_size;
		memset(code, 0, sizeof(code));

#if 1
		memcpy_s(code, sizeof(DWORD) * S, encode, len);

#else
		DWORD* ptr = code;
		size_t dst_len = 0;
		DWORD mask = 1;
		for (int ii = 0; ii < len; ++ii)
		{
			if (encode[ii] == '1') (*ptr) |= mask;
			if (mask == 0x80000000)
			{
				mask = 1;
				ptr++;
				dst_len++;
				if (dst_len >= S) THROW_ERROR(ERR_APP, L"[err] buffer is too short, (%d)", S);
			}
			mask <<= 1;
		}
#endif
		return len;
	}

//	void Encode(DWORD* code, size_t buf_len) const;
	void GetEncodeString(std::string& str) const;

protected:
	UINT FindFileIndex(const std::string & path);

public:
	CONST_ITERATOR Begin() const { return m_ref.begin(); }
	CONST_ITERATOR End() const { return m_ref.end(); }
	const CRefFile & GetFile(CONST_ITERATOR & it) const;
	UINT m_file_num=0, m_dir_num=0; // �ļ�������Ŀ¼����

protected:
	// �����ļ���UINT64���length | checksum, ����dir�������Ŀ¼����
	//	map�д淽ref file��index
	std::map<std::string, UINT> m_ref;
	CRefFile m_files[MAX_FILE_NUM];
	// ����m_files������һ��free list��free list��checksumָ������һ������m_free_listָ����ͷ����
	UINT m_free_list;
	size_t m_free_num;
};

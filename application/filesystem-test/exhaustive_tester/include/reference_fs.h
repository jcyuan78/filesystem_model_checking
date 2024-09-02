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
	UINT64 duration = 0;	// 操作所用的时间
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
		FSIZE size;				// 对于文件，size表示文件大小，对于目录，size表示子项的数量
		UINT checksum;			// 对于文件，文件的checksum；对于目录，-1; 等文件无效时，checksum作为free链表指针使用；
		UINT fid;

		char m_fn[MAX_PATH_SIZE+1];
		// 树的同构编码
		char m_encode[MAX_ENCODE_SIZE];
		int m_encode_size;
		// 构造树结构。这个树结构需要复制，这里只能使用index代替指针
		UINT m_children[MAX_CHILD_NUM];
		UINT m_parent;		// 父节点id;
		UINT m_depth;		// 目录深度
		int m_write_count;	// 对于文件，写入次数，版本号

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
	bool IsExist(const std::string & path);		// 判断路径是否存在
	bool AddPath(const std::string & path, bool dir, UINT fid);		// 添加一个路径
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

	// 对树进行同构编码，用于判断树的同构性
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
	UINT m_file_num=0, m_dir_num=0; // 文件数量和目录数量

protected:
	// 对于文件，UINT64存放length | checksum, 对于dir，存放子目录数量
	//	map中存方ref file的index
	std::map<std::string, UINT> m_ref;
	CRefFile m_files[MAX_FILE_NUM];
	// 利用m_files本身构建一个free list。free list的checksum指向其下一个对象。m_free_list指向其头部。
	UINT m_free_list;
	size_t m_free_num;
};

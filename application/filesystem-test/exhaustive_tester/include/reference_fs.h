///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <stdext.h>
//#include <map>

typedef UINT FSIZE;
typedef WORD _NID;


#define	MAX_DEPTH		(500)
#define MAX_PATH_SIZE	(255)
#define MAX_FILE_NUM	(256)
#define MAX_CHILD_NUM	(64)

#define MAX_ENCODE_SIZE (MAX_FILE_NUM *2)

//#define INVALID_BLK		(0xFFFFFFFF)

//#undef _NID

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
	OP_CODE op_code = OP_CODE::OP_NOP;
	std::string file_path;
	_NID fid;				// 对于已经打开的文件，传递文件号
	UINT op_sn;
	union {
		struct {
			UINT rollback;		// 用于power outage测试，
		};
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
//		UINT checksum;			// 对于文件，文件的checksum；对于目录，-1; 等文件无效时，checksum作为free链表指针使用；
		UINT next;				// 用于文件列表，使用列表或者free列表

		char m_fn[MAX_PATH_SIZE+1];
		// 树的同构编码
		char m_encode[MAX_ENCODE_SIZE];
		int m_encode_size;
		// 构造树结构。这个树结构需要复制，这里只能使用index代替指针
		_NID m_children[MAX_CHILD_NUM];
		UINT m_parent;		// 父节点id;
		UINT m_depth;		// 目录深度
		int m_write_count;	// 对于文件，写入次数，版本号
		_NID fid;
		bool m_is_open;		// 这个文件是否被打开
		bool m_is_dir;

		friend class CReferenceFs;

	public:
		void InitFile(UINT parent_id, CRefFile * parent, const std::string& path, bool isdir, _NID fid);
		void RemoveChild(_NID fid);
		void UpdateEncode(CRefFile* files);
		static int CompareEncode(void*, const void* e1, const void* e2);
//		void GetEncodeString(std::string& str) const { str = m_encode; }
		void GetEncodeString(char* str, size_t len) const {
			memcpy_s(str, len, m_encode, m_encode_size);
			str[m_encode_size] = 0;
		}
		inline bool isdir(void) const { return m_is_dir; }
		inline int depth(void) const { return m_depth; }
		inline int write_count(void) const { return m_write_count; }
		inline UINT child_num(void) const { return size; }
		inline _NID get_fid(void) const { return fid; }
		inline bool is_open(void) const { return m_is_open; }
	};
public:
//	typedef std::map<std::string, UINT>::iterator ITERATOR;
//	typedef std::map<std::string, UINT>::const_iterator CONST_ITERATOR;

public:
	void Initialize(const std::string & root_path = "\\");
	void CopyFrom(const CReferenceFs & src);
	bool IsExist(const std::string & path);		// 判断路径是否存在
	CRefFile * AddPath(const std::string & path, bool dir, _NID fid);		// 添加一个路径
	void GetFileInfo(const CRefFile & file, DWORD & checksum, FSIZE &len) const;
	void GetFilePath(const CRefFile& file, std::string & path) const;
	void UpdateFile(const std::string & path, DWORD checksum, size_t len);
	void UpdateFile(CRefFile & file, DWORD checksum, size_t len);
	int GetDirDepth(const CRefFile& dir) const {
		return dir.m_depth;
	}
	size_t GetFileNumber(void) const {
//		JCASSERT((m_free_num + m_file_num + m_dir_num) == MAX_FILE_NUM);
		return (MAX_FILE_NUM - m_free_num);
	}
	//void AddRoot(void);
	void OpenFile(CRefFile& file);
	void CloseFile(CRefFile& file);
	UINT OpenedFileNr(void) const { return m_opened; }
	void Demount(void);

	inline bool IsDir(const CRefFile& file) const { return file.isdir(); }
	CRefFile * FindFile(const std::string & path);
	void RemoveFile(const std::string & path);

	// 对树进行同构编码，用于判断树的同构性
#if 0
	template <size_t S>
	int Encode(DWORD(&code)[S]) const
	{
		const char* encode = m_files[0].m_encode;
		int len = m_files[0].m_encode_size;
		memset(code, 0, sizeof(code));
		memcpy_s(code, sizeof(DWORD) * S, encode, len);
		return len;
	}
#else
	size_t Encode(char* code, size_t buf_len) const;
#endif

//	void Encode(DWORD* code, size_t buf_len) const;
	//void GetEncodeString(std::string& str) const;

protected:
	UINT FindFileIndex(const std::string & path);

public:
	class CONST_ITERATOR
	{
	public:
		CONST_ITERATOR(UINT init, CRefFile * ff) : index(init), files(ff) {}
		CONST_ITERATOR(const CONST_ITERATOR& ii) :index(ii.index), files(ii.files) {}
		CONST_ITERATOR& operator ++ (void) { index = files[index].next; return (*this); }
		bool operator != (const CONST_ITERATOR& b) { return (index != b.index); }
		friend class CReferenceFs;
	protected:
		UINT index;
		CRefFile* files;
	};
	CReferenceFs::CONST_ITERATOR Begin() { return CONST_ITERATOR(m_used_list, m_files); }
	CReferenceFs::CONST_ITERATOR End() { return CONST_ITERATOR((UINT)-1, m_files); }
	const CRefFile& GetFile(CONST_ITERATOR& it) const { return m_files[it.index]; }
	UINT m_file_num=0, m_dir_num=0; // 文件数量和目录数量

protected:
	UINT get_file(void);
	void put_file(UINT index);
	void debug_out_used_files(void);
	// 对于文件，UINT64存放length | checksum, 对于dir，存放子目录数量
	//	map中存方ref file的index
//	std::map<std::string, UINT> m_ref;
	CRefFile m_files[MAX_FILE_NUM];
	// 利用m_files本身构建一个free list。free list的checksum指向其下一个对象。m_free_list指向其头部。
	UINT m_free_list;
	UINT m_used_list;
	UINT m_free_num;
	UINT m_opened =0;		// 打开的文件数量
	UINT m_reset_count = 0;	// 重置次数
};

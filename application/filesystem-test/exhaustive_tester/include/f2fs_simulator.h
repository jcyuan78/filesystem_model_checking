///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "fs_simulator.h"

#include "f2fs_segment.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == ��������  ==
#define MAX_INDEX_LEVEL			(3)
#define INDEX_SIZE				(128)
#define MAX_TABLE_SIZE			(128)

#define NR_DENTRY_IN_BLOCK		(4)
#define FN_SLOT_LEN				(4)
#define MAX_DENTRY_LEVEL		(4)

#define ROOT_FID				(0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == inode and index node  ==
// ���ڼ�¼һ��ʵ�ʵ�inode
struct INODE
{
	UINT blk_num;		// �ļ�ռ�õĿ���
	UINT file_size;		// �ļ����ȣ��ֽڵ�λ
	UINT ref_count;		
	bool delete_on_close;
	CPageAllocator::INDEX index_table[MAX_TABLE_SIZE];
};

struct INDEX_NODE
{
	CPageAllocator::INDEX index_table[MAX_TABLE_SIZE];
};

struct NODE_INFO
{
public:
//	enum NODE_TYPE { NODE_FREE, NODE_INODE, NODE_INDEX, } m_type;
	NID m_nid;
	NID m_parent_node;
	UINT valid_data;
	CPageAllocator::INDEX page_id;		// ���node��Ӧ��page��id
	union {
		INODE inode;
		INDEX_NODE index;
	};
};

struct DENTRY
{
	FID ino;
	WORD hash;
	WORD name_len;
	BYTE file_type;
};

struct DENTRY_BLOCK
{
	DWORD bitmap;
	char filenames[NR_DENTRY_IN_BLOCK][FN_SLOT_LEN];
	DENTRY dentries[NR_DENTRY_IN_BLOCK];
};

#define NODE_NEXT_FREE node.m_nid
struct BLOCK_DATA
{
	enum BLOCK_TYPE {BLOCK_FREE, BLOCK_INODE, BLOCK_INDEX, BLOCK_DENTRY} m_type;
	union
	{
		NODE_INFO node;
		DENTRY_BLOCK dentry;
	};
};

// ����Buffer��ģ���ļ�ϵͳ��ҳ���档
template <typename BLOCK_TYPE>
class CBufferManager
{
public:
	typedef UINT _INDEX;
public:
	CBufferManager(void) {}
	// ���� root�� NODE
	BLOCK_TYPE * Init(void) {
		// ����free list
		for (UINT ii = 1; ii < MAX_NODE_NUM; ++ii)
		{
			m_data_buffer[ii].NODE_NEXT_FREE = ii + 1;
			m_data_buffer[ii].m_type = BLOCK_DATA::BLOCK_FREE;
		}
		m_data_buffer[MAX_NODE_NUM - 1].NODE_NEXT_FREE = INVALID_BLK;
		m_data_buffer[0].NODE_NEXT_FREE = 0;	// ������0��node
		m_free_ptr = 1;
		m_used_nr = 1;
		// FID = 0��ʾroot
		return m_data_buffer + 0;
	}
	_INDEX get_block(void)
	{
		if (m_used_nr >= MAX_NODE_NUM) THROW_ERROR(ERR_APP, L"no free block buffer");
		_INDEX index = m_free_ptr;
		m_free_ptr = m_data_buffer[index].NODE_NEXT_FREE;
		m_used_nr++;
		return index;
	}
	void put_block(_INDEX index)
	{
		m_data_buffer[index].m_type = BLOCK_DATA::BLOCK_FREE;
		m_data_buffer[index].NODE_NEXT_FREE = (NID)m_free_ptr;
		m_free_ptr = index;
		m_used_nr--;
	}
public:
	BLOCK_TYPE m_data_buffer[MAX_NODE_NUM];
protected:
	_INDEX m_free_ptr;
	UINT m_used_nr;
};


// �������� inode��index table��ӳ���ϵ
class CIndexPath
{
public:
	CIndexPath(NODE_INFO* inode)
	{
		level = -1;
		memset(offset, 0, sizeof(offset));
		memset(node, 0, sizeof(node));
		memset(page, 0, sizeof(page));
		node[0] = inode;
	}
public:
	int level = -1;
	int offset[MAX_INDEX_LEVEL];
	CPageInfo* page[MAX_INDEX_LEVEL];
	NODE_INFO* node[MAX_INDEX_LEVEL];
};


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == file system  ==


class CF2fsSimulator : public IFsSimulator
{
public:
	CF2fsSimulator(void) {};
	virtual ~CF2fsSimulator(void);

public:
	virtual bool Initialzie(const boost::property_tree::wptree& config, const std::wstring & log_path);
	virtual FID  FileCreate(const std::wstring& fn);
	virtual FID  DirCreate(const std::wstring& fn);

	virtual void SetFileSize(FID fid, FSIZE secs) {}
	virtual void FileWrite(FID fid, FSIZE offset, FSIZE len);
	virtual void FileRead(std::vector<CPageInfo*>& blks, FID fid, FSIZE offset, FSIZE secs);
	// ����truncate�����ļ�
	virtual void FileTruncate(FID fid, FSIZE offset, FSIZE secs);
	virtual void FileDelete(const std::wstring & fn);
	virtual void FileFlush(FID fid);
	virtual void FileClose(FID fid);
	//virtual void FileOpen(FID fid, bool delete_on_close = false);
	virtual FSIZE GetFileSize(FID fid);
	virtual FID  FileOpen(const std::wstring& fn, bool delete_on_close = false);

	virtual bool Mount(void) {	return false;	}
	virtual bool Unmount(void) { return false; }
	virtual bool Reset(void) { return false; }

	virtual void Clone(IFsSimulator*& dst);
	virtual void CopyFrom(const IFsSimulator* src);


	virtual void DumpSegments(const std::wstring& fn, bool sanity_check);
	virtual void DumpSegmentBlocks(const std::wstring& fn);
	virtual void DumpFileMap(FILE* out, FID fid) { DumpFileMap_merge(out, fid); }

	virtual void DumpAllFileMap(const std::wstring& fn);
	virtual void DumpBlockWAF(const std::wstring& fn);

	virtual void GetConfig(boost::property_tree::wptree& config, const std::wstring& config_name);
	// �������֧���ļ���С��block����
	virtual DWORD MaxFileSize(void) const { return MAX_TABLE_SIZE* MAX_TABLE_SIZE-10; }

//	virtual void SetLogFolder(const std::wstring& fn);
	virtual void GetFsInfo(FS_INFO& space_info);
	virtual void GetHealthInfo(FsHealthInfo& info) const {};

#ifdef ENABLE_FS_TRACE
	void fs_trace(const char* op, FID fid, DWORD start_blk, DWORD blk_nr);
#else
	void fs_trace(const char* op, FID fid, DWORD start_blk, DWORD blk_nr) {}
#endif

protected:
	friend class CF2fsSegmentManager;

	NODE_INFO * FileOpenInternal(const char* fn);
	// �ڸ�Ŀ¼�в����ļ�fn, �ҵ�����fid���Ҳ�������invalid_blk
	FID FindFile(NODE_INFO& parent, const char* fn);

	// pages: out ��ȡ����page_id�������������ڴ�
	void FileReadInternal(CPageAllocator::INDEX *pages, NODE_INFO & inode, FSIZE start_blk, FSIZE end_blk);
	void FileWriteInternal(NODE_INFO& inode, FSIZE start_blk, FSIZE end_blk, CPageAllocator::INDEX * pages);

	inline WORD FileNameHash(const char* fn)
	{
		size_t seed = 0;
		const char* p = fn;
		while (*p != 0) {
			boost::hash_combine(seed, *p); p++;
		}
		return ( (WORD)(seed & 0xFFFF) );
	}

	void AddChild(NODE_INFO* parent, const char* fn, FID inode);
	void RemoveChild(NODE_INFO* parent, WORD hash, FID fid);
	FID InternalCreatreFile(const std::wstring& fn, bool is_dir);
	void FileRemove(NODE_INFO * inode);
	// ����block��ʵ���¶ȣ�������Ŀǰʹ�õ��㷨��
	BLK_TEMP GetBlockTemp(CPageInfo* page);
	// ���ݵ�ǰ���㷨����block���¶ȡ�
	BLK_TEMP GetAlgorithmBlockTemp(CPageInfo* page, BLK_TEMP temp);
	// �����file mapʱ�����ϲ�����������block
	void DumpFileMap_no_merge(FILE* out, FID fid);
	bool InvalidBlock(const char* reason, PHY_BLK phy_blk);
	void OffsetToIndex(CIndexPath& path, LBLK_T offset, bool alloc);
	// ��index_path�ƶ�����һ��blockλ�á����ڼ���offset��index��ת�������index block�����仯�������path, ��Ҫ���¼��㡣
	void NextOffset(CIndexPath& path);
	// ���inode�е�����index block��inode block�����û���ڴ����ϣ��Ᵽ��
	void UpdateInode(NODE_INFO& inode, const char* caller = "");

	// �����file mapʱ��������������block
	void DumpFileMap_merge(FILE* out, FID fid);

	void InitInode(BLOCK_DATA* block, NID nid, NID parent, CPageAllocator::INDEX page_id, CPageInfo * page)
	{
		memset(block, 0xFF, sizeof(BLOCK_DATA));
		block->m_type = BLOCK_DATA::BLOCK_INODE;

		NODE_INFO* node = &(block->node);
		node->m_nid = nid;
		node->valid_data = 0;
		node->page_id = page_id;
		node->m_parent_node = parent;

		node->inode.blk_num = 0;
		node->inode.file_size = 0;
		node->inode.ref_count = 0;
		node->inode.delete_on_close = false;

		page->data_index = nid;
		page->inode = nid;
		page->dirty = true;
	}

	void InitIndexNode(BLOCK_DATA* block, NID nid, NID parent, CPageAllocator::INDEX page_id, CPageInfo * page)
	{
		memset(block, 0xFF, sizeof(BLOCK_DATA));
		block->m_type = BLOCK_DATA::BLOCK_INDEX;

		NODE_INFO* node = &(block->node);
		node->m_nid = nid;
		node->m_parent_node = parent;
		node->valid_data = 0;
		node->page_id = page_id;

		page->data_index = nid;
		page->inode = parent;
		page->dirty = true;
	}

	void InitDentry(BLOCK_DATA* block)
	{
		memset(block, 0xFF, sizeof(BLOCK_DATA));
		block->m_type = BLOCK_DATA::BLOCK_DENTRY;
		DENTRY_BLOCK& entries = block->dentry;
		entries.bitmap = 0;

	}

	BLOCK_DATA* AllocateDentryBlock(CPageAllocator::INDEX & page_id);

	inline NODE_INFO& get_inode(FID fid)
	{
		if (fid >= MAX_NODE_NUM) THROW_ERROR(ERR_APP, L"invalid fid number: %d", fid);
		BLOCK_DATA & block = m_block_buf.m_data_buffer[fid];
		if (block.m_type != BLOCK_DATA::BLOCK_INDEX && block.m_type != BLOCK_DATA::BLOCK_INODE) {
			THROW_ERROR(ERR_APP, L"block[%d] is not a node block", fid);
		}
		return block.node;
	}

	inline BLOCK_DATA& get_block(FID fid)
	{
		if (fid >= MAX_NODE_NUM) THROW_ERROR(ERR_APP, L"invalid fid number: %d", fid);
		return m_block_buf.m_data_buffer[fid];
	}

	// �ļ�ϵͳ״̬������Cloneʱ����Ҫ���Ƶı�����
protected:
	// ģ�����layout
	CF2fsSegmentManager m_segments;
	FsHealthInfo m_health_info;

protected:
	// �ļ�ϵͳ����
	CPageAllocator m_pages;
	typedef CBufferManager<BLOCK_DATA>	BLOCK_BUF_TYPE;
	BLOCK_BUF_TYPE m_block_buf;

protected:
	int m_multihead_cnt = 0;
	UINT m_node_blks = 0;	//ʹ�õ�inode��index node������

	// data for log
//	size_t m_write_count = 0;
	// ����ɾ���ļ���д����ͳ�ơ�

protected:
	// log��ͳ����Ϣ
	std::wstring m_log_path;
#ifdef ENABLE_FS_TRACE
	FILE* m_log_invalid_trace = nullptr;
	FILE* m_log_write_trace = nullptr;		// �ļ���trace_fs.csv
	// ���ڼ�¼�ļ�ϵͳ��ε�д��
	FILE* m_log_fs_trace = nullptr;			// �ļ�: fs_trace.csv;
	FILE* m_gc_trace = nullptr;
	FILE* m_inode_trace = nullptr;
#endif

	//UINT m_file_num = 0;						// �ܵ���Ч�ļ�������
	//int m_free_blks = 0;
};


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "fs_simulator.h"

#include "f2fs_segment.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == ��������  ==
#define MAX_INDEX_LEVEL			(3)
#define INDEX_SIZE				(128)
#define MAX_TABLE_SIZE			(128)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == inode and index node  ==
// ���ڼ�¼һ��ʵ�ʵ�inode
struct INODE
{
//	wchar_t fn[MAX_FILENAME_LEN];	// �ļ���
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

#define NODE_NEXT_FREE m_nid
struct NODE_INFO
{
public:
	enum NODE_TYPE { NODE_FREE, NODE_INODE, NODE_INDEX, } m_type;
	NID m_nid;
	NID m_parent_node;
	UINT valid_data;
	CPageAllocator::INDEX page_id;		// ���node��Ӧ��page��id
	union {
		INODE inode;
		INDEX_NODE index;
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
	void Init(void) {
		// ����free list
		for (UINT ii = 1; ii < MAX_NODE_NUM; ++ii)
		{
			//m_free_blk[ii] = ii;
			m_data_buffer[ii].NODE_NEXT_FREE = ii + 1;
		}
		m_data_buffer[MAX_NODE_NUM - 1].NODE_NEXT_FREE = INVALID_BLK;
		m_data_buffer[0].NODE_NEXT_FREE = 0;	// ������0��node
		m_free_ptr = 1;
		m_used_nr = 1;
//		m_free_head = 1;
	}
	_INDEX get_block(void)
	{
		if (m_used_nr >= MAX_NODE_NUM) THROW_ERROR(ERR_APP, L"no free block buffer");
		_INDEX index = m_free_ptr;
		m_free_ptr = m_data_buffer[index].NODE_NEXT_FREE;
		m_used_nr++;
//		UINT index = m_free_blk[m_free_head++];
		return index;
	}
	void put_block(_INDEX index)
	{
//		if (m_free_head <= 1) THROW_ERROR(ERR_APP, L"free block does not match");
//		m_free_head--;
//		m_free_blk[m_free_head] = index;
		m_data_buffer[index].NODE_NEXT_FREE = (NID)m_free_ptr;
		m_free_ptr = index;
		m_used_nr--;
	}
public:
	BLOCK_TYPE m_data_buffer[MAX_NODE_NUM];
protected:
	//UINT m_free_blk[MAX_NODE_NUM];
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
	virtual void FileWrite(FID fid, FSIZE offset, FSIZE secs);
	virtual void FileRead(std::vector<CPageInfo*>& blks, FID fid, FSIZE offset, FSIZE secs);
	// ����truncate�����ļ�
	virtual void FileTruncate(FID fid, FSIZE offset, FSIZE secs);
	virtual void FileDelete(const std::wstring & fn);
	virtual void FileFlush(FID fid);
	virtual void FileClose(FID fid);
	virtual void FileOpen(FID fid, bool delete_on_close = false);
	virtual FSIZE GetFileSize(FID fid);
	virtual FID  FileOpen(const std::wstring& fn, bool delete_on_close = false);

	virtual bool Mount(void) {	return false;	}
	virtual bool Unmount(void) { return false; }
	virtual bool Reset(void) { return false; }

	virtual void Clone(IFsSimulator*& dst);

	virtual void DumpSegments(const std::wstring& fn, bool sanity_check);
	virtual void DumpSegmentBlocks(const std::wstring& fn);
	virtual void DumpFileMap(FILE* out, FID fid) { DumpFileMap_merge(out, fid); }

	virtual void DumpAllFileMap(const std::wstring& fn);
	virtual void DumpBlockWAF(const std::wstring& fn);

	virtual void GetConfig(boost::property_tree::wptree& config, const std::wstring& config_name);
	virtual DWORD MaxFileSize(void) const { return 10000; }

//	virtual void SetLogFolder(const std::wstring& fn);
	virtual void GetSpaceInfo(FsSpaceInfo& space_info);
	virtual void GetHealthInfo(FsHealthInfo& info) const {};

#ifdef ENABLE_FS_TRACE
	void fs_trace(const char* op, FID fid, DWORD start_blk, DWORD blk_nr);
#else
	void fs_trace(const char* op, FID fid, DWORD start_blk, DWORD blk_nr) {}
#endif

protected:
	friend class CF2fsSegmentManager;

	void FileRemove(FID fid);
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

	void InitInode(NODE_INFO* node, NID nid, CPageAllocator::INDEX page_id, CPageInfo * page)
	{
		memset(node, 0xFF, sizeof(NODE_INFO));
		node->m_nid = nid;
		node->m_type = NODE_INFO::NODE_INODE;
		node->valid_data = 0;
		node->page_id = page_id;
//		node->m_parent_node = INVALID_BLK;
		node->inode.blk_num = 0;
		node->inode.file_size = 0;
		node->inode.ref_count = 0;
		node->inode.delete_on_close = false;

		page->data_index = nid;
		page->inode = nid;
		page->dirty = true;
	}

	void InitIndexNode(NODE_INFO* node, NID nid, NID parent, CPageAllocator::INDEX page_id, CPageInfo * page)
	{
		memset(node, 0xFF, sizeof(NODE_INFO));
		node->m_nid = nid;
		node->m_type = NODE_INFO::NODE_INDEX;
		node->m_parent_node = parent;
		node->valid_data = 0;
		node->page_id = page_id;

		page->data_index = nid;
		page->inode = parent;
		page->dirty = true;
	}

	inline NODE_INFO& get_inode(FID fid)
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
//	CInodeManager_ m_inodes;
	CBufferManager<NODE_INFO> m_block_buf;

protected:
	std::map<std::wstring, FID> m_path_map;
//	LBLK_T m_level_to_offset[MAX_INDEX_LEVEL];
	int m_multihead_cnt = 0;
	UINT m_node_blks = 0;	//ʹ�õ�inode��index node������

	// data for log
	size_t m_write_count = 0;
//	SEG_T m_gc_th_low, m_gc_th_hi;
	// ����ɾ���ļ���д����ͳ�ơ�
	//UINT64 m_truncated_host_write[BT_TEMP_NR];
	//UINT64 m_truncated_media_write[BT_TEMP_NR];


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

	UINT m_file_num = 0;						// �ܵ���Ч�ļ�������
	int m_free_blks = 0;
};


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "fs_simulator.h"
#include "config.h"

#include "f2fs_segment.h"

#include "storage.h"
#include "blocks.h"
#include "pages.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == ��������  ==



#define ROOT_FID				(0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



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

class CNodeAddressTable
{
public:
	CNodeAddressTable(CF2fsSimulator* fs);
	NID Init(PHY_BLK root);
	// ���ؿ��е� node����
	int build_free(void);
	void CopyFrom(const CNodeAddressTable* src);
public:
	bool Load(CKPT_BLOCK & checkpoint);
	void Sync(void);
	void Reset(void);

	NID get_node(void);
	void put_node(NID node);
	PHY_BLK get_phy_blk(NID nid);
	void set_phy_blk(NID nid, PHY_BLK phy_blk);
	void f2fs_flush_nat_entries(CKPT_BLOCK & checkpoint);
	void f2fs_out_nat_journal(NAT_JOURNAL_ENTRY* journal, UINT & journal_nr);

	void set_dirty(NID nid);
	void clear_dirty(NID nid);
	DWORD is_dirty(NID nid);
	UINT get_dirty_node_nr(void);

public:
	PAGE_INDEX node_cache[NODE_NR];
	UINT free_nr;
protected:
	PHY_BLK nat[NODE_NR];
	DWORD dirty[NAT_BLK_NR];	// ��¼NAT��dirty��ÿ��bitһ��NAT��
	NID next_scan;				// �����λ�ÿ�ʼ������һ��free
	CPageAllocator* m_pages;
	CStorage* m_storage;
	CF2fsSimulator* m_fs;
};

// ���ڻ����Ѿ��򿪵��ļ���inode��page
struct OPENED_FILE
{
	NID ino;
	PAGE_INDEX ipage;
};

struct F2FS_FSCK;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == file system  ==
class CF2fsSimulator : public IFsSimulator
{
protected:
	CF2fsSimulator(void);
	virtual ~CF2fsSimulator(void);

public:
	virtual void Clone(IFsSimulator*& dst);
	virtual void CopyFrom(const IFsSimulator* src);
	virtual void add_ref(void) {
		m_ref++;
	}
	virtual void release(void) {
		m_ref--;
		if (m_ref == 0) delete this;
	}
	static IFsSimulator* factory(void) {
		CF2fsSimulator* fs = new CF2fsSimulator;
		return static_cast<IFsSimulator*>(fs);
	}

protected:
	void InternalCopyFrom(const CF2fsSimulator* src);

public:
	virtual bool Initialzie(const boost::property_tree::wptree& config, const std::wstring & log_path);
	virtual ERROR_CODE  FileCreate(NID & fid, const std::string& fn);
	virtual ERROR_CODE  DirCreate(NID & fid, const std::string& fn);

	virtual void SetFileSize(NID fid, FSIZE secs) {}
	virtual FSIZE FileWrite(NID fid, FSIZE offset, FSIZE len);
	virtual size_t FileRead(FILE_DATA blks[], NID fid, FSIZE offset, FSIZE secs);
	// ����truncate�����ļ�
	virtual void FileTruncate(NID fid, FSIZE offset, FSIZE secs);
	virtual void FileDelete(const std::string & fn);
	virtual ERROR_CODE DirDelete(const std::string& fn);

	virtual void FileFlush(NID fid);
	virtual void FileClose(NID fid);
	virtual ERROR_CODE  FileOpen(NID & fid, const std::string& fn, bool delete_on_close = false);

	virtual FSIZE GetFileSize(NID fid);
	// ���ڵ��ԣ�����Ҫ���ļ���size���ļ���С��node block������inode���ڣ�index block������data_blk��ʵ��ռ��block����
	virtual void GetFileInfo(NID fid, FSIZE& size, FSIZE& node_blk, FSIZE& data_blk);

	virtual void GetConfig(boost::property_tree::wptree& config, const std::wstring& config_name);
	// �������֧���ļ���С��block����
	virtual DWORD MaxFileSize(void) const { return MAX_FILE_BLKS - 10; }

	//	virtual void SetLogFolder(const std::wstring& fn);
	virtual void GetFsInfo(FS_INFO& space_info);
	virtual void GetHealthInfo(FsHealthInfo& info) const;

	// ��storage������storage��ز���
	virtual UINT GetCacheNum(void) { return m_storage.GetCacheNum(); }
	// ����fidĿ¼�µ���Ŀ¼���ļ�����
	virtual void GetFileDirNum(NID fid, UINT& file_nr, UINT& dir_nr);

	virtual bool Mount(void);
	virtual bool Unmount(void);
	virtual bool Reset(UINT rollback);
	virtual ERROR_CODE fsck(bool fix);

	// ���½ӿ����ڲ��ԡ�
	virtual void DumpSegments(const std::wstring& fn, bool sanity_check);
	virtual void DumpSegmentBlocks(const std::wstring& fn);
	virtual void DumpFileMap(FILE* out, NID fid) { DumpFileMap_merge(out, fid); }

	virtual void DumpAllFileMap(const std::wstring& fn);
	virtual void DumpBlockWAF(const std::wstring& fn);
	virtual size_t DumpFileIndex(NID index[], size_t buf_size, NID fid);
	void DumpFileMap_no_merge(FILE* out, NID fid);

	virtual void GetGcTrace(std::vector<GC_TRACE>& gc) { 
#ifdef GC_TRACE
		gc = m_segments.gc_trace; 
#endif
	};




#ifdef ENABLE_FS_TRACE
	void fs_trace(const char* op, NID fid, DWORD start_blk, DWORD blk_nr);
#else
	void fs_trace(const char* op, NID fid, DWORD start_blk, DWORD blk_nr) {}
#endif


// functions for fsck
protected:
	int fsck_chk_node_blk(F2FS_FSCK* fsck, NID nid, F2FS_FILE_TYPE file_type, BLOCK_DATA::BLOCK_TYPE block_type, UINT &blk_cnt);
	int fsck_init(F2FS_FSCK* fsck);
	int fsck_chk_inode_blk(F2FS_FSCK* fsck, NID nid, PHY_BLK blk, F2FS_FILE_TYPE file_type, NODE_INFO & node_data);
	int fsck_chk_index_blk(F2FS_FSCK* fsck, NID nid, PHY_BLK blk, F2FS_FILE_TYPE file_type, NODE_INFO& node_date, UINT &blk_cnt);
	int fsck_chk_data_blk (F2FS_FSCK* fsck, NID nid, WORD offset, PHY_BLK blk, F2FS_FILE_TYPE file_type);
	int fsck_chk_metadata(F2FS_FSCK* fsck);
	int fsck_verify(F2FS_FSCK* fsck);
	DWORD f2fs_test_main_bitmap(F2FS_FSCK* fsck, PHY_BLK blk);
	DWORD f2fs_set_main_bitmap(F2FS_FSCK* fsck, PHY_BLK blk);
	DWORD f2fs_set_nat_bitmap(F2FS_FSCK* fsck, NID nid);
	DWORD f2fs_test_node_bitmap(F2FS_FSCK* fsck, NID nid);
	DWORD f2fs_set_node_bitmap(F2FS_FSCK* fsck, NID nid);

	// ����nid��ȡnode�����ǲ���cache node�� ����������
	void ReadNodeNoCache(NODE_INFO& node, NID nid);
	void ReadBlockNoCache(BLOCK_DATA& data, PHY_BLK blk);

protected:
	friend class CF2fsSegmentManager;
	friend class CStorage;
	friend class CPageAllocator;
	friend class CNodeAddressTable;

	// �ҵ��ļ�fn(ȫ·������NID��parent����NID���ڸ�Ŀ¼��inode��page
	NID FileOpenInternal(char* fn, CPageInfo* &parent);
	// ��unmount��ʱ��ǿ�ƹر������ļ�
	void ForceClose(OPENED_FILE* file);
	// �ڸ�Ŀ¼�в����ļ�fn, �ҵ�����fid���Ҳ�������invalid_blk
	NID FindFile(NODE_INFO& parent, const char* fn);

	OPENED_FILE* FindOpenFile(NID fid);
	OPENED_FILE* AddFileToOpenList(NID fid, CPageInfo* page);

	void InitOpenList(void);

	// �رմ򿪵�inode���ͷű�inode�����page�������ļ��Ƿ�dirty
	bool CloseInode(CPageInfo* &ipage);

	// pages: out ��ȡ����page_id�������������ڴ�
	void FileReadInternal(CPageInfo * pages[], NODE_INFO& inode, FSIZE start_blk, FSIZE end_blk);
	// ����ʵ��д��� block ����
	UINT FileWriteInternal(NODE_INFO& inode, FSIZE start_blk, FSIZE end_blk, CPageInfo* pages[]);
	void FileTruncateInternal(CPageInfo * ipage, LBLK_T start_blk, LBLK_T end_blk);
	void FileSyncInternal(CPageInfo* ipage);
	ERROR_CODE sync_fs(void);

	void f2fs_write_checkpoint();
	// �Ӵ��̶�ȡcheckpoint
	bool load_checkpoint();
	void save_checkpoint();

	inline WORD FileNameHash(const char* fn)
	{
		size_t seed = 0;
		const char* p = fn;
		while (*p != 0) {
			boost::hash_combine(seed, *p); p++;
		}
		return ( (WORD)(seed & 0xFFFF) );
	}

	// == dir ��غ���
	ERROR_CODE InternalCreatreFile(CPageInfo * &page, NID & fid, const std::string& fn, bool is_dir);

	// ���ļ���ӵ�parent�У�parent:��Ҫ��ӵ�Ŀ¼�ļ���inode��nid�����ļ���inode id
	ERROR_CODE add_link(NODE_INFO* parent, const char* fn, NID nid);
	// ��parent�н�fid�Ƴ�
	void unlink(NID fid, CPageInfo* parent);

	UINT GetChildNumber(NODE_INFO* inode);

	void FileRemove(CPageInfo* &inode_page);
	// ����block��ʵ���¶ȣ�������Ŀǰʹ�õ��㷨��
	BLK_TEMP GetBlockTemp(CPageInfo* page);
	// ���ݵ�ǰ���㷨����block���¶ȡ�
	BLK_TEMP GetAlgorithmBlockTemp(CPageInfo* page, BLK_TEMP temp);
	// �����file mapʱ�����ϲ�����������block
	bool InvalidBlock(const char* reason, PHY_BLK phy_blk);
	bool OffsetToIndex(CIndexPath& path, LBLK_T offset, bool alloc);
	// ��index_path�ƶ�����һ��blockλ�á����ڼ���offset��index��ת�������index block�����仯�������path, ��Ҫ���¼��㡣
	void NextOffset(CIndexPath& path);
	// ���inode�е�����index block��inode block�����û���ڴ����ϣ��Ᵽ��, ����inode��phy blk
	PHY_BLK UpdateInode(CPageInfo * ipage, const char* caller = "");

	// �����file mapʱ��������������block
	void DumpFileMap_merge(FILE* out, NID fid);

	bool InitInode(BLOCK_DATA* block, CPageInfo* page, F2FS_FILE_TYPE type);

	bool InitIndexNode(BLOCK_DATA* block, NID parent, CPageInfo* page);

	void InitDentry(BLOCK_DATA* block)
	{
		memset(block, 0xFF, sizeof(BLOCK_DATA));
		block->m_type = BLOCK_DATA::BLOCK_DENTRY;
		DENTRY_BLOCK& entries = block->dentry;
		entries.bitmap = 0;
	}

	// ͨ���ļ����ȼ����ļ���block������������Ч������
	inline static UINT get_file_blks(INODE& inode)
	{
		UINT blk = ROUND_UP_DIV(inode.file_size, BLOCK_SIZE);
		return blk;
	}

	// ����NAT��nid��node id��phy_blk��node�µ������ַ
	void UpdateNat(NID nid, PHY_BLK phy_blk);
	// ����index table�У�block�������ַ��nid��index table��node id��offset��data block��index�е�offset��phy_blk��data block�µ������ַ
	void UpdateIndex(NID nid, UINT offset, PHY_BLK phy_blk);

	CPageInfo* AllocateDentryBlock();
	// ��ȡNode����������page�С�page���Զ�cache������Ҫ�ͷ�
	NODE_INFO& ReadNode(NID fid, CPageInfo * &page);
	// �ļ�ϵͳ״̬������Cloneʱ����Ҫ���Ƶı�����
protected:
	// ģ�����layout
	CKPT_BLOCK			m_checkpoint;
	CF2fsSegmentManager m_segments;
	CNodeAddressTable	m_nat;
	CStorage			m_storage;
	FsHealthInfo		m_health_info;
//	FS_INFO				m_fs_info;

protected:
	// OS�����ݻ��档m_pagesģ��OS��ҳ���棬m_block_bufΪpage�ṩ���ݡ������ļ����ݲ���Ҫʵ�����ݣ�����ʡ�ԡ�
	CPageAllocator m_pages;
	OPENED_FILE m_open_files[MAX_OPEN_FILE];
	UINT m_free_ptr;	// �����б��ͷָ��
	UINT m_open_nr;		// �򿪵��ļ�����

protected:
	int m_multihead_cnt = 0;
	UINT m_node_blks = 0;	//ʹ�õ�inode��index node������

	// data for log
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

	UINT m_ref;	//���ü���
};


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "fs_simulator.h"
#include "config.h"

#include "f2fs_segment.h"

#include "storage.h"
#include "blocks.h"
#include "pages.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == 参数配置  ==



#define ROOT_FID				(0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



// 用于描述 inode中index table的映射关系
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
	_NID Init(PHY_BLK root);
	// 返回空闲的 node数量
	int build_free(void);
	void CopyFrom(const CNodeAddressTable* src);
public:
	bool Load(CKPT_BLOCK & checkpoint);
	void Sync(void);
	void Reset(void);

	_NID get_node(void);
	void put_node(_NID node);
	PHY_BLK get_phy_blk(_NID nid);
	void set_phy_blk(_NID nid, PHY_BLK phy_blk);
	void f2fs_flush_nat_entries(CKPT_BLOCK & checkpoint);
	void f2fs_out_nat_journal(/*NAT_JOURNAL_ENTRY* journal, UINT & journal_nr, */CPageInfo ** nat_pages, CKPT_BLOCK & checkpoint);
	LBLK_T get_nat_next_block(UINT nn, CKPT_BLOCK& checkpoint);
	LBLK_T get_nat_block(UINT nn, const CKPT_BLOCK& checkpoint);

	void set_dirty(_NID nid);
	void clear_dirty(_NID nid);
	DWORD is_dirty(_NID nid);
	UINT get_dirty_node_nr(void);

public:
	// for debug
	void DumpNat(FILE* out);

public:
	PAGE_INDEX node_cache[NODE_NR];
	UINT free_nr;
protected:
	PHY_BLK nat[NODE_NR];
	DWORD dirty[NAT_BLK_NR];	// 记录NAT的dirty，每个bit一个NAT。
	_NID next_scan;				// 从这个位置开始搜索下一个free
	CPageAllocator* m_pages;
	CStorage* m_storage;
	CF2fsSimulator* m_fs;


};

// 用于缓存已经打开的文件，inode的page
struct OPENED_FILE
{
	_NID ino;
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
	bool SanityCheckConfig(void);

public:
	virtual bool Initialzie(const boost::property_tree::wptree& config, const std::wstring & log_path);
	virtual ERROR_CODE  FileCreate(_NID & fid, const std::string& fn);
	virtual ERROR_CODE  DirCreate(_NID & fid, const std::string& fn);

	virtual void SetFileSize(_NID fid, FSIZE secs) {}
	virtual FSIZE FileWrite(_NID fid, FSIZE offset, FSIZE len);
	virtual size_t FileRead(FILE_DATA blks[], _NID fid, FSIZE offset, FSIZE secs);
	// 可以truncate部分文件
	virtual void FileTruncate(_NID fid, FSIZE offset, FSIZE secs);
	virtual void FileDelete(const std::string & fn);
	virtual ERROR_CODE DirDelete(const std::string& fn);

	virtual void FileFlush(_NID fid);
	virtual void FileClose(_NID fid);
	virtual ERROR_CODE  FileOpen(_NID & fid, const std::string& fn, bool delete_on_close = false);

	virtual FSIZE GetFileSize(_NID fid);
	// 用于调试，不需要打开文件。size：文件大小。node block：包括inode在内，index block数量；data_blk：实际占用block数量
	virtual void GetFileInfo(_NID fid, FSIZE& size, FSIZE& node_blk, FSIZE& data_blk);

	virtual void GetConfig(boost::property_tree::wptree& config, const std::wstring& config_name);
	// 返回最大支持文件大小的block数量
	virtual DWORD MaxFileSize(void) const { return MAX_FILE_BLKS - 10; }

	//	virtual void SetLogFolder(const std::wstring& fn);
	virtual void GetFsInfo(FS_INFO& space_info);
	virtual void GetHealthInfo(FsHealthInfo& info) const;

	// 对storage，用于storage相关测试
	virtual UINT GetCacheNum(void) { return m_storage.GetCacheNum(); }
	// 返回fid目录下的子目录和文件总数
	virtual void GetFileDirNum(_NID fid, UINT& file_nr, UINT& dir_nr);

	virtual bool Mount(void);
	virtual bool Unmount(void);
	virtual bool Reset(UINT rollback);
	virtual ERROR_CODE fsck(bool fix);

	// 以下接口用于测试。
	//virtual void DumpSegments(const std::wstring& fn, bool sanity_check);
	//virtual void DumpSegmentBlocks(const std::wstring& fn);
	//virtual void DumpFileMap(FILE* out, _NID fid) { DumpFileMap_merge(out, fid); }

	//virtual void DumpAllFileMap(const std::wstring& fn);
	//virtual void DumpBlockWAF(const std::wstring& fn);
	//virtual size_t DumpFileIndex(_NID index[], size_t buf_size, _NID fid);
	//void DumpFileMap_no_merge(FILE* out, _NID fid);

//	virtual void GetGcTrace(std::vector<GC_TRACE>& gc) { 
//#ifdef GC_TRACE
//		gc = m_segments.gc_trace; 
//#endif
//	};
	// 通过flag，打开或者关闭对应的log内容，这些内容只保存在file system内部。
	virtual void LogOption(FILE * out, DWORD flag);
	// 通过dump相应的log到指定输出
	virtual void DumpLog(FILE* out, const char* log_name);




#ifdef ENABLE_FS_TRACE
	void fs_trace(const char* op, _NID fid, DWORD start_blk, DWORD blk_nr);
#else
	void fs_trace(const char* op, _NID fid, DWORD start_blk, DWORD blk_nr) {}
#endif

protected:
	void DumpCheckpoint(FILE * out, const CKPT_BLOCK& checkpoint);
	void DumpNatPage(FILE* out);
	//void DumpNat(FILE* out);


// functions for fsck
protected:
	int fsck_chk_node_blk(F2FS_FSCK* fsck, _NID nid, F2FS_FILE_TYPE file_type, BLOCK_DATA::BLOCK_TYPE block_type, UINT &blk_cnt);
	int fsck_init(F2FS_FSCK* fsck);
	int fsck_chk_inode_blk(F2FS_FSCK* fsck, _NID nid, PHY_BLK blk, F2FS_FILE_TYPE file_type, NODE_INFO & node_data);
	int fsck_chk_index_blk(F2FS_FSCK* fsck, _NID nid, PHY_BLK blk, F2FS_FILE_TYPE file_type, NODE_INFO& node_date, UINT &blk_cnt);
	int fsck_chk_data_blk (F2FS_FSCK* fsck, _NID nid, WORD offset, PHY_BLK blk, F2FS_FILE_TYPE file_type);
	int fsck_chk_metadata(F2FS_FSCK* fsck);
	int fsck_verify(F2FS_FSCK* fsck);
	DWORD f2fs_test_main_bitmap(F2FS_FSCK* fsck, PHY_BLK blk);
	DWORD f2fs_set_main_bitmap(F2FS_FSCK* fsck, PHY_BLK blk);
	DWORD f2fs_set_nat_bitmap(F2FS_FSCK* fsck, _NID nid);
	DWORD f2fs_test_node_bitmap(F2FS_FSCK* fsck, _NID nid);
	DWORD f2fs_set_node_bitmap(F2FS_FSCK* fsck, _NID nid);

	// 根据nid读取node，但是不会cache node， 仅复制数据
	void ReadNodeNoCache(NODE_INFO& node, _NID nid);
	void ReadBlockNoCache(BLOCK_DATA& data, PHY_BLK blk);

protected:
	friend class CF2fsSegmentManager;
	friend class CStorage;
	friend class CPageAllocator;
	friend class CNodeAddressTable;

	// 找到文件fn(全路径）的NID，parent返回NID所在父目录的inode的page
	_NID FileOpenInternal(char* fn, CPageInfo* &parent);
	// 当unmount的时候，强制关闭所有文件
	void ForceClose(OPENED_FILE* file);
	// 在父目录中查找文件fn, 找到返回fid，找不到返回invalid_blk
	_NID FindFile(NODE_INFO& parent, const char* fn);

	OPENED_FILE* FindOpenFile(_NID fid);
	OPENED_FILE* AddFileToOpenList(_NID fid, CPageInfo* page);

	void InitOpenList(void);

	// 关闭打开的inode，释放被inode缓存的page。返回文件是否dirty
	bool CloseInode(CPageInfo* &ipage);

	// pages: out 读取到的page_id，调用者申请内存
	void FileReadInternal(CPageInfo * pages[], NODE_INFO& inode, FSIZE start_blk, FSIZE end_blk);
	// 返回实际写入的 block 数量
	UINT FileWriteInternal(NODE_INFO& inode, FSIZE start_blk, FSIZE end_blk, CPageInfo* pages[], bool force=false);
	void FileTruncateInternal(CPageInfo * ipage, LBLK_T start_blk, LBLK_T end_blk);
	void FileSyncInternal(CPageInfo* ipage);
	ERROR_CODE sync_fs(void);
	ERROR_CODE f2fs_sync_node_page(void);

	void f2fs_write_checkpoint();
	// 从磁盘读取checkpoint, 返回checkpoint的序号
	int load_checkpoint();
	void save_checkpoint(int ckpt_id);

	inline WORD FileNameHash(const char* fn)
	{
		size_t seed = 0;
		const char* p = fn;
		while (*p != 0) {
			boost::hash_combine(seed, *p); p++;
		}
		return ( (WORD)(seed & 0xFFFF) );
	}

	// == dir 相关函数
	ERROR_CODE InternalCreatreFile(CPageInfo * &page, _NID & fid, const std::string& fn, bool is_dir);

	// 将文件添加到parent中，parent:需要添加的目录文件的inode，nid：子文件的inode id
	ERROR_CODE add_link(NODE_INFO* parent, const char* fn, _NID nid);
	// 从parent中将fid移除
	void unlink(_NID fid, CPageInfo* parent);

	UINT GetChildNumber(NODE_INFO* inode);

	void FileRemove(CPageInfo* &inode_page);
	// 计算block的实际温度，不考虑目前使用的算法。
	BLK_TEMP GetBlockTemp(CPageInfo* page);
	// 根据当前的算法计算block的温度。
	BLK_TEMP GetAlgorithmBlockTemp(CPageInfo* page, BLK_TEMP temp);
	// 在输出file map时，不合并连续的物理block
	bool InvalidBlock(const char* reason, PHY_BLK phy_blk);
	bool OffsetToIndex(CIndexPath& path, LBLK_T offset, bool alloc);
	// 将index_path移动到下一个block位置。用于加速offset到index的转化。如果index block发生变化，则清除path, 需要重新计算。
	void NextOffset(CIndexPath& path);
	// 检查inode中的所有index block和inode block，如果没有在磁盘上，测保存, 返回inode的phy blk
	PHY_BLK UpdateInode(CPageInfo * ipage, const char* caller = "");

	// 在输出file map时，并连续的物理block
	//void DumpFileMap_merge(FILE* out, _NID fid);

	bool InitInode(BLOCK_DATA* block, CPageInfo* page, F2FS_FILE_TYPE type);

	bool InitIndexNode(BLOCK_DATA* block, _NID parent, CPageInfo* page);

	void InitDentry(BLOCK_DATA* block)
	{
		memset(block, 0xFF, sizeof(BLOCK_DATA));
		block->m_type = BLOCK_DATA::BLOCK_DENTRY;
		DENTRY_BLOCK& entries = block->dentry;
		entries.bitmap = 0;
	}

	// 通过文件长度计算文件的block数量（不是有效块数）
	inline static UINT get_file_blks(INODE& inode)
	{
		UINT blk = ROUND_UP_DIV(inode.file_size, BLOCK_SIZE);
		return blk;
	}

	// 更新NAT，nid：node id，phy_blk：node新的物理地址
	void UpdateNat(_NID nid, PHY_BLK phy_blk);
	// 更新index table中，block的物理地址，nid：index table的node id，offset：data block在index中的offset，phy_blk：data block新的物理地址
	void UpdateIndex(_NID nid, UINT offset, PHY_BLK phy_blk);

	CPageInfo* AllocateDentryBlock();
	// 读取Node，并保存在page中。page会自动cache，不需要释放
	NODE_INFO& ReadNode(_NID fid, CPageInfo * &page);
	// 文件系统状态变量（Clone时，需要复制的变量）
protected:
	// 模拟磁盘layout
	CKPT_BLOCK			m_checkpoint;
	CF2fsSegmentManager m_segments;
	CNodeAddressTable	m_nat;
	CStorage			m_storage;
	FsHealthInfo		m_health_info;
//	FS_INFO				m_fs_info;

protected:
	// OS的数据缓存。m_pages模拟OS的页缓存，m_block_buf为page提供数据。由于文件数据不需要实际数据，可以省略。
	CPageAllocator m_pages;
	OPENED_FILE m_open_files[MAX_OPEN_FILE];
	UINT m_free_ptr;	// 空闲列表的头指针
	UINT m_open_nr;		// 打开的文件数量

protected:
	int m_multihead_cnt = 0;
	UINT m_node_blks = 0;	//使用的inode或index node的数量
	int m_op_segs = 0;
	int m_cur_ckpt;			//只是当前使用的checkpoint的位置
	// data for log
	// 对于删除文件的写入量统计。

protected:
	// log和统计信息
	std::wstring m_log_path;
#ifdef ENABLE_FS_TRACE
	FILE* m_log_invalid_trace = nullptr;
	FILE* m_log_write_trace = nullptr;		// 文件：trace_fs.csv
	// 用于记录文件系统层次的写入
	FILE* m_log_fs_trace = nullptr;			// 文件: fs_trace.csv;
	FILE* m_gc_trace = nullptr;
	FILE* m_inode_trace = nullptr;
#endif

	// log options
	bool m_log_fsck = false;
	bool m_log_storage = false;
	FILE* m_log_out = nullptr;

	UINT m_ref;	//引用计数
};


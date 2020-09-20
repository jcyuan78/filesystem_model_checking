#pragma once
#include <stdext.h>
#include <boost/property_tree/json_parser.hpp>
#include <dokanfs-lib.h>

#include <list>

#define MAX_LOG_NUM	(1024*1024)

class CNandLog
{
public:
	enum {LOG_WRITE_CHUNK, LOG_ERASE_BLOCK};
public:
	UINT32 op;
	UINT32 nand_chunk;
};

class CNandDriverFile : public INandDriver
{
public:
	CNandDriverFile();
	virtual ~CNandDriverFile();
public:
	bool CreateDevice(const boost::property_tree::wptree & prop, bool create);

// dummy implement for IVirtualDisk
protected:
	virtual size_t GetCapacity(void) { JCASSERT(0); return 0; }
	virtual bool ReadSectors(void * buf, UINT lba, size_t secs) { JCASSERT(0); return false; }
	virtual bool WriteSectors(void * buf, UINT lba, size_t secs) { JCASSERT(0); return false; }
	virtual bool Trim(UINT lba, size_t secs) { JCASSERT(0); return false; }
	virtual bool FlushData(UINT lba, size_t secs) { JCASSERT(0); return false; }
	virtual void SetSectorSize(UINT size) { JCASSERT(0);}
	virtual void CopyFrom(IVirtualDisk * dev) { JCASSERT(0);}
	// 将当前的image保存到文件中.
	virtual bool SaveSnapshot(const std::wstring &fn) { JCASSERT(0); return false; }

	virtual bool GetHealthInfo(HEALTH_INFO & info);

// journal device的interface
public:
	virtual size_t GetLogNumber(void) const { return m_log_count; }
	virtual bool BackLog(size_t num);
	virtual void ResetLog(void) { m_log_count = 0; }

public:
	virtual void GetFlashId(NAND_DEVICE_INFO & info);
	virtual bool WriteChunk(FAddress page, const BYTE *data, size_t data_len);
	virtual bool ReadChunk(FAddress nand_chunk, BYTE *data, size_t data_len, ECC_RESULT &ecc_result);

	virtual bool WriteChunk(int nand_chunk, const BYTE *data, size_t data_len, 
		const BYTE *oob, size_t oob_len);
	virtual bool ReadChunk(int nand_chunk, BYTE *data, size_t data_len, 
		BYTE *oob, size_t oob_len, ECC_RESULT &ecc_result);
	virtual bool Erase(int block_no);
	virtual bool MarkBad(int block_no);
	virtual bool CheckBad(int block_no);

	//virtual void QueryInitBlockState(int blk, yaffs_block_state &state, UINT32 &sn) {};

protected:
	void PushLog(UINT32 op, UINT32 chunk);
	void PopLog(UINT32 & op, UINT32 & chunk);

protected:
	void SwitchBlock(int blk);
protected:
	HANDLE m_file;
	// data_size: page中数据区大小，通常为2的整幂；spare_size, page_size：包含data_size+spare_size
	UINT32 m_data_size, m_spare_size, m_page_size;
	UINT32 m_page_per_block;
	size_t m_block_size;
	size_t m_block_num;
	size_t m_file_size;
	
	BYTE * m_block_buf;
	size_t m_cached_blk;
	bool m_dirty;

	CRITICAL_SECTION m_file_lock;

	FILE * m_log_file;
//	HANDLE m_log_file;
	std::wstring m_log_fn;

	UINT32 m_empty_blocks;

	UINT32 m_host_write;
	UINT32 m_host_read;
	UINT32 m_media_read;
	UINT32 m_media_write;
	size_t m_total_erase_count;

	CNandLog * m_logs;		// 循环队列记录log，用于power测试
//	CNandLog * m_log_header;	// 队列头指针。
	size_t m_log_header;
	size_t m_log_size;		// 队列有效长度，包括循环
	size_t m_log_count;		// 累计log数量
//	std::list<CNandLog> m_logs;



};


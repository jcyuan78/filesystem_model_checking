///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "segment_manager.h"

class CSsdSimulatorInterface
{
public:
	virtual ~CSsdSimulatorInterface(void) {}
public:
	virtual bool Initialize(const boost::property_tree::wptree& config) = 0;
	virtual void SetLogFile(const std::wstring& fn) = 0;
	virtual bool WriteSector(size_t lba, size_t secs, BLK_TEMP temp) = 0;
	virtual void DumpL2PMap(const std::wstring& fn) = 0;
	virtual void CheckingColdDataBySeg(const std::wstring& fn) = 0;
	virtual size_t GetLBlockNr(void) const = 0;
};

class CSsdSegmentManager : public CSegmentManagerBase<LBLK_T>
{
public:
	virtual PHY_BLK WriteBlockToSeg(const LBLK_T& lblk, BLK_TEMP temp);
	virtual void GarbageCollection(void) {}
};


class CSingleHeadSM : public CSsdSimulatorInterface
{
public:
	CSingleHeadSM(size_t seg_nr, size_t cap);
	CSingleHeadSM(void) {};
	virtual ~CSingleHeadSM(void);
public:
	virtual bool Initialize(const boost::property_tree::wptree& config);

	virtual void SetLogFile(const std::wstring& fn);
	virtual bool WriteSector(size_t lba, size_t secs, BLK_TEMP temp);
	virtual void DumpL2PMap(const std::wstring& fn);
	virtual void CheckingColdDataBySeg(const std::wstring& fn);
	virtual size_t GetLBlockNr(void) const { return m_blk_nr; }

protected:
	virtual void GarbageCollection(void);


protected:
	// 查找一个空的segment
	//SEG_T AllocSegment(BLK_TEMP temp);
	// 回收一个segment
	//void FreeSegment(SEG_T seg_id);
	// 在当前Segment中分配一个blk, 返回block id;
//	DWORD AllocBlockInSeg(DWORD lblk);
// 

	// 把逻辑block写入seg_id指定的block。如果seg_id为INVALID_BLK，则为其分配新的segment。
	// 写入的segment id和block id在seg_id和blk_id中返回
	// temp要写入数据的温度


	// 将src_seg:src_blk中的logical block移动到tar_seg中。
	// 一般的，tar_seg时current segment的引用。如果tar_seg为INVALID_BLK，则会自动分配。如果写入的是
	void MoveBlock(/*SEG_T& tar_seg,*/ SEG_T src_seg, BLK_T src_blk, BLK_TEMP temp);

protected:

	//	DWORD * m_seg_map = nullptr;		//表示segment是否使用的bitmap

		// Logical(LBA) to Physical(Segment/Block) mapping, 0xFFFF为未使用
		//DWORD* m_l2p_map = nullptr;
	LBLOCK_INFO* m_l2p_map = nullptr;
	size_t m_blk_nr;

	size_t m_total_host_write, m_total_media_write;
//	DWORD m_free_blk;
	size_t m_next_update;		// 下次更新的时间

	//	LOGS
	std::wstring m_log_fn;
	FILE* m_log_file = nullptr;

protected:
	CSsdSegmentManager m_segment;
private:
	//SEG_T m_cur_seg;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class CMultiHeadSM : public CSingleHeadSM
{
public:
	CMultiHeadSM(void) {};
	virtual ~CMultiHeadSM(void);
public:
	virtual bool Initialize(const boost::property_tree::wptree& config);
	virtual bool WriteSector(size_t lba, size_t secs, BLK_TEMP temp);
protected:
	// 根据lblock和host提供的temp信息，决定使用那个segment
	BLK_TEMP TemperaturePolicy(LBLK_T lblk, BLK_TEMP temp);

	virtual void GarbageCollection(void);


protected:
	enum TEMP_POLICY
	{
		BY_HOST,	// 根据host指示的温度处理，用于理想状况
		BY_COUNT,	// 根据write count动态决定，模拟实际状况
	};

	TEMP_POLICY m_temp_policy;

	//	size_t m_thread_nr;	// 最大支持温度数量
//	SEG_T m_cur_segs[BT_TEMP_NR];
};

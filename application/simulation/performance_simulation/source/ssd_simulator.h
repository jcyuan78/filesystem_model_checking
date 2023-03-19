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
	// ����һ���յ�segment
	//SEG_T AllocSegment(BLK_TEMP temp);
	// ����һ��segment
	//void FreeSegment(SEG_T seg_id);
	// �ڵ�ǰSegment�з���һ��blk, ����block id;
//	DWORD AllocBlockInSeg(DWORD lblk);
// 

	// ���߼�blockд��seg_idָ����block�����seg_idΪINVALID_BLK����Ϊ������µ�segment��
	// д���segment id��block id��seg_id��blk_id�з���
	// tempҪд�����ݵ��¶�


	// ��src_seg:src_blk�е�logical block�ƶ���tar_seg�С�
	// һ��ģ�tar_segʱcurrent segment�����á����tar_segΪINVALID_BLK������Զ����䡣���д�����
	void MoveBlock(/*SEG_T& tar_seg,*/ SEG_T src_seg, BLK_T src_blk, BLK_TEMP temp);

protected:

	//	DWORD * m_seg_map = nullptr;		//��ʾsegment�Ƿ�ʹ�õ�bitmap

		// Logical(LBA) to Physical(Segment/Block) mapping, 0xFFFFΪδʹ��
		//DWORD* m_l2p_map = nullptr;
	LBLOCK_INFO* m_l2p_map = nullptr;
	size_t m_blk_nr;

	size_t m_total_host_write, m_total_media_write;
//	DWORD m_free_blk;
	size_t m_next_update;		// �´θ��µ�ʱ��

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
	// ����lblock��host�ṩ��temp��Ϣ������ʹ���Ǹ�segment
	BLK_TEMP TemperaturePolicy(LBLK_T lblk, BLK_TEMP temp);

	virtual void GarbageCollection(void);


protected:
	enum TEMP_POLICY
	{
		BY_HOST,	// ����hostָʾ���¶ȴ�����������״��
		BY_COUNT,	// ����write count��̬������ģ��ʵ��״��
	};

	TEMP_POLICY m_temp_policy;

	//	size_t m_thread_nr;	// ���֧���¶�����
//	SEG_T m_cur_segs[BT_TEMP_NR];
};

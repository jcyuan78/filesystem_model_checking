#pragma once
#include <stdext.h>

class INandStore : public IJCInterface
{
public:
	// store
	virtual int Store(int page, const unsigned char * buffer) = 0;
	// retrieve
	virtual int Retrieve(int page, unsigned char * buffer) = 0;
	// erase
	virtual int Erase(int block) = 0;
	// shutdown
	virtual int Shutdown(void) = 0;
	virtual int GetBytesPerPage(void) const = 0;
	virtual int GetSpareBytesPerPage(void) const =0;
	virtual int GetBlockes(void) const = 0;
	virtual int GetPagesPerBlock(void) const = 0;

};

class CNandFileStore : public INandStore
{
public:
	CNandFileStore();
	virtual ~CNandFileStore();

public:
	// store
	virtual int Store(int page, const unsigned char * buffer);
	// retrieve
	virtual int Retrieve(int page, unsigned char * buffer);
	// erase
	virtual int Erase(int block);
	// shutdown
	virtual int Shutdown(void);
	virtual int GetBytesPerPage(void) const { return m_data_bytes_per_page; }
	virtual int GetSpareBytesPerPage(void) const { return m_spare_bytes_per_page; }
	virtual int GetBlockes(void) const { return m_blocks; }
	virtual int GetPagesPerBlock(void) const { return m_pages_per_block; }



public:
	bool Init(const std::wstring &fn, int blocks, int page_per_block, int data_bytes, int spare_bytes);
	void PowerFailInit(void);
	void MaybePowerFail(UINT32 nand_chunk, int fail_point);

protected:
// base members
	int m_blocks;
	int m_pages_per_block;
	int m_data_bytes_per_page;
	int m_spare_bytes_per_page;

// expand members
	std::wstring m_backing_file;
	int m_handle;
	BYTE * m_buffer;
	int m_buff_size;

//
	int remaining_ops;
	int nops_so_far;
	int random_seed;
	bool simulate_power_failure;
};

class CNandChip : public IJCInterface
{
public:
	CNandChip(void);
	virtual ~CNandChip(void);

public:
	//void(*set_ale)(struct nand_chip * this, int high);
	virtual void SetAle(int high) { m_ale = high; }

	//void(*set_cle)(struct nand_chip * this, int high);
	virtual void SetCle(int high) { m_cle = high; }

	//unsigned(*read_cycle)(struct nand_chip * this);
	virtual UINT32 ReadCycle(void);

	//void(*write_cycle)(struct nand_chip * this, unsigned b);
	virtual void WriteCycle(UINT32 b);

	//int(*check_busy)(struct nand_chip * this);
	virtual int CheckBusy(void);

	//void(*idle_fn) (struct nand_chip *this);
	virtual void IdleFn(void) {};

	//int(*power_check) (struct nand_chip *this);
	virtual int PowerCheck(void) { return 0; }

public:
	bool Init(INandStore * store, int bus_width_shift);
	bool InitPrivate(INandStore * store);
	void Idle(int line);
	void LastCmd(unsigned char val) { m_last_cmd_byte = val; }
	UINT32 DlRead(int bus_width_shift);
	void ClWrite(BYTE val);
	void AlWrite(BYTE val);
	void DlWrite(BYTE val, int bus_width_shift);

protected:
// basic members
	int m_blocks;
	int m_pages_per_block;
	int m_data_bytes_per_page;
	int m_spare_bytes_per_page;
	int m_bus_width_shift;

// extend members
	INandStore * m_store;
	// Access buffers.
	unsigned char *m_buffer;
	int m_buff_size;
	// Address buffer has two parts to it:	2 byte column (byte) offset, 3 byte row (page) offset
	unsigned char m_addr_buffer[5];

	// Offsets used to access address, read or write buffer.
	// If the offset is negative then accessing is illegal.
	int m_addr_offset;
	int m_addr_expected;
	int m_addr_received;

	int m_read_offset;
	int m_write_offset;
	int m_read_started;

	// busy_count: If greater than zero then the device is busy.
	// Busy count is decremented by check_busy() and by read_status()
	int m_busy_count;
	int m_write_prog_error;
	int m_reading_status;
	unsigned char m_last_cmd_byte;

	int m_ale;
	int m_cle;
};


#include "stdafx.h"
#include "..\include\nand_file_store.h"

LOCAL_LOGGER_ENABLE(L"nand_store", LOGGER_LEVEL_DEBUGINFO);


CNandFileStore::CNandFileStore()
{
}


CNandFileStore::~CNandFileStore()
{
}

int CNandFileStore::Store(int page, const unsigned char * buffer)
{
	//struct nandstore_file_private *nsfp =
	//	(struct nandstore_file_private *)this->private_data;
	int pos = m_buff_size * page;
	int i;

	MaybePowerFail(page, __LINE__);

	lseek(m_handle, pos, SEEK_SET);
	read(m_handle, m_buffer, m_buff_size);
	for (i = 0; i < m_buff_size; i++)	m_buffer[i] &= buffer[i];
	lseek(m_handle, pos, SEEK_SET);
	write(m_handle, m_buffer, m_buff_size);
	MaybePowerFail(page, __LINE__);
	return 0;
}

int CNandFileStore::Retrieve(int page, unsigned char * buffer)
{
	//struct nandstore_file_private *nsfp =
	//	(struct nandstore_file_private *)this->private_data;

	lseek(m_handle, m_buff_size * page, SEEK_SET);
	read(m_handle, buffer, m_buff_size);
	return 0;
}

int CNandFileStore::Erase(int block)
{
	int block = page / m_pages_per_block;
	//struct nandstore_file_private *nsfp =
	//	(struct nandstore_file_private *)this->private_data;
	int i;

	MaybePowerFail(page, __LINE__);

	lseek(m_handle,	block * m_buff_size * m_pages_per_block, SEEK_SET);
	memset(m_buffer, 0xff, m_buff_size);
	for (i = 0; i < m_pages_per_block; i++)
		write(m_handle, m_buffer, m_buff_size);
	return 0;
}

int CNandFileStore::Shutdown(void)
{
	//struct nandstore_file_private *nsfp =
	//	(struct nandstore_file_private *)this->private_data;
	close(m_handle);
	m_handle = -1;
	return 0;
}

bool CNandFileStore::Init(const std::wstring & fn, int blocks, int pages_per_block, int data_bytes, int spare_bytes)
{
	int fsize;
	int nbytes;
	int i;
	struct nand_store *ns;
	struct nandstore_file_private *nsfp;
	BYTE *buffer;
	int buff_size = data_bytes + spare_bytes;

//	ns = malloc(sizeof(struct nand_store));
//	nsfp = malloc(sizeof(struct nandstore_file_private));
//	buffer = malloc(buff_size);
	buffer = new BYTE[buff_size];


	//if (!ns || !nsfp || !buffer) 
	//{
	//	free(ns);
	//	free(nsfp);
	//	free(buffer);
	//	return NULL;
	//}

	//memset(ns, 0, sizeof(*ns));
	//memset(nsfp, 0, sizeof(*nsfp));
	m_buffer = buffer;
	m_buff_size = buff_size;
//	ns->private_data = nsfp;

	//ns->store = nandstore_file_store;
	//m_retrieve = nandstore_file_retrieve;
	//ns->erase = nandstore_file_erase;
	//ns->shutdown = nandstore_file_shutdown;

	m_blocks = blocks;
	m_pages_per_block = pages_per_block;
	m_data_bytes_per_page = data_bytes;
	m_spare_bytes_per_page = spare_bytes;

//	strncpy(m_backing_file, fname, sizeof(m_backing_file));
	m_backing_file = fn;

	m_handle = open(m_backing_file, O_RDWR | O_CREAT, 0666);
	if (m_handle >= 0) 
	{
		fsize = lseek(m_handle, 0, SEEK_END);
		nbytes = m_blocks * m_pages_per_block *
			(m_data_bytes_per_page + m_spare_bytes_per_page);
		if (fsize != nbytes) 
		{
			printf("Initialising backing file.\n");
			ftruncate(m_handle, 0);
			for (i = 0; i < m_blocks; i++)			Erase(i * m_pages_per_block);
		}
	}

	PowerFailInit();

	return ns;

}

void CNandFileStore::PowerFailInit(void)
{
	remaining_ops = (rand() % 1000) * 5;
}

void CNandFileStore::MaybePowerFail(UINT32 nand_chunk, int fail_point)
{
	nops_so_far++;
	remaining_ops--;
	if (simulate_power_failure && remaining_ops < 1) 
	{
		printf("Simulated power failure after %d operations\n", nops_so_far);
		printf("  power failed on nand_chunk %d, at fail point %d\n",
			nand_chunk, fail_point);
		exit(0);
	}
}

///////////////////////////////////////////////////////////////////////////////
// --  NAND Chip

bool CNandChip::Init(INandStore * store, int bus_width_shift)
{
	//struct nand_chip *chip = NULL;
	//struct nandsim_private *ns = NULL;

	//chip = malloc(sizeof(struct nand_chip));
//	ns = nandsim_init_private(store);

//	if (chip && ns) {
//		memset(chip, 0, sizeof(struct nand_chip));;

	// 通过虚函数实现，省略设置函数
		//m_private_data = ns;
		//m_set_ale = nandsim_set_ale;
		//m_set_cle = nandsim_set_cle;
		//m_read_cycle = nandsim_read_cycle;
		//m_write_cycle = nandsim_write_cycle;
		//m_check_busy = nandsim_check_busy;
		//m_idle_fn = nandsim_idle_fn;

	m_bus_width_shift = bus_width_shift;
	m_blocks = m_store->GetBlockes();
	m_pages_per_block = m_store->GetPagesPerBlock();
	m_data_bytes_per_page = m_store->GetBytesPerPage();
	m_spare_bytes_per_page = m_store->GetSpareBytesPerPage();
	return true;
}

bool CNandChip::InitPrivate(INandStore * store)
{
	JCASSERT(store);
	unsigned char *buffer;
	int buff_size;

	buff_size = (store->GetBytesPerPage() + store->GetSpareBytesPerPage());

//	ns = malloc(sizeof(struct nandsim_private));
	buffer = new BYTE[buff_size];
	if (!buffer) THROW_ERROR(ERR_APP, L"failed on creating buffer");
	//	memset(ns, 0, sizeof(struct nandsim_private));
	m_write_prog_error = 0;
	memset(m_addr_buffer, 0, 5);

	m_buffer = buffer;
	m_buff_size = buff_size;
	m_store = store;
	m_store->AddRef();
	Idle(__LINE__);
	//return ns;
	return true;
}

void CNandChip::Idle(int line)
{
	(void)line;

	m_read_offset = -1;
	m_write_offset = -1;
	m_addr_offset = -1;
	m_addr_expected = -1;
	m_addr_received = -1;
	LastCmd(0xff);
	m_busy_count = 0;
	m_reading_status = 0;
	m_read_started = 0;

}

UINT32 CNandChip::DlRead(int bus_width_shift)
{
	unsigned retval;
	if (m_reading_status) 
	{
		/*
		 * bit 0 == 0 pass, == 1 fail.
		 * bit 6 == 0 busy, == 1 ready
		 */
		retval = 0xfe;
		if (m_busy_count > 0) 
		{
			m_busy_count--;
			retval &= ~(1 << 6);
		}
		if (m_write_prog_error)			retval |= ~(1 << -0);
		LOG_DEBUG(L"Read status returning %02X", retval);
	}
	else if (m_busy_count > 0) 
	{
		LOG_DEBUG(L"Read while still busy");
		retval = 0;
	}
	else if (m_read_offset < 0 || m_read_offset >= m_buff_size) 
	{
		LOG_DEBUG(L"Read with no data available");
		retval = 0;
	}
	else if (bus_width_shift == 0) 
	{
		retval = m_buffer[m_read_offset];
		m_read_offset++;
	}
	else if (bus_width_shift == 1) 
	{
		retval = m_buffer[m_read_offset];
		m_read_offset++;
		retval |= (((unsigned)m_buffer[m_read_offset]) << 8);
		m_read_offset++;
	}
	return retval;
}

void CNandChip::ClWrite(BYTE val)
{
	LOG_DEBUG(L"CLE write %02X", val);
	switch (val)
	{
	case 0x00:	read_0(ns);	break;
	case 0x05:	random_data_output_0(ns);	break;
	case 0x10:	program_1(ns);			break;
	case 0x15:	unsupported(ns);		break;
	case 0x30:	read_1(ns);			break;
	case 0x35:	unsupported(ns);		break;
	case 0x60:	block_erase_0(ns);		break;
	case 0x70:	read_status(ns);		break;
	case 0x80:	program_0(ns);			break;
	case 0x85:	random_data_input(ns);	break;
	case 0x90:	read_id(ns);			break;
	case 0xD0:	block_erase_1(ns);		break;
	case 0xE0:	random_data_output_1(ns);	break;
	case 0xFF:	reset_0(ns);		break;
	default:	LOG_DEBUG(1, L"CLE written with invalid value %02X.", val);
		break;
	}
}

void CNandChip::AlWrite(BYTE val)
{
	check_not_busy(ns, __LINE__);
	if (m_addr_expected < 1 ||
		m_addr_offset < 0 ||
		m_addr_offset >= (int)sizeof(m_addr_buffer)) 
	{
		LOG_DEBUG(L"Address write when not expected");
	}
	else 
	{
		LOG_DEBUG(L"Address write when expecting %d bytes",	m_addr_expected);
		m_addr_buffer[m_addr_offset] = val;
		m_addr_offset++;
		m_addr_received++;
		m_addr_expected--;
		if (m_addr_expected == 0)			set_offset(ns);
	}
}

void CNandChip::DlWrite(BYTE val, int bus_width_shift)
{
	check_not_busy(ns, __LINE__);
	if (m_write_offset < 0 || m_write_offset >= m_buff_size) 
	{
		LOG_DEBUG(L"Write at illegal buffer offset %d",	m_write_offset);
	}
	else if (bus_width_shift == 0) 
	{
		m_buffer[m_write_offset] = val & 0xff;
		m_write_offset++;
	}
	else if (bus_width_shift == 1)
	{
		m_buffer[m_write_offset] = val & 0xff;
		m_write_offset++;
		m_buffer[m_write_offset] = (val >> 8) & 0xff;
		m_write_offset++;
	}

}

CNandChip::CNandChip(void)
	: m_store(NULL), m_buffer(NULL)
{
}

CNandChip::~CNandChip(void)
{
	delete[] m_buffer;
	RELEASE(m_store);
}

UINT32 CNandChip::ReadCycle(void)
{
	unsigned retval;
	//struct nandsim_private *ns =
	//	(struct nandsim_private *)this->private_data;

	if (m_cle || m_ale) 
	{
		LOG_DEBUG(L"Read cycle with CLE %s and ALE %s\n",
			m_cle ? L"high" : L"low",
			m_ale ? L"high" : L"low");
		retval = 0;
	}
	else 
	{
		retval = nandsim_dl_read(m_bus_width_shift);
	}
	LOG_DEBUG(L"Read cycle returning %02X\n", retval);
//	debug(5, "Read cycle returning %02X\n", retval);
	return retval;
}

void CNandChip::WriteCycle(UINT32 b)
{
	//struct nandsim_private *ns =
	//	(struct nandsim_private *)this->private_data;
	const wchar_t *x;

	if (m_ale && m_cle)	x = L"ALE AND CLE";
	else if (m_ale)		x = L"ALE";
	else if (m_cle)		x = L"CLE";
	else				x = L"data";
	LOG_DEBUG(L"Write %02x to %s\n", b, x)
	//debug(5, "Write %02x to %s\n",
	//	b, x);
	if (m_cle && m_ale)	LOG_DEBUG(L"Write cycle with both ALE and CLE high\n")
	else if (m_cle)		nandsim_cl_write(b);
	else if (m_ale)		nandsim_al_write(b);
	else		nandsim_dl_write(b, m_bus_width_shift);

}

int CNandChip::CheckBusy(void)
{
	if (m_busy_count > 0) 
	{
		m_busy_count--;
		LOG_DEBUG(L"Still busy");
		return 1;
	}
	else 
	{
		LOG_DEBUG(L"Not busy");
		return 0;
	}
}

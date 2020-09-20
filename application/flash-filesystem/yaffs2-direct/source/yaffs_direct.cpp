#include "pch.h"

#include "yaffs_object.h"
#include "yaffs_direct.h"
//#include "../yaffs-guts/yaffsfs.h"
#include <boost\cast.hpp>

#include "yaffs_factory.h"

extern "C" {
#include "../yaffs-guts/yaffs_guts.h"
}
LOCAL_LOGGER_ENABLE(L"yaffs_direct", LOGGER_LEVEL_DEBUGINFO);

// driver adaptor
extern "C" {

	struct nand_context 
	{
//		struct nand_chip *chip;
		INandDriver * driver;
		u8 *buffer;
	};


	static inline INandDriver *dev_to_driver(struct yaffs_dev *dev)
	{
		struct nand_context *ctxt =	(struct nand_context *)(dev->driver_context);
		return ctxt->driver;
	}

	static inline u8 *dev_to_buffer(struct yaffs_dev *dev)
	{
		struct nand_context *ctxt = (struct nand_context *)(dev->driver_context);
		return ctxt->buffer;
	}


	static int yaffs_nand_drv_WriteChunk(struct yaffs_dev *dev, int nand_chunk,
		const u8 *data, int data_len, const u8 *oob, int oob_len)
	{
		INandDriver *nand = dev_to_driver(dev);
		bool br = nand->WriteChunk(nand_chunk, data, data_len, oob, oob_len);

		return br? YAFFS_OK : YAFFS_FAIL;
	}

	static int yaffs_nand_drv_ReadChunk(struct yaffs_dev *dev, int nand_chunk,
		u8 *data, int data_len, u8 *oob, int oob_len, enum yaffs_ecc_result *ecc_result_out)
	{
		INandDriver *nand = dev_to_driver(dev);
		INandDriver::ECC_RESULT ecc_result;
		bool br = nand->ReadChunk(nand_chunk, data, data_len, oob, oob_len, ecc_result);
		if (ecc_result_out)	*ecc_result_out = (yaffs_ecc_result)(ecc_result);

#if 0
		/* Do ECC and marshalling */
		if (oob) memcpy(oob, buffer + 26, oob_len);

		ecc_result = YAFFS_ECC_RESULT_NO_ERROR;

		if (data) {
			for (i = 0, e = buffer + 2; i < nand->data_bytes_per_page; i += 256, e += 3) {
				yaffs_ecc_calc(data + i, read_ecc);
				ret = yaffs_ecc_correct(data + i, e, read_ecc);
				if (ret < 0)
					ecc_result = YAFFS_ECC_RESULT_UNFIXED;
				else if (ret > 0 && ecc_result == YAFFS_ECC_RESULT_NO_ERROR)
					ecc_result = YAFFS_ECC_RESULT_FIXED;
			}
		}
		if (ecc_result_out)	*ecc_result_out = ecc_result;
#endif

		return br?YAFFS_OK:YAFFS_FAIL;
	}

	static int yaffs_nand_drv_EraseBlock(struct yaffs_dev *dev, int block_no)
	{
		INandDriver *nand = dev_to_driver(dev);
		bool br = nand->Erase(block_no);
		return br?YAFFS_OK:YAFFS_FAIL;
	}

	static int yaffs_nand_drv_MarkBad(struct yaffs_dev *dev, int block_no)
	{
		INandDriver *nand = dev_to_driver(dev);
		bool br = nand->MarkBad(block_no);
		return br?YAFFS_OK:YAFFS_FAIL;
	}

	static int yaffs_nand_drv_CheckBad(struct yaffs_dev *dev, int block_no)
	{
		INandDriver *nand = dev_to_driver(dev);
		bool br = nand->CheckBad(block_no);
#if 0
		/* Check that bad block marker is not set */
		if (yaffs_hweight8(buffer[0]) + yaffs_hweight8(buffer[1]) < 14)	return YAFFS_FAIL;
		else			return YAFFS_OK;
#endif
		return br?YAFFS_OK:YAFFS_FAIL;

	}

	static int yaffs_nand_drv_Initialise(struct yaffs_dev *dev)
	{
		INandDriver *nand = dev_to_driver(dev);
		(void)nand;
		return YAFFS_OK;
	}

	static int yaffs_nand_drv_Deinitialise(struct yaffs_dev *dev)
	{
		INandDriver *nand = dev_to_driver(dev);
		(void)nand;
		return YAFFS_OK;
	}


	int yaffs_nand_install_drv(struct yaffs_dev *dev, INandDriver *nand)
	{
		struct yaffs_driver *drv = &dev->drv;
		//u8 *buffer = NULL;
		struct nand_context *ctxt = NULL;

		//ctxt = malloc(sizeof(struct nand_context));
		ctxt = new nand_context;
		//buffer = malloc(nand->spare_bytes_per_page);
		//buffer = new BYTE[spare_size];

		if (/*!buffer ||*/ !ctxt) goto fail;

		drv->drv_write_chunk_fn = yaffs_nand_drv_WriteChunk;
		drv->drv_read_chunk_fn = yaffs_nand_drv_ReadChunk;
		drv->drv_erase_fn = yaffs_nand_drv_EraseBlock;
		drv->drv_mark_bad_fn = yaffs_nand_drv_MarkBad;
		drv->drv_check_bad_fn = yaffs_nand_drv_CheckBad;
		drv->drv_initialise_fn = yaffs_nand_drv_Initialise;
		drv->drv_deinitialise_fn = yaffs_nand_drv_Deinitialise;

		//ctxt->nand = nand;
		ctxt->driver = nand;
		//ctxt->buffer = buffer;
		dev->driver_context = (void *)ctxt;
		return YAFFS_OK;

	fail:
		//free(ctxt);
		delete ctxt;
		//free(buffer);
		return YAFFS_FAIL;
	}
}


CYaffsDirect::CYaffsDirect() 
	: m_driver(NULL), m_yaffs_dev(NULL)
	, m_root(NULL)
{
}

CYaffsDirect::~CYaffsDirect()
{
	delete m_yaffs_dev;
	RELEASE(m_driver);
}

void CYaffsDirect::GetRoot(IFileInfo *& root)
{
	CYaffsObject * obj = jcvos::CDynamicInstance<CYaffsObject>::Create();
	JCASSERT(obj);
	obj->Initialize(this, m_root);
	root = static_cast<IFileInfo*>(obj);
}


bool CYaffsDirect::ConnectToDevice(IVirtualDisk * dev)
{
	m_driver = dynamic_cast<INandDriver*>(dev);
	if (m_driver == NULL)
	{
		LOG_ERROR(L"[err] the device is not a INandDeirver!");
		return false;
	}

	//<TODO> 临时处理：创建固定的driver, nand driver作为IVirtualDisk参数传入
	m_driver->AddRef();
	INandDriver::NAND_DEVICE_INFO dev_info;
	m_driver->GetFlashId(dev_info);
	////m_dev->param.inband_tags = ;
	//m_dev->param.total_bytes_per_chunk = dev_info.data_size;
	//m_dev->param.chunks_per_block = dev_info.page_num;
	//m_dev->param.spare_bytes_per_chunk = dev_info.spare_size;

	//m_dev->param.start_block = 1;
	//m_dev->param.end_block = dev_info.block_num - 1;
	//m_dev->param.n_reserved_blocks = 8;
	////m_dev->param.n_caches=;
	//m_dev->param.use_nand_ecc = true;
	//m_dev->param.is_yaffs2 = true;

// create device
	m_yaffs_dev = new yaffs_dev;
	memset(m_yaffs_dev, 0, sizeof(yaffs_dev));
	yaffs_param * param = &(m_yaffs_dev->param);
	param->name = new YCHAR[100];
	wcscpy_s((YCHAR*)param->name, 100, L"Z:\\");

	param->total_bytes_per_chunk = boost::numeric_cast<u32>(dev_info.data_size);
	param->chunks_per_block = boost::numeric_cast<u32>(dev_info.page_num);
	param->n_reserved_blocks = 8;
	param->spare_bytes_per_chunk = boost::numeric_cast<u32>(dev_info.spare_size);
	param->start_block = 1; // First block
	param->end_block = boost::numeric_cast<u32>( dev_info.block_num - 1 ); // Last block
	param->is_yaffs2 = 1;
	param->use_nand_ecc = 1;
	param->n_caches = 0;
	param->stored_endian = 2;

	if (yaffs_nand_install_drv(m_yaffs_dev, m_driver) != YAFFS_OK)	goto fail;
	yaffs_add_device(m_yaffs_dev);
	return true;
fail:
	delete[](m_yaffs_dev->param.name);
	m_yaffs_dev->param.name = NULL;
	return false;
}

void CYaffsDirect::Disconnect(void)
{
	delete[] m_yaffs_dev->param.name;
	m_yaffs_dev->param.name = NULL;
	delete m_yaffs_dev->driver_context;
	m_yaffs_dev->driver_context = 0;
	delete m_yaffs_dev;
	m_yaffs_dev = NULL;
	RELEASE(m_driver);
}

bool CYaffsDirect::Mount(void)
{
	int ir = yaffs_mount(L"Z:\\"); 
	if (ir < 0) return false;
	m_root = m_yaffs_dev->root_dir;
	return true;
}

void CYaffsDirect::Unmount(void)
{
	int ir = yaffs_unmount(L"Z:\\");
}

bool CYaffsDirect::DokanGetDiskSpace(ULONGLONG & free_bytes, ULONGLONG & total_bytes, ULONGLONG & total_free_bytes)
{
	free_bytes = 0;
	total_bytes = 0;
	total_free_bytes = 0;
	if (m_yaffs_dev->is_mounted == 0) return false;
	free_bytes = yaffs_freespace(L"Z:\\");
	total_bytes = yaffs_totalspace(L"Z:\\");
	total_free_bytes = free_bytes;
	return true;
}

bool CYaffsDirect::DokanDeleteFile(const std::wstring & fn, IFileInfo * file, bool isdir)
{
	if (!file)
	{
		bool br = DokanCreateFile(file, fn, GENERIC_READ | GENERIC_WRITE, 0, OPEN_EXISTING, 0, 0, isdir);
		if (!file) return false;
		if (!br)
		{
			file->Release();
			return false;
		}
	}
	else file->AddRef();
	CYaffsObject * obj = dynamic_cast<CYaffsObject*>(file); JCASSERT(obj);
	yaffs_del_obj(obj->m_obj);
	file->Release();
	return true;
}

bool CYaffsDirect::MakeFileSystem(UINT32 volume_size, const std::wstring & volume_name)
{
	int ir = yaffs_format(L"Z:\\", 1, 1, 1);
	return ir >= 0;
}

static bool yaffsdirect_is_path_divider(const YCHAR ch)
{
	const YCHAR *str = YAFFS_PATH_DIVIDERS;
	while (*str) 
	{
		if (*str == ch)		return true;
		str++;
	}
	return false;
}

yaffs_obj * CYaffsDirect::FindDir(yaffs_obj * start_dir, const YCHAR * path,const YCHAR * &name/*, bool & isdir*/)
{
	const YCHAR * rest_of_path = (YCHAR*)path;
	YCHAR str[YAFFS_MAX_NAME_LENGTH + 1];
	int ii;
	//isdir = true;
	while (start_dir) 
	{
		/* parse off /.
		 * curve ball: also throw away surplus '/'
		 * eg. "/ram/x////ff" gets treated the same as "/ram/x/ff"	 */
		while (yaffsdirect_is_path_divider(*rest_of_path))	rest_of_path++;	/* get rid of '/' */
		name = rest_of_path;
		ii = 0;

		while (*rest_of_path && !yaffsdirect_is_path_divider(*rest_of_path)) 
		{
			if (ii < YAFFS_MAX_NAME_LENGTH) 
			{
				str[ii] = *rest_of_path;
				str[ii + 1] = '\0';
				ii++;
			}
			rest_of_path++;
		}

		if (!*rest_of_path)	return start_dir;		/* got to the end of the string */
		else 
		{
			if (yaffs_strcmp(str, _Y(".")) == 0) {	/* Do nothing */}
			else if (yaffs_strcmp(str, _Y("..")) == 0) {	start_dir = start_dir->parent;	}
			else 
			{
				start_dir = yaffs_find_by_name(start_dir, str);
				// 不支持link
				//start_dir = yaffsfs_FollowLink(start_dir, symDepth, loop);
				//if (start_dir && start_dir->variant_type != YAFFS_OBJECT_TYPE_DIRECTORY) 
				//{
				//	//isdir = false;
				//	//if (notDir)		*notDir = 1;
				//	start_dir = NULL;
				//}

			}
		}
	}
	/* directory did not exist. */
	return NULL;
}

bool CYaffsDirect::DokanCreateFile(IFileInfo *& file, const std::wstring & fn, ACCESS_MASK access_mask, DWORD attr, DWORD disp, ULONG share, ULONG opt, bool isdir)
{
	int flag=0;

	//bool is_dir = true;
	const YCHAR * file_name = NULL;
	yaffs_obj * parent = FindDir(m_root, fn.c_str(), file_name);
	if (!parent) return false;									// no parent
//	if (is_dir == false) return false;							// parent is not a dir
	if (parent->variant_type != YAFFS_OBJECT_TYPE_DIRECTORY) return false;
	yaffs_obj * new_obj = NULL;
	if (yaffs_strnlen(file_name, 5) == 0)
	{	// file exist?
		if (disp == OPEN_EXISTING && isdir && fn == L"\\")
		{	// open root
			GetRoot(file);
			return true;
		}
		else
		{
			JCASSERT(0);
			return false;
		}
	}		

	yaffs_obj * exist_obj = yaffs_find_by_name(parent, file_name);

	if (disp == CREATE_NEW)
	{
		if (exist_obj) return false;	// dir exist
		if (isdir)	new_obj = yaffs_create_dir(parent, file_name, 0, 0, 0);
		else new_obj = yaffs_create_file(parent, file_name, 0, 0, 0);
		//if (!new_obj) return false;
	}
	else if (disp == OPEN_EXISTING || disp == TRUNCATE_EXISTING)
	{
		if (!exist_obj) return false;
		new_obj = exist_obj;
		if (disp == TRUNCATE_EXISTING && !isdir) yaffs_resize_file(new_obj, 0);
	}
	else if (disp == CREATE_ALWAYS || disp == OPEN_ALWAYS)
	{
		if (exist_obj)
		{
			new_obj = exist_obj;
			if (disp == OPEN_ALWAYS) yaffs_resize_file(new_obj, 0);
		}
		else
		{
			if (isdir) new_obj = yaffs_create_dir(parent, file_name, 0, 0, 0);
			else		new_obj = yaffs_create_file(parent, file_name, 0, 0, 0);
		}
	}
	else return false;
	
	if (new_obj)
	{
		CYaffsObject * _obj = jcvos::CDynamicInstance<CYaffsObject>::Create();
		JCASSERT(_obj);
		_obj->Initialize(this, new_obj);
		file = static_cast<IFileInfo*>(_obj);
		return true;
	}
	else return false;
}


bool CYaffsFactory::CreateFileSystem(IFileSystem *& fs, const std::wstring & fs_name)
{
	JCASSERT(fs == NULL);
	if (fs_name == L"yaffs2")
	{
		CYaffsDirect *yaffs = jcvos::CDynamicInstance<CYaffsDirect>::Create();
		fs = static_cast<IFileSystem*>(yaffs);
		return true;
	}
	return false;
}


#include "pch.h"
#include "yaffs_object.h"
#include "yaffs_direct.h"
//#include "../yaffs-guts/yaffsfs.h"
extern "C" {
#include "../yaffs-guts/yaffs_guts.h"
}
LOCAL_LOGGER_ENABLE(L"yaffs_object", LOGGER_LEVEL_DEBUGINFO);
#define YAFFS_MAX_RW_SIZE	0x70000000
/* YAFFSFS_RW_SIZE must be a power of 2 */
#define YAFFSFS_RW_SHIFT (13)
#define YAFFSFS_RW_SIZE  (1<<YAFFSFS_RW_SHIFT)

bool CYaffsObject::DokanReadFile(LPVOID vbuf, DWORD len, DWORD & read, LONGLONG offset)
{
	JCASSERT(m_obj);

	struct yaffsfs_FileDes *fd = NULL;
	//struct yaffs_obj *obj = NULL;
	Y_LOFF_T pos = 0;
	Y_LOFF_T startPos = 0;
	Y_LOFF_T endPos = 0;
	int nRead = 0;
	int nToRead = 0;
	int totalRead = 0;
	Y_LOFF_T maxRead;
	u8 *buf = (u8 *)vbuf;
	read = 0;
	//yaffsfs_Lock();
	if (len > YAFFS_MAX_RW_SIZE) return false;
	startPos = offset;
	pos = startPos;
	if (yaffs_get_obj_length(m_obj) > pos)	maxRead = yaffs_get_obj_length(m_obj) - pos;
	else									maxRead = 0;
	if (len > maxRead)		len = maxRead;
	//yaffsfs_GetHandle(handle);
	endPos = pos + len;
	if (pos < 0 || pos > YAFFS_MAX_FILE_SIZE || len > YAFFS_MAX_RW_SIZE ||
		endPos < 0 || endPos > YAFFS_MAX_FILE_SIZE) 
	{
		totalRead = -1;
		len = 0;
	}

	while (len > 0) 
	{
		nToRead = YAFFSFS_RW_SIZE - (pos & (YAFFSFS_RW_SIZE - 1));
		if (nToRead > (int)len) nToRead = len;
		/* Tricky bit...
		 * Need to reverify object in case the device was
		 * unmounted in another thread.		 */
//		m_obj = yaffsfs_HandleToObject(handle);
//		if (!m_obj)	nRead = 0;
//		else
		nRead = yaffs_file_rd(m_obj, buf, pos, nToRead);

		if (nRead > 0) 
		{
			totalRead += nRead;
			pos += nRead;
			buf += nRead;
		}

		if (nRead == nToRead)		len -= nRead;
		else						len = 0;	/* no more to read */
		//if (len > 0) 
		//{
		//	yaffsfs_Unlock();
		//	yaffsfs_Lock();
		//}

	}
	//yaffsfs_Unlock();
	read = (totalRead >= 0) ? totalRead : 0;
	return (totalRead >=0);
}

bool CYaffsObject::DokanWriteFile(const void * vbuf, DWORD nbyte, DWORD & written, LONGLONG offset)
{
	JCASSERT(m_obj);
	//struct yaffsfs_FileDes *fd = NULL;
	//struct yaffs_obj *obj = NULL;
	Y_LOFF_T pos = 0;
	Y_LOFF_T startPos = 0;
	Y_LOFF_T endPos;
	int nWritten = 0;
	int totalWritten = 0;
	int write_trhrough = 0;
	int nToWrite = 0;
	const u8 *buf = (const u8 *)vbuf;
	written = 0;

	//yaffsfs_Lock();
	if (m_obj->my_dev->read_only) return false;
	startPos = offset;
//	yaffsfs_GetHandle(handle);
	pos = startPos;
	endPos = pos + nbyte;
	if (pos < 0 || pos > YAFFS_MAX_FILE_SIZE || nbyte > YAFFS_MAX_RW_SIZE ||
			endPos < 0 || endPos > YAFFS_MAX_FILE_SIZE) 
	{
		totalWritten = -1;
		nbyte = 0;
	}

	while (nbyte > 0) 
	{
		nToWrite = YAFFSFS_RW_SIZE - (pos & (YAFFSFS_RW_SIZE - 1));
		if (nToWrite > (int)nbyte)		nToWrite = nbyte;

		/* Tricky bit...
		 * Need to reverify object in case the device was
		 * remounted or unmounted in another thread.	 */
//		obj = yaffsfs_HandleToObject(handle);
//		if (!obj || obj->my_dev->read_only)
//				nWritten = 0;
//			else
		nWritten = yaffs_wr_file(m_obj, buf, pos, nToWrite, write_trhrough);
		if (nWritten > 0) 
		{
			totalWritten += nWritten;
			pos += nWritten;
			buf += nWritten;
		}

		if (nWritten == nToWrite)		nbyte -= nToWrite;
		else							nbyte = 0;
		if (nWritten < 1 && totalWritten < 1) 
		{
			yaffsfs_SetError(-ENOSPC);
			totalWritten = -1;
		}
		//if (nbyte > 0) 
		//{
		//	yaffsfs_Unlock();
		//	yaffsfs_Lock();
		//}
	}

		//yaffsfs_PutHandle(handle);
	//yaffsfs_Unlock();

	written = (totalWritten >= 0) ? totalWritten : 0;
	return (totalWritten >= 0);
}

bool CYaffsObject::SetAllocationSize(LONGLONG size)
{
	JCASSERT(m_obj);
	int ir = yaffs_resize_file(m_obj, size);
	return ir == YAFFS_OK;
}

bool CYaffsObject::SetEndOfFile(LONGLONG size)
{
	JCASSERT(m_obj);
	int ir = yaffs_resize_file(m_obj, size);
	return ir == YAFFS_OK;
}

void CYaffsObject::GetParent(IFileInfo * & parent)
{
	JCASSERT(parent == NULL);
	jcvos::auto_interface<CYaffsObject> obj;
	GetParent(obj);
	obj.detach<IFileInfo>(parent);
}


void CYaffsObject::GetParent(CYaffsObject* &parent)
{
	JCASSERT(parent == NULL && m_obj);
	yaffs_obj * obj = m_obj->parent;
	if (obj)
	{
		parent = jcvos::CDynamicInstance<CYaffsObject>::Create();
		parent->Initialize(m_fs, obj);
	}
}

bool CYaffsObject::IsDirectory(void) const
{
	JCASSERT(m_obj);
	return m_obj->variant_type == YAFFS_OBJECT_TYPE_DIRECTORY;
}

void CYaffsObject::Initialize(CYaffsDirect * fs, yaffs_obj * obj)
{
	m_fs = fs; m_obj = obj;
}

bool CYaffsObject::GetFileInformation(LPBY_HANDLE_FILE_INFORMATION fileinfo) const
{
	JCASSERT(m_obj);
	fileinfo->dwFileAttributes =0;
	if (m_obj->variant_type != YAFFS_OBJECT_TYPE_FILE)
	{
		fileinfo->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
		fileinfo->nFileSizeHigh = 0;
		fileinfo->nFileSizeLow = 0;
	}
	else
	{
		fileinfo->nFileSizeLow = m_obj->variant.file_variant.file_size;
	}
	return true;
}

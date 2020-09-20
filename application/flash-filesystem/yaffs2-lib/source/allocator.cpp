#include "stdafx.h"
#include "../include/allocator.h"
#include "../include/yaffs_obj.h"
#include "../include/yaffs_file.h"
#include "../include/yaffs_dir.h"


///////////////////////////////////////////////////////////////////////////////
//-- Tnode Allocator

CTnodeAllocator::CTnodeAllocator(void)
	: m_tnodes(NULL), m_free_list(NULL)
{
}

void CTnodeAllocator::Initialize(size_t num, size_t tnode_size)
{
	JCASSERT(num > 0 && m_tnodes == NULL && m_free_list == NULL);
	m_tnode_num = num;
	m_tnodes = new yaffs_tnode[num];
	m_free_list = new PTNODE[num];
	for (size_t ii = 0; ii < num; ++ii)
	{
		m_free_list[ii] = m_tnodes + ii;
	}
	m_free_num = m_tnode_num;
}

void CTnodeAllocator::Deinitialize(void)
{
	delete[] m_tnodes;
	m_tnodes = NULL;
	delete[] m_free_list;
	m_free_list = NULL;
}

yaffs_tnode * CTnodeAllocator::GetTnode(void)
{
	if (m_free_num == 0) THROW_ERROR(ERR_USER, L"tnode allocator: out of memory");
	m_free_num--;
	yaffs_tnode * tn = m_free_list[m_free_num];
	memset(tn, 0, sizeof(yaffs_tnode));
	return tn;
}

void CTnodeAllocator::FreeTnode(yaffs_tnode * &tn)
{
	if (m_free_num >= m_tnode_num) THROW_ERROR(ERR_USER, L"tnode allocator full");
	m_free_list[m_free_num] = tn;
	m_free_num++;
	tn = NULL;
}

///////////////////////////////////////////////////////////////////////////////
//-- Yaffs Object Allocator

typedef CAllocateInstance<CYaffsObject, CYaffsObjAllocator> OBJ_INSTANCE;
typedef CAllocateInstance<CYaffsFile, CYaffsObjAllocator> FILE_INSTANCE;
typedef CAllocateInstance<CYaffsDir, CYaffsObjAllocator> DIR_INSTANCE;

bool CYaffsObjAllocator::Initlaize(size_t num)
{
	JCASSERT(num > 0 && m_objs == NULL && m_free_list == NULL);
	size_t size_obj = sizeof(OBJ_INSTANCE);
	size_t size_file = sizeof(FILE_INSTANCE);
	size_t size_dir = sizeof(DIR_INSTANCE);

	size_t ss = max(size_obj, size_file);
	ss = max(ss, size_dir);
	//ss = ss - 1;
	//ss = ss & (~3);
	//m_object_size = ss + 4;
	// 4×Ö½Ú¶ÔÆë
	m_object_size = ((ss - 1) & (~3)) + 4;
	m_obj_num = num;

	m_objs = new BYTE[m_obj_num * m_object_size];
	m_free_list = new PTR[m_obj_num];
	for (size_t ii = 0; ii < m_obj_num; ++ii)
	{
		m_free_list[ii] = m_objs + (ii*m_object_size);
	}
	m_free_num = m_obj_num;
	return true;
}

void CYaffsObjAllocator::Deinitialize(void)
{
	delete[] m_objs;
	m_objs = NULL;
	delete[] m_free_list;
	m_free_list = NULL;
}

void * CYaffsObjAllocator::AllocObj()
{
	if (m_free_num == 0) THROW_ERROR(ERR_USER, L"object allocator: out of memory");
	m_free_num--;
	BYTE * obj = m_free_list[m_free_num];
	memset(obj, 0, m_object_size);
	return obj;
}

void CYaffsObjAllocator::FreeObj(void * ptr)
{
	if (m_free_num >= m_obj_num) THROW_ERROR(ERR_USER, L"tnode allocator full");
	m_free_list[m_free_num] = (BYTE*)ptr;
	m_free_num++;
	ptr = NULL;
}

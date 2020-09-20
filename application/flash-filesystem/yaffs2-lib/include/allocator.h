#pragma once

#include "yaffs_define.h"

template <class ImpClass, class Allocator>
class CAllocateInstance : public ImpClass
{
public:
	CAllocateInstance<ImpClass, Allocator>(Allocator & alloc) : m_ref(1), m_alloc(alloc) {};
	virtual ~CAllocateInstance<ImpClass, Allocator>(void) {};
	static ImpClass * Create(Allocator & alloc)
	{
		void * mem = alloc.AllocObj();
		CAllocateInstance<ImpClass, Allocator> * obj = new(mem) CAllocateInstance<ImpClass, Allocator>(alloc);
		return static_cast<ImpClass*>(obj);
	}
protected:
	mutable __declspec(align(4))	long	m_ref;
	Allocator & m_alloc;
public:
	inline virtual void AddRef() { LockedIncrement(m_ref); }
	inline virtual void Release(void)
	{
		if (LockedDecrement(m_ref) == 0)
		{
			this->~CAllocateInstance<ImpClass, Allocator>();
			m_alloc.FreeObj(this);
		}
	}
	virtual bool QueryInterface(const char * if_name, IJCInterface * &if_ptr) { return false; }
};

class CYaffsObjAllocator
{
	typedef BYTE * PTR;
public:
	bool Initlaize(size_t num);
	void Deinitialize(void);
	void * AllocObj();
	void FreeObj(void * ptr);
protected:
	BYTE * m_objs;
	size_t m_obj_num;
	PTR * m_free_list;
	size_t m_free_num;

	size_t m_object_size;
};


class CTnodeAllocator
{
	typedef yaffs_tnode * PTNODE;
public:
	CTnodeAllocator(void);
public:
	void Initialize(size_t num, size_t tnode_size);
	void Deinitialize(void);
	yaffs_tnode * GetTnode(void);
	void FreeTnode(yaffs_tnode * &tn);
#ifdef _DEBUG
public:
#else
protected:
#endif
	PTNODE m_tnodes;
	size_t m_tnode_num;
	PTNODE* m_free_list;
	size_t m_free_num;
};

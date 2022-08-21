///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ==== atomic ====


// 原子变量采用interallocked处理
typedef long atomic_t;
typedef long atomic_long_t;
typedef LONG64 atomic64_t;

#define atomic_set(a, b)			InterlockedExchange(a,b)
#define atomic64_set(a, b)			InterlockedExchange64(a, b)
#define atomic_inc(x)				InterlockedIncrement(x)
#define atomic_inc_return(x)		InterlockedIncrement(x)
#define atomic_dec(x)				InterlockedDecrement(x)
#define atomic_dec_and_test(x)		InterlockedDecrement(x)
#define atomic_long_inc(x)			InterlockedIncrement(x)
#define atomic_long_dec(x)			InterlockedDecrement(x)
#define atomic_long_sub_return(a, x)	InterlockedAdd(x, (0-a))
#define atomic_add(a, x)			InterlockedAdd(x, a)
#define atomic_sub(a, x)			InterlockedAdd(x, (0-a))
#define atomic_read(x)				InterlockedAdd(x, 0)
#define atomic_long_read(x)			InterlockedAdd(x, 0)

#define cmpxchg(x, cmp, exch)		InterlockedCompareExchange(x, exch, cmp)



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ==== critical section ====
#define spinlock_t					CRITICAL_SECTION

#define spin_lock_init(x)			InitializeCriticalSection(x)
#define spin_lock_del(x)			DeleteCriticalSection(x)
#define spin_lock(x)				EnterCriticalSection(x)
#define spin_unlock(x)				LeaveCriticalSection(x)
#define spin_lock_irqsave(x,f)		EnterCriticalSection(x)
#define spin_unlock_irqrestore(x,f)	LeaveCriticalSection(x)
#define spin_lock_irq(x,f)			EnterCriticalSection(x)
#define spin_unlock_irq(x,f)		LeaveCriticalSection(x)
#define spin_trylock(x)				TryEnterCriticalSection(x)


#define write_lock(x)				EnterCriticalSection(x)
#define write_unlock(x)				LeaveCriticalSection(x)
#define rwlock_init(x)				InitializeCriticalSection(x)

#define seqlock_t			CRITICAL_SECTION
inline void write_seqlock(seqlock_t* x) { EnterCriticalSection(x); }
inline void write_sequnlock(seqlock_t* x) { LeaveCriticalSection(x); }

// seqcount_spinlock_t时Linux的顺序锁，对写入保护。
#define seqcount_spinlock_t	CRITICAL_SECTION
inline void	raw_write_seqcount_begin(seqcount_spinlock_t* x) { EnterCriticalSection(x); }
inline void raw_write_seqcount_end(seqcount_spinlock_t* x) { LeaveCriticalSection(x); }

// == rwlock_t
#define rwlock_t			CRITICAL_SECTION
#define read_lock(x)		EnterCriticalSection(x)
#define read_unlock(x)		LeaveCriticalSection(x)
inline bool write_trylock(rwlock_t* x) { EnterCriticalSection(x); return true; }

inline int atomic_dec_and_lock(atomic_t* a, spinlock_t* l)
{
	long c = atomic_dec(a);
	if (c != 0) return 0;

	spin_lock(l);
	if (c) return c;
	spin_unlock(l);
	return 0;
}




// ==== auto locker for critical section
class spin_locker
{
public:
	spin_locker(spinlock_t& locker) :m_locker(locker) { /*m_locker = const_cast<spinlock_t*>(&locker);*/ }
	void lock(void) { spin_lock(&m_locker); }
	void unlock(void) { spin_unlock(&m_locker); }
protected:
	spinlock_t& m_locker;
};

class CriticalSection
{
public:
	CriticalSection(CRITICAL_SECTION& locker) :m_locker(locker) { /*m_locker = const_cast<spinlock_t*>(&locker);*/ }
	void lock(void) { EnterCriticalSection(&m_locker); }
	void unlock(void) { LeaveCriticalSection(&m_locker); }
protected:
	CRITICAL_SECTION& m_locker;
};



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ==== Read write semaphore ====

//#define init_rwsem(x)	{HANDLE * hh = x; *hh = CreateSemaphore(NULL, 1, 1, NULL);}
// rw_semahpre => SRWLOCK
#define rw_semaphore SRWLOCK
#define semaphore SRWLOCK
#define init_rwsem(x)	InitializeSRWLock(x)
#define down_write(x)	AcquireSRWLockExclusive(x)
#define up_write(x)		ReleaseSRWLockExclusive(x)
#define down_read(x)	AcquireSRWLockShared(x)
#define up_read(x)		ReleaseSRWLockShared(x)
inline int down_write_trylock(SRWLOCK* sem) { return TryAcquireSRWLockExclusive(sem); }
inline int down_read_trylock(SRWLOCK* sem) { return TryAcquireSRWLockShared(sem); }

class semaphore_read_lock
{
public:
	semaphore_read_lock(rw_semaphore& sem) : m_sem(sem) {}
	void lock(void) { down_read(&m_sem); }
	void unlock(void) { up_read(&m_sem); }
protected:
	rw_semaphore & m_sem;
};




// completion => 用event代替
typedef HANDLE completion;
inline void init_completion(HANDLE* c)
{
	JCASSERT(c);
	*c = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (*c == NULL || *c == INVALID_HANDLE_VALUE) THROW_WIN32_ERROR(L"failed on creating completion");
}

inline void wait_for_completion(HANDLE* c)
{
	JCASSERT(c);
	DWORD ir = WaitForSingleObject(*c, INFINITE);
	if (ir != 0) THROW_WIN32_ERROR(L"failed on waiting for completion event");
}

inline void wait_for_completion_io(HANDLE* c)
{
	JCASSERT(c);
	DWORD ir = WaitForSingleObject(*c, INFINITE);
	if (ir != 0) THROW_WIN32_ERROR(L"failed on waiting for completion event");
}

inline void complete(HANDLE* c)
{
	JCASSERT(c);
	BOOL br = SetEvent(*c);
	if (!br) THROW_WIN32_ERROR(L"failed on set completion event");
}

inline void complete_all(HANDLE* c)
{
	JCASSERT(c);
	BOOL br = SetEvent(*c);
	if (!br) THROW_WIN32_ERROR(L"failed on set completion event");
}





inline void write_seqcount_begin_nested(seqcount_spinlock_t* x, int cc) { raw_write_seqcount_begin(x); }
inline void write_seqcount_begin(seqcount_spinlock_t* x) { raw_write_seqcount_begin(x); }
inline void write_seqcount_end(seqcount_spinlock_t* x) { raw_write_seqcount_end(x); }



// == mutex
#define mutex				HANDLE
inline bool mutex_trylock(HANDLE* mm)
{
	DWORD ir = WaitForSingleObject(*mm, 0);
	return (ir == 0);
}
inline void mutex_init(mutex* hh) { JCASSERT(*hh == NULL);	*hh = CreateMutex(NULL, FALSE, NULL); }
inline void mutex_destory(mutex* hh) { CloseHandle(*hh); *hh = NULL; }
inline bool mutex_is_locked(mutex* mm)
{
	DWORD ir = WaitForSingleObject(*mm, 0);
	return (ir == WAIT_TIMEOUT);
}
//#define mutex_init(x)	{HANDLE * hh = x; *hh = CreateMutex(NULL, FALSE, NULL);}
#define mutex_lock(x)	{DWORD ir = WaitForSingleObject(*x, INFINITE); if (ir!=0) THROW_WIN32_ERROR(L"failed on waiting " #x); }
#define mutex_unlock(x)	{ReleaseMutex(*x);}

class mutex_locker
{
public:
	mutex_locker(mutex& mm): m_mutex(mm) {}
	void lock(void) { mutex_lock(&m_mutex); }
	void unlock(void) { mutex_unlock(&m_mutex); }
protected:
	mutex& m_mutex;
};



struct lockref
{
	spinlock_t lock;
	int count;
};


inline void spin_lock_nested(spinlock_t* ll, int cc)
{

}


inline void lockref_get(lockref* ll)
{
	spin_lock(&ll->lock);
	ll->count++;
	spin_unlock(&ll->lock);
}

// ==== per cpu count ====
//<YUAN>简化per cpu count设计，将其是为atomic
struct percpu_counter
{
	INT64 count;
};

inline void percpu_counter_add(percpu_counter* c, INT64 v) { InterlockedAdd64(&c->count, v); }
inline void percpu_counter_add_batch(percpu_counter * c, INT64 v, INT64 batch) { InterlockedAdd64(&c->count, v); }
inline void percpu_counter_sub(percpu_counter* c, INT64 v) { InterlockedAdd64(&c->count, (-v)); }
inline void percpu_counter_inc(percpu_counter* c) { InterlockedIncrement64(&c->count); }
inline void percpu_counter_dec(percpu_counter* c) { InterlockedDecrement64(&c->count); }
inline void percpu_counter_set(percpu_counter* c, INT64 v)	{ InterlockedExchange64(&c->count, v); }
inline s64  percpu_counter_sum_positive(percpu_counter* c) {
	s64 cc = c->count; return max(cc, 0);
}



template <class T> 
class auto_lock
{
public:
	template <class LOCKER_T>
	auto_lock(LOCKER_T& locker) : m_locker(locker) { m_locker.lock(); };
	~auto_lock(void) { m_locker.unlock(); }
protected:
	T m_locker;
};

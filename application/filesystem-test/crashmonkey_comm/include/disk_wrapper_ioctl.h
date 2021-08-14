#ifndef HELLOW_IOCTL_H
#define HELLOW_IOCTL_H

#define HWM_LOG_OFF               0xff00
#define HWM_LOG_ON                0xff01
#define HWM_GET_LOG_META          0xff02
#define HWM_GET_LOG_DATA          0xff03
#define HWM_NEXT_ENT              0xff04
#define HWM_CLR_LOG               0xff05
#define HWM_CHECKPOINT            0xff06
#define HWM_GET_LOG_ALL			  0xff07

#define COW_BRD_SNAPSHOT          0xff06
#define COW_BRD_UNSNAPSHOT        0xff07
#define COW_BRD_RESTORE_SNAPSHOT  0xff08
#define COW_BRD_WIPE              0xff09
// 获取snapshot drive，低字节为drive 编号
#define COW_BRD_GET_SNAPSHOT        0xFE00

// Defines that are separate from the kernel because these values aren't stable.
// Based on 4.4 kernel flags. Comments below sourced from 4.4 Linux kernel.
//
// TODO(ashmrtn): These should be narrowed down to just the flags that we
// actually care about.
enum flag_shifts
{
	// Common flags.
	REQ_WRITE_,               // 00 Set for write, else read.
	REQ_FAILFAST_DEV_,        // 01 No driver retries for device errors.
	REQ_FAILFAST_TRANSPORT_,  // 02 No driver retries for transport errors.
	REQ_FAILFAST_DRIVER_,     // 03 No driver retires for driver errors.

	REQ_SYNC_,                // 04 Sync request, process waiting on IO?
	REQ_META_,                // 05 Metadata IO request. Appears to be for things like inode data and journal data.
	REQ_PRIO_,                // 06 Boost priority in CFQ.
	REQ_DISCARD_,             // 07 Request to discard sectors.

	REQ_SECURE_,              // 08 Used with REQ_DISCARD_.
	REQ_WRITE_SAME_,          // 09 Write the same block many times.
	REQ_NOIDLE_,              // 10 Don't wait for more IO after this one.
	REQ_INTEGRITY_,           // 11 IO includes block integrity payload.

	REQ_FUA_,                 // 12 Forced Unit Access.
	REQ_FLUSH_,               // 13 Cache flush.
	// Bio only flags.
	REQ_READAHEAD_,           // 14 Can fail anytime.
	REQ_THROTTLED_,           // 15 Already throttled, don't throttle again.

	// Request only flags.
	REQ_SORTED_,              // 16 Elevator knows about this request.
	REQ_SOFTBARRIER_,         // 17 May not be passed when reordering by ioscheduler.
	REQ_NOMERGE_,             // 18 Don't merge this with others.
	REQ_STARTED_,             // 19 Drive may have already started this.

	REQ_DONTPREP_,            // Don't call prep on this one.
	REQ_QUEUED_,              // Uses queuing.
	REQ_ELVPRIV_,             // Elevator private data attached.
	REQ_FAILED_,              // Set if request failed.

	REQ_QUIET_,               // Don't worry about errors.
	REQ_PREEMPT_,             // Set for "ide_preempt" and when SCSI "quiesce" should be ignored.
	REQ_ALLOCED_,             // Request came from alloc pool.
	REQ_COPY_USER_,           // Contains copies of user pages.

	REQ_FLUSH_SEQ_,           // Request for flush sequence. Appears to be part of sequence for merged flushes (see comment in https://patchwork.kernel.org/patch/498741/).
	REQ_IO_STAT_,             // Account for IO stat.
	REQ_MIXED_MERGE_,         // Merge of different types, fail separately.
	REQ_PM_,                  // Runtime pm request.
	
	REQ_HASHED_,              // On IO scheduler merge hash.
	REQ_MQ_INFLIGHT_,         // Track inflgiht for MQ (multi-queue?).
	REQ_NO_TIMEOUT_,          // Requests never expire.

	REQ_OP_WRITE_ZEROES_,     // Write the zero filled sector many times (4.15+).

	REQ_NR_BITS_,             // No longer accurate according to the number of bits the kernel uses.
};

#define HWM_WRITE_FLAG (1ULL << REQ_WRITE_)
#define HWM_FAILFAST_DEV_FLAG (1ULL << REQ_FAILFAST_DEV_)
#define HWM_FAILFAST_TRANSPORT_FLAG (1ULL << REQ_FAILFAST_TRANSPORT_)
#define HWM_FAILFAST_DRIVER_FLAG (1ULL << REQ_FAILFAST_DRIVER_)

#define HWM_SYNC_FLAG (1ULL << REQ_SYNC_)
#define HWM_META_FLAG (1ULL << REQ_META_)
#define HWM_PRIO_FLAG (1ULL << REQ_PRIO_)
#define HWM_DISCARD_FLAG (1ULL << REQ_DISCARD_)

#define HWM_SECURE_FLAG (1ULL << REQ_SECURE_)
#define HWM_WRITE_SAME_FLAG (1ULL << REQ_WRITE_SAME_)
#define HWM_NOIDLE_FLAG (1ULL << REQ_NOIDLE_)
#define HWM_INTEGRITY_FLAG (1ULL << REQ_INTEGRITY_)

#define HWM_FUA_FLAG (1ULL << REQ_FUA_)
#define HWM_FLUSH_FLAG (1ULL << REQ_FLUSH_)
#define HWM_READAHEAD_FLAG (1ULL << REQ_READAHEAD_)
#define HWM_THROTTLED_FLAG (1ULL << REQ_THROTTLED_)

#define HWM_SORTED_FLAG (1ULL << REQ_SORTED_)
#define HWM_SOFTBARRIER_FLAG (1ULL << REQ_SOFTBARRIER_)
#define HWM_NOMERGE_FLAG (1ULL << REQ_NOMERGE_)
#define HWM_STARTED_FLAG (1ULL << REQ_STARTED_)

#define HWM_DONTPREP_FLAG (1ULL << REQ_DONTPREP_)
#define HWM_QUEUED_FLAG (1ULL << REQ_QUEUED_)
#define HWM_ELVPRIV_FLAG (1ULL << REQ_ELVPRIV_)
#define HWM_FAILED_FLAG (1ULL << REQ_FAILED_)

#define HWM_QUIET_FLAG (1ULL << REQ_QUIET_)
#define HWM_PREEMPT_FLAG (1ULL << REQ_PREEMPT_)
#define HWM_ALLOCED_FLAG (1ULL << REQ_ALLOCED_)
#define HWM_COPY_USER_FLAG (1ULL << REQ_COPY_USER_)

#define HWM_FLUSH_SEQ_FLAG (1ULL << REQ_FLUSH_SEQ_)
#define HWM_IO_STAT_FLAG (1ULL << REQ_IO_STAT_)
#define HWM_MIXED_MERGE_FLAG (1ULL << REQ_MIXED_MERGE_)
#define HWM_PM_FLAG (1ULL << REQ_PM_)

#define HWM_HASHED_FLAG (1ULL << REQ_HASHED_)
#define HWM_MQ_INFLIGHT_FLAG (1ULL << REQ_MQ_INFLIGHT_)
#define HWM_NO_TIMEOUT_FLAG (1ULL << REQ_NO_TIMEOUT_)

// Kernel 4.15+.
#define HWM_WRITE_ZEROES_FLAG (1ULL << REQ_OP_WRITE_ZEROES_)

#define HWM_CHECKPOINT_FLAG       (1ULL << 63)

// For ease of transferring data to user-land.
struct disk_write_op_meta
{
	unsigned long long  bi_flags;
	unsigned long long  bi_rw;
	unsigned long       write_sector;
	unsigned int        size = 0;       // <YUAN> size修改为sector单位
	unsigned long long  time_ns;        // windows下为system tick

};

#endif

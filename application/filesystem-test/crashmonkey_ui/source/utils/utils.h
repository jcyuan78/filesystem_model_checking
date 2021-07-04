#ifndef UTILS_H
#define UTILS_H

#include <fstream>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

//#include "../disk_wrapper_ioctl.h"
#include <crashmonkey_drive.h>

// �ڲ������������д�С��
inline uint64_t htobe64(uint64_t a)
{
	return a;
}
inline uint64_t be64toh(uint64_t a)
{
	return a;
}

inline uint16_t htobe16(uint16_t a)
{
	return a;
}

inline uint16_t be16toh(uint16_t a)
{
	return a;
}

namespace fs_testing {
	namespace utils {

		class disk_write
		{
		public:
			disk_write();
			disk_write(const struct disk_write_op_meta& m, const BYTE* d);

			struct disk_write_op_meta metadata;

			friend bool operator==(const disk_write& a, const disk_write& b);
			friend bool operator!=(const disk_write& a, const disk_write& b);

			bool has_write_flag();
			bool is_barrier();
			bool is_async_write();
			bool is_checkpoint();
			bool is_meta();
			bool has_flush_flag();
			bool has_flush_seq_flag();
			bool has_FUA_flag();
			void set_flush_flag();
			void set_flush_seq_flag();
			void clear_flush_flag();
			void clear_flush_seq_flag();


			static std::wstring flags_to_string(long long flags);
			static void serialize(std::wofstream& fs, const disk_write& dw);
			static disk_write deserialize(std::ifstream& is);

			// Returns a pointer to the data which was assigned or NULL if data could not be assigned. 
			// Pointer is valid only as long as the object exists. The user
			// should not call free on this pointer or otherwise attempt memory management of it.
			std::shared_ptr<BYTE> set_data(const BYTE* data);
			// Returns a pointer to the data field or NULL if data has not been assigned.
			// Pointer is valid only as long as the object exists or otherwise attempt
			// memory management of it.
			std::shared_ptr<BYTE> get_data();
			void clear_data();

		private:
			std::shared_ptr<BYTE> data;
		};


		bool operator==(const disk_write& a, const disk_write& b);
		bool operator!=(const disk_write& a, const disk_write& b);
		std::wostream& operator<<(std::wostream& os, const disk_write& dw);


		/*
		 * Describes data to be written out to the crash state. Can contain data from
		 * either a bio or a sector of a bio. This used instead of returning a
		 * vector of strictly one or the other structs because some workloads have large
		 * bios that cause considerable slowdowns when written back out to disk if
		 * described solely with sectors.
		 */
		struct DiskWriteData
		{
		public:
			DiskWriteData();
			DiskWriteData(bool full_bio, unsigned int bio_index,
				unsigned int bio_sector_index, unsigned int disk_offset,
				unsigned int size, std::shared_ptr<BYTE> data_base,
				unsigned int data_offset);

			void* GetData();
			// Denotes whether or not this represents the entire epoch_op and not just one
			// sector in it.
			bool full_bio;
			unsigned int bio_index;
			// If this is a single sector in the epoch_op, which sector in that epoch_op
			// is it?
			unsigned int bio_sector_index;
			unsigned int disk_offset;
			unsigned int size;

		private:
			// Pointer to the start of the data region for this data. There could still be
			// an offset added to this to get to the actual data that this struct
			// describes. However, to reduces copies and simplify pointer management, a
			// single shared_ptr describes all the data in a bio, meaning that if we want
			// to ensure our pointer is valid at all times we also need a shared_ptr to
			// all the data in the bio.
			std::shared_ptr<BYTE> data_base_;
			unsigned int data_offset_;
		};

	}  // namespace utils
}  // namespace fs_testing
#endif

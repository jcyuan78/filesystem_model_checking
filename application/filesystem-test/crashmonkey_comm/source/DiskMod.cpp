#include "pch.h"
#include "../include/DiskMod.h"

#include <assert.h>
#include <string.h>
#include <iomanip>

LOCAL_LOGGER_ENABLE(L"crashmonke.diskmod", LOGGER_LEVEL_DEBUGINFO);


// 内部处理，忽略所有大小端
static uint64_t htobe64(uint64_t a){	return a;}
static uint64_t be64toh(uint64_t a){	return a;}
static uint16_t htobe16(uint16_t a){	return a;}
static uint16_t be16toh(uint16_t a){	return a;}


using fs_testing::utils::DiskMod;

using std::shared_ptr;
using std::vector;

uint64_t DiskMod::GetSerializeSize()
{
	// mod_type, mod_opts, and a uint64_t for the size of the serialized mod.
	uint64_t res = (2 * sizeof(uint16_t)) + sizeof(uint64_t);
	if (mod_type == DiskMod::kCheckpointMod || mod_type == DiskMod::kSyncMod) { return res; }
	res += sizeof(bool);  // directory_mod.
	res += (path.size() + 1) * sizeof(wchar_t);  // size() doesn't include null terminator.

	if (mod_type == DiskMod::kFsyncMod || mod_type == DiskMod::kRemoveMod || mod_type == DiskMod::kCreateMod)
	{
		return res;
	}

	if (mod_type == DiskMod::kSyncFileRangeMod ||		mod_opts == DiskMod::kFallocateOpt ||
		mod_opts == DiskMod::kFallocateKeepSizeOpt ||	mod_opts == DiskMod::kPunchHoleKeepSizeOpt ||
		mod_opts == DiskMod::kCollapseRangeOpt ||		mod_opts == DiskMod::kZeroRangeOpt ||
		mod_opts == DiskMod::kZeroRangeKeepSizeOpt ||	mod_opts == DiskMod::kInsertRangeOpt)
	{	// Do not contain the data for the range, just the offset and length.
		res += 2 * sizeof(uint64_t);
		return res;
	}

		// Path changed in directory.
	if (directory_mod)	{	res += (directory_added_entry.size() + 1) * sizeof(wchar_t);  	}
	else
	{   // Data changed, location of change, length of change.
		res += 2 * sizeof(uint64_t);
		return res + m_file_mod_len;
	}
	return res;
}

/*
 * The basic serialized format is as follows:
 *    * uint64_t size of following region in bytes
 *    * uint16_t mod_type
 *    * uint16_t mod_opts
 *    ~~~~~~~~~~~~~~~~~~~~    <-- End of entry if kCheckpointMod.
 *    * null-terminated string for path the mod refers to (ex. file path)
 *    * 1-byte directory_mod boolean
 *    ~~~~~~~~~~~~~~~~~~~~    <-- End of ChangeHeader function data.
 *    * uint64_t m_file_mod_location
 *    * uint64_t file_mod_len
 *    * <file_mod_len>-bytes of file mod data
 *
 * The final three lines of this layout are specific only to modifications on files. Modifications to directories are
	not yet supported, though there are some structures that may be used to help track them.
 *
 * All multi-byte data fields in the serialized format use big endian encoding.
 */

 /*
  * Make things miserable on myself, and only store either the directory or the
  * file fields based on the value of directory_mod.
  *
  * Convert everything to big endian for the sake of reading dumps in a consistent manner if need be.
  */
shared_ptr<BYTE> DiskMod::Serialize(/*DiskMod& dm,*/ unsigned long long* size)
{
	// Standard code to serialize the front part of the DiskMod. Get a block large enough for this DiskMod.
	const uint64_t mod_size = GetSerializeSize();
#ifdef _DEBUG
	static int index = 0;
#endif
	//LOG_DEBUG(L"Mod[%d] lengt=%d, type=%s(%d), opt=%s(%d), path=%s, dir=%d, data offset=%llX, data len=%llX",
	//	index++, TypeToString(), (int)mod_type, OptToString(), (int)mod_opts, path.c_str(), directory_mod, 
	//	m_file_mod_location, m_file_mod_len);
	LOG_DEBUG(L"Mod[%d] lengt=%lld, type=%s(%d), opt=%s(%d)",
		index++, mod_size, TypeToString(), (int)mod_type, OptToString(), (int)mod_opts);
	LOG_DEBUG(L"path=%s, dir=%d, data offset=%llX, data len=%llX",
		path.c_str(), directory_mod, m_file_mod_location, m_file_mod_len);
	if (size != nullptr)	{		*size = mod_size;	}
	// TODO(ashmrtn): May want to split this if it is very large.
	shared_ptr<BYTE> res_ptr(new (std::nothrow) BYTE[mod_size], [](BYTE* c) {delete[] c; });
#ifdef _DEBUG
	memset(res_ptr.get(), 0xAA, mod_size);
#endif
	BYTE* buf = res_ptr.get();
	if (buf == nullptr)	{		return res_ptr;	}

	size_t buf_size = mod_size;
	const uint64_t mod_size_be = htobe64(mod_size);
	memcpy_s(buf, buf_size, &mod_size_be, sizeof(uint64_t));
	unsigned int buf_offset = sizeof(uint64_t);
	buf_size -= sizeof(uint64_t);

	int res = SerializeHeader(buf, buf_size, buf_offset);
	if (res < 0)	{		return shared_ptr<BYTE>(nullptr);	}
	buf_offset += res;

	// kCheckpointMod and kSyncMod don't need anything done after the type.
	if (!(mod_type == DiskMod::kCheckpointMod ||	mod_type == DiskMod::kSyncMod))
	{
		res = SerializeChangeHeader(buf, buf_size, buf_offset);
		if (res < 0)		{			return shared_ptr<BYTE>(nullptr);		}
		//<YUAN>源代码中的BUG，计算buf长度时，跳过RemoveMod的情况，但是复制数据时，没有跳过RemoveMod的情况。
		if (mod_type == DiskMod::kFsyncMod || mod_type == DiskMod::kRemoveMod 
			|| mod_type == DiskMod::kCreateMod)	{	return res_ptr;	}

		buf_offset += res;
		if (directory_mod)
		{	// We changed a directory, only put that down.
			res = SerializeDirectoryMod(buf, buf_size, buf_offset);
			if (res < 0)	{	return shared_ptr<BYTE>(nullptr);	}
			buf_offset += res;
		}
		else
		{
			// TODO(ashmrtn): *Technically* fallocate and friends can be called on a directory file descriptor. 
			// The current code will not play well with that. We changed a file, only put that down.
			res = SerializeDataRange(buf, buf_size, buf_offset);
			if (res < 0) { return shared_ptr<BYTE>(nullptr); }
			buf_offset += res;
		}
	}
	return res_ptr;
}

int DiskMod::SerializeHeader(BYTE* buf, size_t & buf_size, const unsigned int buf_offset/*, DiskMod& dm*/)
{
	buf = buf + buf_offset;
	uint16_t _mod_type = htobe16((uint16_t)mod_type);
	uint16_t _mod_opts = htobe16((uint16_t)mod_opts);
	memcpy_s(buf, buf_size, &_mod_type, sizeof(uint16_t));
	buf_size -= sizeof(uint16_t);
	buf += sizeof(uint16_t);
	memcpy_s(buf, buf_size, &_mod_opts, sizeof(uint16_t));
	buf_size -= sizeof(uint16_t);
	return 2 * sizeof(uint16_t);
}

size_t DiskMod::SerializeChangeHeader(BYTE* buf, size_t & buf_size, const unsigned int buf_offset/*, DiskMod& dm*/)
{
	size_t res = 0;
	buf += buf_offset;
	size_t size = path.size() + 1;
	// Add the path that was changed to the buffer.
	// size() doesn't include null-terminator.
	// TODO(ashmrtn): The below assumes 1 character per byte encoding.
	res = size * sizeof(wchar_t);
	memcpy_s(buf, buf_size, path.c_str(), res);
	buf_size -= res;
	buf += res;

	// Add directory_mod to buffer.
	uint8_t mod_directory_mod = directory_mod;
	memcpy_s(buf, buf_size, &mod_directory_mod, sizeof(uint8_t));
	buf_size -= sizeof(uint8_t);
	buf += sizeof(uint8_t);		//<YUAN>这里不是必要的，以后代码优化，考虑在函数内更新buf.
	res += sizeof(uint8_t);
	return res;
}

int DiskMod::SerializeDataRange(BYTE* buf, size_t & buf_size, const unsigned int buf_offset/*, DiskMod& dm*/)
{
	buf += buf_offset;
	// Add m_file_mod_location.
	uint64_t file_mod_location = htobe64(m_file_mod_location);
	memcpy_s(buf, buf_size, &file_mod_location, sizeof(uint64_t));
	buf_size -= sizeof(uint64_t);
	buf += sizeof(uint64_t);

	// Add file_mod_len.
	uint64_t file_mod_len = htobe64(m_file_mod_len);
	memcpy_s(buf, buf_size, &file_mod_len, sizeof(uint64_t));
	buf_size -= sizeof(uint64_t);
	buf += sizeof(uint64_t);

	if (mod_type == DiskMod::kSyncFileRangeMod ||		mod_opts == DiskMod::kFallocateOpt ||
		mod_opts == DiskMod::kFallocateKeepSizeOpt ||	mod_opts == DiskMod::kPunchHoleKeepSizeOpt ||
		mod_opts == DiskMod::kCollapseRangeOpt ||		mod_opts == DiskMod::kZeroRangeOpt ||
		mod_opts == DiskMod::kZeroRangeKeepSizeOpt ||	mod_opts == DiskMod::kInsertRangeOpt)
	{	// kSyncFileRangeMod does not contain the data range, just the offset and length.
		return 2 * sizeof(uint64_t);
	}

	// Add file_mod_data (non-null terminated).
	memcpy_s(buf, buf_size, file_mod_data.get(), m_file_mod_len);
	buf_size -= m_file_mod_len;
	return (2 * sizeof(uint64_t)) + m_file_mod_len;
}

int DiskMod::SerializeDirectoryMod(BYTE* buf, size_t & buf_size, const unsigned int buf_offset)
{
	assert(0 && "Not implemented");
	return 0;
}

const wchar_t* fs_testing::utils::DiskMod::TypeToString(void) const
{
	switch (mod_type)
	{
	case kCreateMod:		return L"kCreateMod";     // File or directory created.
	case kDataMetadataMod:	return L"kDataMetadataMod";   
	case kMetadataMod:		return L"kMetadataMod";       // Only file metadata changed.
	case kDataMod:			return L"kDataMod";           // Only file data changed.
	case kDataMmapMod:		return L"kDataMmapMod";       // Only file data changed via mmap.
	case kRemoveMod:		return L"kRemoveMod";         // File or directory removed.
	case kCheckpointMod:	return L"kCheckpointMod";     // CrashMonkey Checkpoint() marker.
	case kFsyncMod:			return L"kFsyncMod";         
	case kSyncMod:			return L"kSyncMod";           // sync: return L""; flushes all the contents.
	case kSyncFileRangeMod: return L"kSyncFileRangeMod";  
	default:				return L"Unkonw";
	};
}

const wchar_t* fs_testing::utils::DiskMod::OptToString(void) const
{
	// TODO(ashmrtn): Figure out how to handle permissions.
	switch (mod_opts)
	{
	case kNoneOpt :					return L"kNoneOpt";           // No special flags given.
	case kTruncateOpt:				return L"kTruncateOpt";           // ex. truncate on open.
	case kFallocateOpt:				return L"kFallocateOpt";          // For regular fallocate.
	case kFallocateKeepSizeOpt:		return L"kFallocateKeepSizeOpt";  
	case kPunchHoleKeepSizeOpt:		return L"kPunchHoleKeepSizeOpt";  // Implies keep_size.
	case kCollapseRangeOpt:			return L"kCollapseRangeOpt";      // Cannot have keep_size.
	case kZeroRangeOpt:				return L"kZeroRangeOpt";          // Does not have keep_size.
	case kZeroRangeKeepSizeOpt:		return L"kZeroRangeKeepSizeOpt";  // Does have keep_size.
	case kInsertRangeOpt:			return L"kInsertRangeOpt";        // Cannot have keep_size.
	case kMsAsyncOpt:				return L"kMsAsyncOpt";
	case kMsSyncOpt:				return L"kMsSyncOpt";             // Waits for sync to complete so ok.
	default:						return L"Unkonw";
	};

}

int DiskMod::Deserialize(shared_ptr<BYTE> data, DiskMod& res)
{
	res.Reset();

	// Skip the first uint64 which is the size of this region. This is a blind deserialization of the object!
	BYTE* data_ptr = data.get();
	data_ptr += sizeof(uint64_t);

	uint16_t mod_type;
	uint16_t mod_opts;
	memcpy_s(&mod_type, sizeof(mod_type), data_ptr, sizeof(uint16_t));
	data_ptr += sizeof(uint16_t);
	res.mod_type = (DiskMod::ModType)be16toh(mod_type);

	memcpy_s(&mod_opts, sizeof(mod_opts), data_ptr, sizeof(uint16_t));
	data_ptr += sizeof(uint16_t);
	res.mod_opts = (DiskMod::ModOpts)be16toh(mod_opts);

	if (res.mod_type == DiskMod::kCheckpointMod || res.mod_type == DiskMod::kSyncMod)
	{	// No more left to do here.
		return 0;
	}

	// Small buffer to read characters into so we aren't adding to a string one
	// character at a time until the end of the string.
	const unsigned int tmp_size = 128;
	wchar_t tmp[tmp_size];
	memset(tmp, 0, tmp_size* sizeof(wchar_t));
	unsigned int chars_read = 0;
//	wchar_t* str = (wchar_t*)(data_ptr);
	while (data_ptr[0] != '\0')
	{	// We still haven't seen a null terminator, so read another character.
		tmp[chars_read] = ((wchar_t*)data_ptr)[0];
		++chars_read;
		if (chars_read == tmp_size - 1)
		{	// Fall into this at one character short so that we have an automatic null terminator
			res.path += tmp;
			chars_read = 0;
			// Required because we just add the wchar_t[] to the string and we don't want extra junk. An alternative
			//  would be to make sure you always had a null terminator the character after the one that was just
			//	assigned.
			memset(tmp, 0, tmp_size*sizeof(wchar_t));
		}
		//++data_ptr;
		data_ptr += sizeof(wchar_t);
	}
	// Add the remaining data that is in tmp.
	res.path += tmp;
	// Move past the null terminating character.
	//++data_ptr;
	data_ptr += sizeof(wchar_t);

	res.directory_mod = (bool)data_ptr[0];
	++data_ptr;

	if (res.mod_type == DiskMod::kFsyncMod || res.mod_type == DiskMod::kCreateMod 
		|| mod_type == DiskMod::kRemoveMod)	{return 0;	}

	uint64_t file_mod_location;
	uint64_t file_mod_len;
	memcpy_s(&file_mod_location, sizeof(file_mod_location), data_ptr, sizeof(uint64_t));
	data_ptr += sizeof(uint64_t);
	file_mod_location = be64toh(file_mod_location);
	res.m_file_mod_location = file_mod_location;

	memcpy_s(&file_mod_len, sizeof(file_mod_len), data_ptr, sizeof(uint64_t));
	data_ptr += sizeof(uint64_t);
	file_mod_len = be64toh(file_mod_len);
	res.m_file_mod_len = file_mod_len;

	// Some mods have file length and location, but no actual data associated with
	// them.
	if (res.mod_type == DiskMod::kSyncFileRangeMod ||		res.mod_opts == DiskMod::kFallocateOpt ||
		res.mod_opts == DiskMod::kFallocateKeepSizeOpt ||	res.mod_opts == DiskMod::kPunchHoleKeepSizeOpt ||
		res.mod_opts == DiskMod::kCollapseRangeOpt ||		res.mod_opts == DiskMod::kZeroRangeOpt ||
		res.mod_opts == DiskMod::kZeroRangeKeepSizeOpt ||	res.mod_opts == DiskMod::kInsertRangeOpt)
	{
		return 0;
	}

	if (res.m_file_mod_len > 0)
	{	// Read the data for this mod.
		res.file_mod_data.reset(new (std::nothrow) BYTE[res.m_file_mod_len], [](BYTE* c) {delete[] c; });
		if (res.file_mod_data.get() == nullptr)
			THROW_ERROR(ERR_MEM, L"failed on allocating file_mod_data");
		memcpy_s(res.file_mod_data.get(), res.m_file_mod_len, data_ptr, res.m_file_mod_len);
	}

	return 0;
}

DiskMod::DiskMod()
{
	Reset();
}

void DiskMod::Reset()
{
	path.clear();
	mod_type = kCreateMod;
	mod_opts = kNoneOpt;
	memset(&post_mod_stats, 0, sizeof(struct stat));
	directory_mod = false;
	file_mod_data.reset();
	m_file_mod_location = 0;
	m_file_mod_len = 0;
	directory_added_entry.clear();
}
//
//	}  // namespace utils
//}  // namespace fs_testing


std::wostream& fs_testing::utils::operator << (std::wostream& os, const DiskMod& m)
{
	// type, opt, path, dir
	os << std::setw(25) << m.TypeToString() << std::setw(25) << m.OptToString() << std::setw(25)<< m.path << std::endl;
	return os;

}

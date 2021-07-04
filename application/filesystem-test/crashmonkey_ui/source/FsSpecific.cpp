#include "pch.h"
#include "FsSpecific.h"

//namespace fs_testing {
using namespace fs_testing;
using std::wstring;

namespace {

	constexpr wchar_t kMkfsStart[] = L"mkfs -t ";
	constexpr wchar_t kFsckCommand[] = L"fsck -T -t ";

	constexpr wchar_t kExtRemountOpts[] = L"errors=remount-ro";
	// Disable lazy init for now.
	constexpr wchar_t kExtMkfsOpts[] = L"-E lazy_itable_init=0,lazy_journal_init=0";

	// TODO(ashmrtn): See if we actually want the repair flag or not. The man page
	// for btrfs check is not clear on whether it will try to cleanup the file
	// system some without it. It also says to be careful about using the repair
	// flag.
	constexpr wchar_t kBtrfsFsckCommand[] = L"yes | btrfs check ";

	constexpr wchar_t kXfsFsckCommand[] = L"xfs_repair ";

	constexpr wchar_t kExtNewUUIDCommand[] = L"tune2fs -U random ";
	constexpr wchar_t kBtrfsNewUUIDCommand[] = L"yes | btrfstune -u ";
	constexpr wchar_t kXfsNewUUIDCommand[] = L"xfs_admin -U generate ";
	constexpr wchar_t kF2fsNewUUIDCommand[] = L":";
}


FsSpecific* fs_testing::GetFsSpecific(std::wstring& fs_type)
{
	// TODO(ashmrtn): Find an elegant way to handle errors.
	if (fs_type.compare(Ext4FsSpecific::kFsType) == 0)
	{
		return new Ext4FsSpecific();
	}
	else if (fs_type.compare(Ext3FsSpecific::kFsType) == 0)
	{
		return new Ext3FsSpecific();
	}
	else if (fs_type.compare(Ext2FsSpecific::kFsType) == 0)
	{
		return new Ext2FsSpecific();
	}
	else if (fs_type.compare(BtrfsFsSpecific::kFsType) == 0)
	{
		return new BtrfsFsSpecific();
	}
	else if (fs_type.compare(F2fsFsSpecific::kFsType) == 0)
	{
		return new F2fsFsSpecific();
	}
	else if (fs_type.compare(XfsFsSpecific::kFsType) == 0)
	{
		return new XfsFsSpecific();
	}
	return NULL;
}

/******************************* Ext File Systems *****************************/
constexpr wchar_t Ext2FsSpecific::kFsType[];
Ext2FsSpecific::Ext2FsSpecific() :
	ExtFsSpecific(Ext2FsSpecific::kFsType, Ext2FsSpecific::kDelaySeconds)
{
}

constexpr wchar_t Ext3FsSpecific::kFsType[];
Ext3FsSpecific::Ext3FsSpecific() :
	ExtFsSpecific(Ext3FsSpecific::kFsType, Ext3FsSpecific::kDelaySeconds)
{
}

constexpr wchar_t Ext4FsSpecific::kFsType[];
Ext4FsSpecific::Ext4FsSpecific() :
	ExtFsSpecific(Ext4FsSpecific::kFsType, Ext4FsSpecific::kDelaySeconds)
{
}

ExtFsSpecific::ExtFsSpecific(std::wstring type, unsigned int delay_seconds) :
	fs_type_(type), delay_seconds_(delay_seconds)
{
}

std::wstring ExtFsSpecific::GetMkfsCommand(std::wstring& device_path)
{
	return std::wstring(kMkfsStart) + fs_type_ + L" " +
		kExtMkfsOpts + L" " + device_path;
}

std::wstring ExtFsSpecific::GetPostReplayMntOpts()
{
	return std::wstring(kExtRemountOpts);
}

std::wstring ExtFsSpecific::GetFsckCommand(const std::wstring& fs_path)
{
	return std::wstring(kFsckCommand) + fs_type_ + L" " + fs_path + L" -- -y";
}

std::wstring ExtFsSpecific::GetNewUUIDCommand(const std::wstring& disk_path)
{
	return std::wstring(kExtNewUUIDCommand) + disk_path;
}

FileSystemTestResult::ErrorType ExtFsSpecific::GetFsckReturn(
	int return_code)
{
	// The following is taken from the specification in man(8) fsck.ext4.
	if ((return_code & 0x8) || (return_code & 0x10) || (return_code & 0x20) ||
		// Some sort of fsck error.
		return_code & 0x80)
	{
		return FileSystemTestResult::kCheck;
	}

	if (return_code & 0x4)
	{
		return FileSystemTestResult::kCheckUnfixed;
	}

	if ((return_code & 0x1) || (return_code & 0x2))
	{
		return FileSystemTestResult::kFixed;
	}

	if (return_code == 0)
	{
		return FileSystemTestResult::kClean;
	}

	// Default selection so at least something looks wrong.
	return FileSystemTestResult::kOther;
}

std::wstring ExtFsSpecific::GetFsTypeString()
{
	return std::wstring(Ext4FsSpecific::kFsType);
}

unsigned int ExtFsSpecific::GetPostRunDelaySeconds()
{
	return delay_seconds_;
}

/******************************* Btrfs ****************************************/
constexpr wchar_t BtrfsFsSpecific::kFsType[];

std::wstring BtrfsFsSpecific::GetMkfsCommand(std::wstring& device_path)
{
	return std::wstring(kMkfsStart) + BtrfsFsSpecific::kFsType + L" " + device_path;
}

std::wstring BtrfsFsSpecific::GetPostReplayMntOpts()
{
	return std::wstring();
}

std::wstring BtrfsFsSpecific::GetFsckCommand(const std::wstring& fs_path)
{
	return std::wstring(kBtrfsFsckCommand) + fs_path;
}

std::wstring BtrfsFsSpecific::GetNewUUIDCommand(const std::wstring& disk_path)
{
	return std::wstring(kBtrfsNewUUIDCommand) + disk_path;
}

FileSystemTestResult::ErrorType BtrfsFsSpecific::GetFsckReturn(
	int return_code)
{
	// The following is taken from the specification in man(8) btrfs-check.
	// `btrfs check` is much less expressive in its return codes than fsck.ext4.
	// Here all we get is 0/1 corresponding to success/failure respectively. For
	// 0, `btrfs check` did not find anything out of the ordinary. For 1,
	// `btrfs check` found something. The tests in the btrfs-progs repo seem to
	// imply that it won't automatically fix things for you so we return
	// FileSystemTestResult::kCheckUnfixed.
	if (return_code == 0)
	{
		return FileSystemTestResult::kFixed;
	}
	return FileSystemTestResult::kCheckUnfixed;
}

std::wstring BtrfsFsSpecific::GetFsTypeString()
{
	return std::wstring(BtrfsFsSpecific::kFsType);
}

unsigned int BtrfsFsSpecific::GetPostRunDelaySeconds()
{
	return BtrfsFsSpecific::kDelaySeconds;
}

/******************************* F2fs *****************************************/
constexpr wchar_t F2fsFsSpecific::kFsType[];

std::wstring F2fsFsSpecific::GetMkfsCommand(std::wstring& device_path)
{
	return std::wstring(kMkfsStart) + F2fsFsSpecific::kFsType + L" " + device_path;
}

std::wstring F2fsFsSpecific::GetPostReplayMntOpts()
{
	return std::wstring();
}

std::wstring F2fsFsSpecific::GetFsckCommand(const std::wstring& fs_path)
{
	return std::wstring(kFsckCommand) + kFsType + L" " + fs_path + L" -- -y";
}

std::wstring F2fsFsSpecific::GetNewUUIDCommand(const std::wstring& disk_path)
{
	return std::wstring(kF2fsNewUUIDCommand);
}

FileSystemTestResult::ErrorType F2fsFsSpecific::GetFsckReturn(
	int return_code)
{
	// The following is taken from the specification in man(8) fsck.f2fs.
	// `fsck.f2fs` is much less expressive in its return codes than fsck.ext4.
	// Here all we get is 0/-1 corresponding to success/failure respectively. For
	// 0, FileSystemTestResult::kFixed will be assumed as it appears that 0 is the
	// return when fsck has completed running (the function that runs fsck is void
	// in the source code so there is no way to tell what it did easily). For -1,
	// FileSystemTestResult::kCheck will be assumed.
	// TODO(ashmrtn): Update with better values based on std::wstring parsing.
	if (return_code == 0)
	{
		return FileSystemTestResult::kFixed;
	}
	return FileSystemTestResult::kCheck;
}

std::wstring F2fsFsSpecific::GetFsTypeString()
{
	return std::wstring(F2fsFsSpecific::kFsType);
}

unsigned int F2fsFsSpecific::GetPostRunDelaySeconds()
{
	return F2fsFsSpecific::kDelaySeconds;
}

/******************************* Xfs ******************************************/
constexpr wchar_t XfsFsSpecific::kFsType[];

std::wstring XfsFsSpecific::GetMkfsCommand(std::wstring& device_path)
{
	return std::wstring(kMkfsStart) + XfsFsSpecific::kFsType + L" " + device_path;
}

std::wstring XfsFsSpecific::GetPostReplayMntOpts()
{
	return std::wstring();
}

std::wstring XfsFsSpecific::GetFsckCommand(const std::wstring& fs_path)
{
	return std::wstring(kXfsFsckCommand) + fs_path;
}

std::wstring XfsFsSpecific::GetNewUUIDCommand(const std::wstring& disk_path)
{
	return std::wstring(kXfsNewUUIDCommand) + disk_path;
}

FileSystemTestResult::ErrorType XfsFsSpecific::GetFsckReturn(
	int return_code)
{
	if (return_code == 0)
	{
		// Will always return 0 when running without the dry-run flag. Things have
		// been fixed though.
		return FileSystemTestResult::kFixed;
	}
	return FileSystemTestResult::kCheck;
}

std::wstring XfsFsSpecific::GetFsTypeString()
{
	return std::wstring(XfsFsSpecific::kFsType);
}

unsigned int XfsFsSpecific::GetPostRunDelaySeconds()
{
	return XfsFsSpecific::kDelaySeconds;
}

//}  // namespace fs_testing

#ifndef DISK_CONTENTS_H
#define DISK_CONTENTS_H

//#include <dirent.h>     /* Defines DT_* constants */
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
//#include <sys/mount.h>
#include <sys/stat.h>
//#include <sys/syscall.h>
#include <sys/types.h>
//#include <unistd.h>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <map>

namespace fs_testing {

class fileAttributes 
{
public:
#ifdef _TO_BE_IMPLEMENTED_
    struct dirent dir_attr;
    bool compare_dir_attr(struct dirent a);
#endif
    struct _stat64i32 stat_attr;
    std::wstring md5sum;

    fileAttributes();
    ~fileAttributes();

    void set_dir_attr(struct dirent* a);
    void set_stat_attr(std::wstring path, bool islstat);
    void set_md5sum(std::wstring filepath);
    bool compare_stat_attr(const struct _stat64i32 & a);
    bool compare_md5sum(std::wstring a);
    bool is_regular_file();
};

class DiskContents 
{
public:
    // Constructor and Destructor
    DiskContents(std::wstring path, std::wstring type);
    ~DiskContents();
  
    int mount_disk();
    std::wstring get_mount_point();
    void set_mount_point(std::wstring path);
    int unmount_and_delete_mount_point();
    bool compare_disk_contents(DiskContents &compare_disk, std::wofstream &diff_file);
    bool compare_entries_at_path(DiskContents &compare_disk, std::wstring &path, std::wofstream &diff_file);
    bool compare_file_contents(DiskContents &compare_disk, std::wstring path, int offset, int length, std::wofstream &diff_file);
    bool deleteFiles(std::wstring path, std::wofstream &diff_file);
    bool makeFiles(std::wstring base_path, std::wofstream &diff_file);
    bool sanity_checks(std::wofstream &diff_file);

private:
    bool device_mounted;
    std::wstring disk_path;
    std::wstring mount_point;
    std::wstring fs_type;
    std::map<std::wstring, fileAttributes> contents;
    void compare_contents(DiskContents &compare_disk, std::wofstream &diff_file);
    void get_contents(const wchar_t* path);
};

} // namespace fs_testing

#endif // DISK_CONTENTS_H

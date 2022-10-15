#pragma once

#include <stdext.h>
#include <map>


#define	MAX_DEPTH		(20)
#define MAX_PATH_SIZE	(31)
#define MAX_FILE_NUM	(100)

class CReferenceFs
{
public:
	class CRefFile
	{
	public:
		// <0���ļ���Ч
		int m_size;
		UINT m_checksum;	// �����ļ����ļ���checksum������Ŀ¼������Ŀ������
		wchar_t m_fn[MAX_PATH_SIZE+1];
	};
public:
//	typedef std::wstring CRefFile;
//	typedef std::pair<std::wstring, UINT64> CRefFile;
	//typedef std::map<std::wstring, UINT64>::iterator CRefFile;
	typedef std::map<std::wstring, UINT>::iterator ITERATOR;
	typedef std::map<std::wstring, UINT>::const_iterator CONST_ITERATOR;

public:
	void Initialize(const std::wstring & root_path = L"\\");
	void CopyFrom(const CReferenceFs & src);
	bool IsExist(const std::wstring & path);		// �ж�·���Ƿ����
	bool AddPath(const std::wstring & path, bool dir);		// ���һ��·��
	void GetFileInfo(const CRefFile & file, DWORD & checksum, size_t &len) const;
	void GetFilePath(const CRefFile& file, std::wstring & path) const;
	void UpdateFile(const std::wstring & path, DWORD checksum, size_t len);
	void UpdateFile(CRefFile & file, DWORD checksum, size_t len);
	//void AddRoot(void);

	bool IsDir(const CRefFile & file) const;
	CRefFile * FindFile(const std::wstring & path);
	void RemoveFile(const std::wstring & path);
protected:
	UINT FindFileIndex(const std::wstring & path);

public:
	CONST_ITERATOR Begin() const { return m_ref.begin(); }
	CONST_ITERATOR End() const { return m_ref.end(); }
	const CRefFile & GetFile(CONST_ITERATOR & it) const;


protected:
	//std::set<std::wstring> m_ref;
	// �����ļ���UINT64���length | checksum, ����dir�������Ŀ¼����
	//	map�д淽ref file��index
	std::map<std::wstring, UINT> m_ref;
	CRefFile m_files[MAX_FILE_NUM];
	// ����m_files������һ��free list��free list��checksumָ������һ������m_free_listָ����ͷ����
	UINT m_free_list;
	size_t m_free_num;
};

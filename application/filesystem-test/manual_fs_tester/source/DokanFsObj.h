///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

using namespace System;
using namespace System::Management::Automation;

#include <dokanfs-lib.h>


namespace ManualFsTester
{
	public enum class FileType {
		FILE_DIR, FILE_NORMAL,
	};
	public enum class CreateFileOption {
		FS_CREATE_NEW, FS_CREATE_ALWAYS, FS_OPEN_EXISTING, FS_OPEN_ALWAYS, FS_TRUNCATE_EXISTING,

	};
	public ref class DokanStorage : public System::Object
	{
	public:
		DokanStorage(System::String^ lib_path, System::String ^ drive_name);
		!DokanStorage(void);
		~DokanStorage(void) {}
		IVirtualDisk* get_disk(void) { return m_disk; }

	public:
		bool Initialize(System::String^ config_fn, System::String^ data_file);
		bool Save(System::String^ fn);
		bool Load(System::String^ fn);
		array<PSObject^>^ ListIOs(void);
		bool Rollback(int blk_nr);

	protected:
		IVirtualDisk* m_disk = nullptr;
	};

	public ref class DokanFileObj : public Object
	{
	public:
		DokanFileObj(IFileInfo* file);
		!DokanFileObj(void);
		~DokanFileObj(void) {}
	public:
		UINT WriteFile(UINT offset, UINT length);
		void CloseFile(void);
		UINT GetFileIndex(void);
		UINT GetFileSize(void);

	public:
		String^ name;
	protected:
		IFileInfo* m_file;
		//String^ m_fn;
	};

	public ref class DokanFsObj : public System::Object
	{
	public:
		DokanFsObj(System::String ^ config_fn);
		!DokanFsObj(void); 
		~DokanFsObj(void) {}
	public:

		bool Mount(DokanStorage^ disk);
		void Unmount(void);
		bool MakeFs(DokanStorage ^ disk);
		bool CheckFs(DokanStorage^ disk, System::String ^ config);

		DokanFileObj^ CreateFile(System::String^ file_name, FileType type, CreateFileOption option);
		DokanFileObj^ OpenFile(System::String^ file_name);

		bool Verify(void);

	protected:
		bool CheckAllChilds(const std::wstring& fn);

	protected:
		IFileSystem* m_fs = nullptr;
	};



	[CmdletAttribute(VerbsCommon::New, "DokanF2FS")]
		public ref class NewDokanF2FS : public System::Management::Automation::Cmdlet
	{
	public:
		NewDokanF2FS(void) { /*data_file = nullptr; */};
		~NewDokanF2FS(void) {};

	public:
		[Parameter(Position = 0, Mandatory=true, HelpMessage = "name of storage device")]
		property System::String^ config;
		//[Parameter(Position = 1, HelpMessage = "name of storage device")]
		//property System::String^ data_file;


	public:
		virtual void ProcessRecord() override;
	};

	[CmdletAttribute(VerbsCommon::New, "DokanStorage")]
		public ref class NewDokanStorage : public System::Management::Automation::Cmdlet
	{
	public:
		NewDokanStorage(void) { };
		~NewDokanStorage(void) {};

	public:
		[Parameter(Position = 0, Mandatory = true,	HelpMessage = "name of storage device")]
		property System::String ^ lib_path;
		[Parameter(Position = 1, Mandatory = true, HelpMessage = "name of storage device")]
		property System::String^ name;

	public:
		virtual void ProcessRecord() override;
	};

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// == help functions
	//template <typename T1>
	void AddPropertyMember(PSObject^ list, String^ name, UINT val)
	{
		PSNoteProperty^ item = gcnew PSNoteProperty(name, gcnew UInt32(val));
		list->Members->Add(item);
	}
};


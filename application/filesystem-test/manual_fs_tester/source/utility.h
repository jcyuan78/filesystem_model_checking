#pragma once


inline void ToStdString(std::wstring & dst, System::String ^ src)
{
	if (!System::String::IsNullOrEmpty(src))
	{
		const wchar_t * wstr = (const wchar_t*)(System::Runtime::InteropServices::Marshal::StringToHGlobalUni(src)).ToPointer();
		dst = wstr;
		System::Runtime::InteropServices::Marshal::FreeHGlobal(IntPtr((void*)wstr));
	}
}

inline void ToUtf8String(std::string& dst, System::String^ src)
{
	std::wstring wdst;
	if (!System::String::IsNullOrEmpty(src))
	{
		const wchar_t* wstr = (const wchar_t*)(System::Runtime::InteropServices::Marshal::StringToHGlobalUni(src)).ToPointer();
		wdst = wstr;
		System::Runtime::InteropServices::Marshal::FreeHGlobal(IntPtr((void*)wstr));
	}
	jcvos::UnicodeToUtf8(dst, wdst);
}

inline void SystemGuidToGUID(GUID & out_id, System::Guid ^ in_id)
{
	String ^ str_in = in_id->ToString(L"B");
	std::wstring str_out;
	ToStdString(str_out, str_in);
	CLSIDFromString(str_out.c_str(), &out_id);
}

inline System::Guid ^ GUIDToSystemGuid(const GUID & in_id)
{
	wchar_t str[64];
	StringFromGUID2(in_id, str, 64);
	return gcnew System::Guid(gcnew System::String(str));
}

//Clone::PartitionType GuidToPartitionType(System::Guid ^ type_id);
//
//inline System::Guid ^ GptTypeToGuid(const Clone::PartitionType & type)
//{
//	System::Guid ^ type_id = nullptr;
//
//	switch (type)
//	{
//	case Clone::PartitionType::EFI_Partition:
//		type_id = gcnew System::Guid("{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}");
//		break;
//	case Clone::PartitionType::Microsoft_Reserved:
//		type_id = gcnew System::Guid("{e3c9e316-0b5c-4db8-817d-f92df00215ae}");
//		break;
//	case Clone::PartitionType::Basic_Data:
//		type_id = gcnew System::Guid("{ebd0a0a2-b9e5-4433-87c0-68b6b72699c7}");
//		break;
//	case Clone::PartitionType::LDM_Metadata:
//		type_id = gcnew System::Guid("{5808c8aa-7e8f-42e0-85d2-e1e90434cfb3}");
//		break;
//	case Clone::PartitionType::LDM_Data:
//		type_id = gcnew System::Guid("{af9b60a0-1431-4f62-bc68-3311714a69ad}");
//		break;
//	case Clone::PartitionType::Microsoft_Recovery:
//		type_id = gcnew System::Guid("{de94bba4-06d1-4d40-a16a-bfd50179d6ac}");
//		break;
//	default:
//		type_id = gcnew System::Guid();
//		break;
//	}
//	return type_id;
//
//}



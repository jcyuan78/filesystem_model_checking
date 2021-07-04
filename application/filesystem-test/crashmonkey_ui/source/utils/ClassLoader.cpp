#include "../pch.h"

#include "ClassLoader.h"

std::map<std::wstring, void*> fs_testing::utils::ClassLoaderBase::m_function_map;


void fs_testing::utils::ClassLoaderBase::AddFunction(const std::wstring & func_name, void* func)
{
	auto ir = m_function_map.insert(std::make_pair(func_name, func));
	if (!ir.second) THROW_ERROR(ERR_APP, L"function: %s has already exist", func_name.c_str());
}

void* fs_testing::utils::ClassLoaderBase::GetFunction(const wchar_t* func_name)
{
	auto ir = m_function_map.find(func_name);
	if (ir == m_function_map.end()) THROW_ERROR(ERR_APP, L"funciton: %s does not exist", func_name);
	return ir->second;
}

param ($platform="win64")

$env:path = ""	`
	+ "C:\Windows\system32;"	`
	+ "C:\Windows;"	`
	+ "C:\Windows\System32\WindowsPowerShell\v1.0\;"	`
	+ "C:\Program Files\dotnet\;";

$vs_dir = "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community"
$vc_dir = "$vs_dir\VC"
$build_dir = "$vs_dir\VC\Auxiliary\Build\"

& $build_dir\vcvarsall.bat "x86"
$vc = "$vs_dir\VC\Tools\MSVC\14.15.26726\bin\Hostx86\x86"

$tool_dir = "$vs_dir\Common7\Tools\"
$ide_dir = "$vs_dir\Common7\IDE\"
$win_kits_10 = "C:\Program Files (x86)\Windows Kits\10"
$win_kits_inc = "$win_kits_10\include\10.0.17134.0"
$win_kits_lib = "$win_kits_10\Lib\10.0.17134.0"

$win_sdk_inc = "$win_kits_10\include\10.0.17134.0"
$win_sdk_lib = "$win_kits_10\Lib\10.0.17134.0"
	
$env:include = "$vs_dir\VC\Tools\MSVC\14.15.26726\ATLMFC\include;" `
	+ "$vs_dir\VC\Tools\MSVC\14.15.26726\include;" `
	+ "C:\Program Files (x86)\Windows Kits\NETFXSDK\4.6.1\include\um;" `
	+ "$win_kits_inc\ucrt;" `
	+ "$win_kits_inc\shared;" `
	+ "$win_kits_inc\um;" `
	+ "$win_kits_inc\winrt;" `
	+ "$win_kits_inc\cppwinrt;"

$env:LIB = "$vc_dir\Tools\MSVC\14.15.26726\ATLMFC\lib\x86;" `
	+ "$vc_dir\Tools\MSVC\14.15.26726\lib\x86;" `
	+ "C:\Program Files (x86)\Windows Kits\NETFXSDK\4.6.1\lib\um\x86;" `
	+ "$win_kits_lib\um\x86;" `
	+ "$win_kits_lib\ucrt\x86;"	
	
$env:include += "$win_sdk_inc;" `
	+ "$win_sdk_inc\crt;" `
	+ "$win_sdk_inc\crt\sys;" `
	+ "$win_sdk_inc\mfc;" `
	+ "$win_sdk_inc\atl"

$env:path += "$vc_dir\Tools\MSVC\14.16.27023\bin\Hostx86\x86"

<#
	LIB="C:\Program Files\Microsoft Platform SDK for Windows Server 2003 R2\Lib\AMD64;" `
	+ "C:\Program Files\Microsoft Platform SDK for Windows Server 2003 R2\Lib\AMD64\atlmfc;"

	
	export PATH INCLUDE LIB	
$env:LIBPATH = "$vc_dir\Tools\MSVC\14.15.26726\ATLMFC\lib\x86;" `
	+ "$vc_dir\Tools\MSVC\14.15.26726\lib\x86;" `
	+ "$vc_dir\Tools\MSVC\14.15.26726\lib\x86\store\references;" `
	+ "$win_kits_10\UnionMetadata\10.0.17134.0;" `
	+ "$win_kits_10\References\10.0.17134.0;" `
	+ "C:\Windows\Microsoft.NET\Framework\v4.0.30319;" `
	+ "$win_kits_lib\um\x86"
#>
	
	
	
#& "$vc_dir\Tools\MSVC\14.15.26726\bin\Hostx86\x86\cl.exe" `
#	/D "WIN32" /D "NCORE=3" /D "MEMLIM=2048" /D "XUSAFE" /D "SAFETY" /D "NOCLAIM" /D "REACH" pan.c	

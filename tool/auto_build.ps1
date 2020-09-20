# set environment

$platform="x86"

$vs_dir = "E:\Program Files (x86)\Microsoft Visual Studio\2017\Community"
$build_dir = "$vs_dir\VC\Auxiliary\Build\"
$tool_dir = "$vs_dir\Common7\Tools\"
$ide_dir = "$vs_dir\Common7\IDE\"

& $build_dir\vcvarsall.bat $platform

# insert projects to build
$projects = @();
$projects += @{name="stdext"; path="..\jcvos2\stdext\"}
$projects += @{name="jcparam"; path="..\jcvos2\jcparam\"}
$projects += @{name="jcfile"; path="..\jcvos2\jcfile\"}
$projects += @{name="jcapp"; path="..\jcvos2\jcapp\"}

$configs = ("DEBUG_DYNAMIC", "DEBUG_STATIC", "RELEASE_DYNAMIC", "RELEASE_STATIC");


push-location
cd ..\build
foreach ($pp in $projects)
{
#	& $ide_dir\devenv.com ..\jcvos2\vs2017\stdext.vcxproj /build "DEBUG_DYNAMIC|$platform"
	$pfile= [String]::format("{0}{1}.vcxproj", $pp.path, $pp.name)
	foreach ($cc in $configs)
	{
		& $ide_dir\devenv.com $pfile /build "$cc|$platform"
	}
}

pop-location


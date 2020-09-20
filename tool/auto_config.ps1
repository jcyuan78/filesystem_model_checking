<# 
.SYNOPSIS
	This script automaticly configurate the build environment before you build it.
	
.PARAMETER path_vld
	Specify the full path of VLD 2.1. If you do not specify it, you can use ver 1.15 which is included in the workspace. However VLD 1.5 cannot support Win64.
	
.PARAMETER path_boost
	Specify the full path of BOOST installed on you PC.
	
.PARAMETER path_opencv
	Specify the full path of OPENCV installed on your PC.
	
#>
param($path_vld, $path_boost, $path_winddk, $path_opencv,
 $path_zbar, $path_curl, $path_graphvis)

$path_workspace = resolve-path ".."
write-host "Workspace path: " $path_workspace

# create build folder for work.
$build_dir = "$path_workspace\build"
if ( -not (test-path -path $build_dir) )
{
	md $build_dir | out-null
}

#$prop_dir = "$path_workspace\build"
$prop_file = "$build_dir\external_libs.vsprops"
if (test-path -path $prop_file)
{
	remove-item -force $prop_file
}

if (!($path_vld) )
{
	$path_vld = resolve-path "../extlib/vld"
	cp $path_vld"/vld_v1_15.vsprops" $path_vld"/vld_support.vsprops"
}
else
{
	cp $path_workspace"/extlib/vld/vld_v2_1.vsprops" $path_workspace"/extlib/vld/vld_support.vsprops"
}

$prop_list = @()
$prop_list += @{name="VLDDIR"; val=$path_vld}
$prop_list += @{name="BOOST_DIR"; val=$path_boost}
$prop_list += @{name="WINDDK"; val=$path_winddk}
$prop_list += @{name="OPENCV"; val=$path_opencv}
$prop_list += @{name="ZBARDIR"; val=$path_zbar}
$prop_list += @{name="CURLDIR"; val=$path_curl}
$prop_list += @{name="GRAPHVIZ_DIR"; val=$path_graphviz}
$prop_list += @{name="WORKSPACE"; val=$path_workspace}
$prop_list += @{name="BUILD_DIR"; val=$build_dir}



'<?xml version="1.0"?>' >> $prop_file
"<VisualStudioPropertySheet"  >> $prop_file
"	ProjectType=""Visual C++"" " >> $prop_file
"	Version=""8.00"" "  >> $prop_file
"	Name=""external_libs"" "  >> $prop_file
"	>"  >> $prop_file

foreach ($p in $prop_list)
{
	if ($p.val)
	{
		"	<UserMacro" >> $prop_file
		"		Name=""" + $p.name + '"'	>> $prop_file
		"		Value=""" + $p.val + '"'	>> $prop_file
		"	/>" >> $prop_file
	}
}	
"</VisualStudioPropertySheet>"  >> $prop_file
write-host "Cofiguation completed."
#<TODO> create local_config.h for jcvos


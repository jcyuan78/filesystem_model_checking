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
 $path_zbar, $path_curl, $path_graphviz)

$path_workspace = resolve-path ".."
write-host "Workspace path: " $path_workspace

# create build folder for work.
$build_dir = "$path_workspace\build"
if ( -not (test-path -path $build_dir) )
{
	md $build_dir | out-null
}

$prop_dir = "$path_workspace\application"
$prop_file = "$path_workspace\build\external_libs.props"
if (test-path -path $prop_file)
{
	remove-item -force $prop_file
}

if (!($path_vld) )
{
	$path_vld = resolve-path "..\extlib\vld"
	cp $path_vld"/vld_v1_15.props" $path_vld"\support_vld.props"
}
else
{
	cp $path_workspace"/extlib/vld/vld_v2_1.props" $path_workspace"/extlib/vld/support_vld.props"
}

$prop_list = @()
$prop_list += @{name="VLDDIR"; val=$path_vld}
$prop_list += @{name="BOOST_DIR"; val=$path_boost}
$prop_list += @{name="WINDDK"; val=$path_winddk}
$prop_list += @{name="OPENCV"; val=$path_opencv}
$prop_list += @{name="ZBARDIR"; val=$path_zbar}
$prop_list += @{name="CURLDIR"; val=$path_curl}
$prop_list += @{name="WORKSPACE"; val=$path_workspace}
$prop_list += @{name="GRAPHVIZ_DIR"; val=$path_graphviz}
$prop_list += @{name="BUILD_DIR"; val=$build_dir}


$prop_doc = [xml](Get-Content external_libs_temp.xml)
$project = $prop_doc.Project;

$prop_grp = $project.PropertyGroup[0];
$item_grp = $prop_doc.CreateElement("ItemGroup", $project.NamespaceURI)
foreach ($p in $prop_list)
{
#	$name = $p.name;
#	$val = $p.val;
#write-host "NAME=$name, VAL=$val"
	if ($p.val -ne $null)
	{
		$mm = $prop_doc.CreateElement($p.name, $project.NamespaceURI);
		$mm.InnerText=$p.val;
		$prop_grp.AppendChild($mm);
		
		$ii = $prop_doc.CreateElement("BuildMacro", $project.NamespaceURI);
		$ii.SetAttribute("Include", $p.name);
		$vv = $prop_doc.CreateElement("Value", $project.NamespaceURI);
		$vv.InnerText = [String]::Format("{0}({1})", "$", $p.name);
		$ii.AppendChild($vv);
		$item_grp.AppendChild($ii);
	}
}
$prop_doc.Project.AppendChild($item_grp);

$prop_doc.Save($prop_file)

<#



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

#>

#<TODO> create local_config.h for jcvos


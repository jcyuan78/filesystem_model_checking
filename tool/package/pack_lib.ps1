# parameters:
# 	%1: SRC | LIB, select pack source or lib only
param($Output, $LibName)

write-host "packing lib name=$LibName, out=$Output"	#<DEBUG>
$target_dir = "$LibName"+".package"
write-host "target_dir = $target_dir"	#<DEBUG>

if ( test-path -path $target_dir )
{
	write-host "remove folder $target_dir"
	remove-item -force -recurse -path $target_dir
}
md $target_dir > null

# copy include files
copy-item *.h $target_dir
if (test-path -path "include")
{
	"include is exist"
	cp -recurse include $target_dir\.
}
if ($Output -eq "SRC")
{
	cp *.vcproj $target_dir
	cp -recurse source $target_dir\.
}
elseif ($Output -eq "LIB") 
{
	cp -recurse lib $target_dir
}
else
{
	write-host "unknow parameter $args[0]"
}

if ( test-path -path "additional_pack.ps1")
{
	& ".\additional_pack.ps1"
} 

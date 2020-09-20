$tar_dir = "tool.package"
if ( test-path -path $tar_dir )
{
	remove-item -force -recurse -path $tar_dir
}
md $tar_dir | out-null

cp auto_config.ps1 $tar_dir

#trace的数据统计
#（1）冷热文件的统计
param($srcs, $dst)


$file_info_map = @{};
$cur_fid = 1;

function FileStatics()
{
    process
    {
        $trace = $_;
        if ($trace.path -eq "") {return;}
        $path_len = $trace.Path.length;
        if ($path_len -ge 44)        {         $fn = $trace.Path.substring(44); }
        else {
            write-debug "path is too short, path=$($trace.path)";
            return;
        }
        $file_info = $file_info_map[$fn];
        if ($file_info -eq $null)
        {
            $file_info = New-Object PSObject -Property @{
                "FID" = $cur_fid;
                "Path" = $fn;
                "Total_Open" = 0;
                "Cur_Open" = 0;
                "Max_Open" = 0;
                "FileSize" = 0;
                "FileBlocks" = 0;
                "Total_Read" = 0;
                "Total_Write" = 0;
                "WritePercent" =0;
                "WriteBlocks" =0;
            }
            $cur_fid ++;
            $file_info_map[$fn] = $file_info;
        }

        switch ($trace.Operation)
        {
            "CreateFile" {
                $file_info.Total_Open ++;
                $file_info.Cur_Open ++;
                if ($file_info.Max_Open -lt $file_info.Cur_Open) {$file_info.Max_Open = $file_info.Cur_Open;}
            }
            "CloseFile" {
                $file_info.Cur_Open --;
            }
            "ReadFile" {
                $end_of_file = [int64]$trace.Param1 + [int64]$trace.Param2;
                if ($file_info.FileSize -lt $end_of_file) {$file_info.FileSize = $end_of_file;}
                $file_info.Total_Read += [int64]$trace.Param2;
            }
            "WriteFile" {
                $start_blk = [math]::floor([int64]$trace.Param1/4096);
                $end_write = [int64]$trace.Param1 + [int64]$trace.Param2;
                $end_blk = [math]::Ceiling($end_write/4096);
                $file_info.WriteBlocks+=($end_blk-$start_blk);

                if ($file_info.FileSize -lt $end_write) {$file_info.FileSize = $end_write;}
                $file_info.Total_Write += [int64]$trace.Param2;                
            }
        }
    }
}

foreach ($src in $srcs) {
    write-debug "processing file: $src";
    import-csv $src | FileStatics 
}

if (Test-Path $dst) {rm $dst;}

foreach ($key in $file_info_map.Keys)
{
    $file_info = $file_info_map[$key];
    $file_info.FileBlocks = [Math]::Ceiling($file_info.FileSize / 4096);
    if ($file_info.FileBlocks -ne0) {
        $file_info.WritePercent=$file_info.WriteBlocks/$file_info.FileBlocks;
    }
    $file_info | select-object "FID","Path","FileSize","FileBlocks","Total_Open","Max_Open","Total_Read","Total_Write","WriteBlocks","WritePercent" |
        export-csv "$dst" -Append;
}
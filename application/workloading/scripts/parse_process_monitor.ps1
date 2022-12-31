Param($src, $dst)


$reg_detail = [regex]"Disposition: ([^,]+), .* OpenResult: (.+)";
$open_files = 0;


$reg_ts = [regex]"(\d+)\:(\d+)\:(\d+)\.(\d+)"
function ParseTimestamp($timestamp)
{
    [int64]$ts = 0;
    $res = $reg_ts.Match($timestamp);
    if($res.success)
    {
        $ts =  [int]($res.groups[1].value)*60;    # hour => min
        $ts += [int]($res.groups[2].value);
        $ts *= 60;        # min => sec
        $ts += [int]($res.groups[3].value);
        $ts *= 1000000000;      #sec => ns
        $ts += [int]($res.groups[4].value)*100;
    }
    else {write-debug "failed on parsing timestamp = $timestamp";}
    $ts;
}
function ParseCreateFile($item)
{
    $detail = $item.Detail;

    $tokens = $detail.split( (',', ':') ).trim();

    $detail_item_name = "";
    $details = @{};

    foreach ($token in $tokens)
    {
        if ( ($token -eq "Desired Access") -or ($token -eq "Disposition") -or
             ($token -eq "Options") -or ($token -eq "Attributes") -or
             ($token -eq "ShareMode") -or ($token -eq "AllocationSize") -or
             ($token -eq "OpenResult")  )        {
            $detail_item_name = $token;
            $details[$detail_item_name] = @();
        }
        else {$details[$detail_item_name] += $token;}
    }

    #if ($details["Options"] -ne $null) {
    #    $sync1 = $details["Options"] -contains "Synchronous IO Non-Alert";
    #}
    $sync1 = ($details["Desired Access"] -contains "Synchronize") -or
            ($details["Options"] -contains "Synchronous IO Non-Alert") -or
            ($details["Options"] -contains "Synchronous IO Alert");

    if ( ($details["Options"] -contains "Delete On Close") ) {
        $mode="Delete";
        if ($detail["OpenResult"] -contains "Created") {
            Write-Debug "ts=$($item.'Time of Day'), Delete on Create, result=$($item.Result), detail=$detail";
        }
    } 
    elseif ($details["OpenResult"] -contains "Created") {$mode = "Create";}
    elseif ($details["OpenResult"] -contains "Opened") {$mode = "Open";}
    elseif ($details["OpenResult"] -contains "Overwritten") {$mode = "Overwritten";}
    else {$mode = "Unknown";}


    $dir = $details["Options"] -contains "Directory";

    if ($item.Result -eq "SUCCESS") {
#        if ( ($sync1 -eq $false ) ) {
#            write-debug "ts=$($item.'Time of Day'), may asnyc, result=$($item.Result), detail=$detail";
#        }
        if ($mode -eq "Unknown") {
            write-debug "ts=$($item.'Time of Day'), unknown result, result=$($item.Result), detail=$detail";
        }
    }
    else {
#        write-debug "ts=$($item.'Time of Day'), not success, result=$($item.Result), detail=$detail";
    }
    $item.Param1 = $mode;   #Create, Open or Delete
    $item.Param2 = $dir;    #Dir or not
    $item.Param3 = $sync1;  #Sync or Async
    #$item.Param4 = $result;
#    if ($item.Result -eq "SUCCESS") {return $true;}
#    else {return $false;}
#    $input;
}

$reg_rw = [regex]"Offset: ([\d\,]+) Length: ([\d\,]+)( Priority: ([\w]*))?"

function ParseReadWrite($item)
{
    $priority = "";
    $match_result = $reg_rw.match($item.Detail);
    if ($match_result.Success) {
        [int64]$offset = $match_result.groups[1].Value.Replace(",", "");
        [int64]$length = $match_result.groups[2].Value.Replace(",", "");
        if ($match_result.groups[4].Success) {
            $priority = $match_result.groups[4].Value;    
        }
    }
    else {
        write-debug "ts=$($item.'Time of Day'), match failed, result=$($item.Result), detail=$detail";
    }
    $item.Param1 = $offset;
    $item.param2 = $length;
    $item.param3 = $priority;
}

$reg_thread = [regex]"Thread ID: (\d+)(, User Time: ([\d\.]+), Kernel Time: ([\d\.]+))?";
function ParseThread($item)
{
    #Write-host "input=", $item;
    $ts = $item."Time of Day";
    $match_result = $reg_thread.match($item.Detail);
    if ($match_result.Success)
    {
        $item.Param1 = $match_result.groups[1].Value;       #threa ID
        if ($match_result.groups[2].Success)
        {
            $item.Param2 = $match_result.groups[3].Value;   #user time
            $item.Param3 = $match_result.Groups[4].Value;   #kernel time
        }
    }

#    if ($item.Detail -match "Thread ID: (\d+)")
#    {
        #add-member -InputObject $item -NotePropertyName "Param1" $result;
#        $item.Param1=$matches[1];
#    }
    else {
        #Write-Debug $item;
        Write-Debug "ts=$ts wrong format for create thread, $($item.Detail)";
    }
#    $input;
}

$operations = @{};

function ParseProcess()
{
    process
    {
        $item = $_;
        $ts = ParseTimestamp($item."Time of Day");
        Add-Member -InputObject $item -NotePropertyName "timestamp" $ts; 
        
        add-member -InputObject $item -NotePropertyName "Param1" 0;
        Add-Member -InputObject $item -NotePropertyName "Param2" 0;
        add-member -InputObject $item -NotePropertyName "Param3" 0;

#        add-member -InputObject $item -NotePropertyName "Param4" 0;
        #write-debug "item=$item, op=$($item.Operation)";
        if ($operations[$item.Operation] -eq $null)       {
            $operations[$item.Operation] = 1;
        }
        else {$operations[$item.Operation] ++;}
        switch ($item.Operation)
        {
            "CreateFile" {
                if ($item.Result -eq "SUCCESS")
                {
                    ParseCreateFile -item $item;
                    $open_file ++;
                }
                else {$item = $null;}
            }
            "CloseFile" {
                $open_file --;
                #$item = $_;
            }
            "ReadFile" {
                ParseReadWrite -item $item;
            }
            "WriteFile" {
                ParseReadWrite -item $item;
            }
            "Thread Create" {
                ParseThread -item $item;
            }
            "FlushBuffersFile" {}
            "Thread Exit" {
                ParseThread -item $item;
            }
            #"Process Start" {$item=$null;}
            #"Process Exit" {$item=$null;}
            #"QueryNameInformationFile" 
            default {$item=$null;}
        }


        if ($item -ne $null) 
        {
            Add-Member -InputObject $item -NotePropertyName "open_file" $open_file;
            $item;
        }
        else
        {
#            Write-Debug "ignore item $_";
        }
        
    }
}

import-csv $src | select-object "Time of Day","Operation","Path","Result","Detail","TID" |
     ?{ ($_.Path -eq "") -or ($_.Path -like "C:\PCMark_10_Storage_2*") } |
    ParseProcess |
    Select-Object "timestamp", "Operation", "Path", "Result", "TID", "Param1", "Param2", "Param3", "open_file", "Time of Day" |
#    select-object -ExcludeProperty ("Time of Day", "Detail") | 
    export-csv $dst;

$Global:operations = $operations;

Write-Debug "Operation=";
$operations;
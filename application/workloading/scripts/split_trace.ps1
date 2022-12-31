Param($src, $dst);

Function Split_trace_()
{
    Begin {
        $state = 0;     #prepare
        $sub_index = 0;
        $fn = "{0}_{1}.csv" -f $dst, $sub_index;     
        Write-Debug "outputting new sub trace to $fn";    
        $sub_index++;
        $lines = 0;
    }
    Process {
        $item = $_;
        $lines++;
        $open_file = [int]$item.open_file;
        $item | Export-Csv $fn -Append;
        switch ($state)
        {
            0 {
                if ($open_file -gt 150) {$state =1; }    #counting
            }
            1 {
                if ($open_file -eq 0)  {    #output
                    Write-Debug "trace $fn closed, length=$lines";
                    
                    $fn = "{0}_{1}.csv" -f $dst, $sub_index;
                    Write-Debug "outputting new sub trace to $fn";  

                    $state = 0;
                    $sub_index ++;
                    $lines = 0;
                }
            }
        }
    }
    End {
        #if ($sub_trace.Length -gt 0) {
        #    $fn = "{0}_{1}.csv" -f $dst, $sub_index;
        #    $sub_trace | export-csv $fn;
        #}
    }
}

import-csv $src | Split_trace_
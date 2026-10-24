param([int]$Seconds=1810)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path (Split-Path $PSScriptRoot)
$output=Join-Path $projectRoot ('native\artifacts\soak-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $output -Force | Out-Null
$fixtures=Join-Path $projectRoot 'test-artifacts\v3-1.25-1790082981600\photos'
$arguments=@('--benchmark','--seconds',"$Seconds",'--interval','10','--output',('"'+$output+'\metrics.csv"'),('"'+$fixtures+'\a-landscape.bmp"'),('"'+$fixtures+'\b-portrait.bmp"'))
$process=Start-Process -FilePath (Join-Path $projectRoot 'native\out\PhotoFramePrototype.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
Write-Output "稳定性测试 PID=$($process.Id)；报告目录=$output"
$csv=Join-Path $output 'memory.csv'
while(!$process.HasExited){
    $counter=Get-CimInstance Win32_PerfFormattedData_PerfProc_Process -Filter "IDProcess=$($process.Id)"
    if($counter){[pscustomobject]@{Time=(Get-Date).ToString('o');PrivateWorkingSet=$counter.WorkingSetPrivate;WorkingSet=$counter.WorkingSet;PrivateBytes=$counter.PrivateBytes} | Export-Csv -LiteralPath $csv -Append -NoTypeInformation}
    Start-Sleep -Seconds 2
    $process.Refresh()
}
Write-Output "稳定性测试退出码=$($process.ExitCode)；报告目录=$output"

param([int]$Seconds=40,[switch]$IsolatedWindowsProfile)
$ErrorActionPreference='Stop'
if(!$IsolatedWindowsProfile){throw '旧 Electron 对照会登记托盘历史，只允许在独立 Windows 测试账户中显式指定 -IsolatedWindowsProfile 执行'}
$root=Split-Path (Split-Path $PSScriptRoot)
$output=Join-Path $root ('native\artifacts\electron-baseline-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $output -Force | Out-Null
$fixtures=Join-Path $root 'test-artifacts\v3-1.25-1790082981600\photos'
$a=Join-Path $fixtures 'a-landscape.bmp';$b=Join-Path $fixtures 'b-portrait.bmp'
$state=@{global=@{paused=$false};bounds=@{x=120;y=120;width=480;height=360};preferredInnerWidth=444;preferredInnerHeight=324;playlists=@(@{id='baseline';name='性能对照';photoOrder=@($a,$b);sources=@(@{type='file';path=$a},@{type='file';path=$b});config=@{ratio='auto';anchor='bottom-right';frame='minimal';thickness=18;radius=10;shadow=0;transition='fade';interval=10}})}
$state | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $output 'settings.json') -Encoding utf8
$env:PHOTO_FRAME_DATA_DIR=$output
$process=Start-Process -FilePath (Join-Path $root 'dist\1.1.2\release\win-unpacked\桌面相框.exe') -ArgumentList '--force-device-scale-factor=1' -WindowStyle Hidden -PassThru
Write-Output "Electron 对照 PID=$($process.Id)；报告目录=$output"
$end=(Get-Date).AddSeconds($Seconds)
$ids=[System.Collections.Generic.HashSet[int]]::new();[void]$ids.Add($process.Id)
while((Get-Date) -lt $end){
    $tree=Get-CimInstance Win32_Process | Select-Object ProcessId,ParentProcessId
    foreach($level in 1..5){foreach($item in $tree){if($ids.Contains([int]$item.ParentProcessId)){[void]$ids.Add([int]$item.ProcessId)}}}
    $counters=Get-CimInstance Win32_PerfFormattedData_PerfProc_Process | Where-Object {$ids.Contains([int]$_.IDProcess)}
    [pscustomobject]@{Time=(Get-Date).ToString('o');ProcessCount=@($counters).Count;PrivateWorkingSet=($counters | Measure-Object WorkingSetPrivate -Sum).Sum;WorkingSet=($counters | Measure-Object WorkingSet -Sum).Sum;PrivateBytes=($counters | Measure-Object PrivateBytes -Sum).Sum} | Export-Csv -LiteralPath (Join-Path $output 'memory.csv') -Append -NoTypeInformation
    Start-Sleep -Seconds 2
}
# 只停止本次启动的对照程序及其子进程。
foreach($testId in $ids){$p=Get-Process -Id $testId -ErrorAction SilentlyContinue;if($p -and $p.ProcessName -eq '桌面相框'){Stop-Process -Id $testId -Force}}
Write-Output "Electron 对照完成；报告目录=$output"

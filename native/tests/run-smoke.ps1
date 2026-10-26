param([int]$Seconds=130,[string]$Executable='',[double]$Scale=1,[switch]$TimingOnly,[switch]$Video)
$ErrorActionPreference='Stop'
if($TimingOnly -and $Video){throw '高频计时和编码录像必须分开运行。'}
$projectRoot=Split-Path (Split-Path $PSScriptRoot)
$output=Join-Path $projectRoot ('native\artifacts\run-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $output -Force | Out-Null
$fixtures=Join-Path $projectRoot 'test-artifacts\v3-1.25-1790082981600\photos'
$arguments=@('--benchmark','--seconds',"$Seconds",'--scale',"$Scale",'--output',('"'+$output+'\metrics.csv"'),('"'+$fixtures+'\a-landscape.bmp"'),('"'+$fixtures+'\b-portrait.bmp"'))
if(!$Executable){$Executable=Join-Path $projectRoot 'native\out\PhotoFramePrototype.exe'}
$process=Start-Process -FilePath $Executable -ArgumentList $arguments -WindowStyle Hidden -PassThru
Write-Output "原生样机 PID=$($process.Id)；报告目录=$output"
$witnessPath=Join-Path (Split-Path (Resolve-Path $Executable)) 'CaptureWitness.exe'
$witnessArguments=@(('"'+$output+'\capture"'),"$Seconds")
if($TimingOnly){$witnessArguments+='--timing-only'}
if($Video){$witnessArguments+='--video'}
$witness=Start-Process -FilePath $witnessPath -ArgumentList $witnessArguments -WindowStyle Hidden -PassThru
$dwm=Get-Process dwm | Select-Object -First 1
$present=Start-Process -FilePath (Join-Path $projectRoot 'native\toolchain\PresentMon-2.5.1-x64.exe') -ArgumentList @('--process_id',"$($dwm.Id)",'--session_name',('PhotoFrame-'+$process.Id),'--output_file',('"'+$output+'\present.csv"'),'--timed',"$Seconds",'--terminate_after_timed','--no_console_stats') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $output 'present-stdout.txt') -RedirectStandardError (Join-Path $output 'present-stderr.txt')
# 只截取样机所在区域，验证原生合成内容，不捕获整个桌面。
Add-Type -AssemblyName System.Drawing
Start-Sleep -Seconds 3
$bitmap=New-Object System.Drawing.Bitmap 800,650
$graphics=[System.Drawing.Graphics]::FromImage($bitmap)
try {
    $graphics.CopyFromScreen(0,0,0,0,$bitmap.Size)
    $bitmap.Save((Join-Path $output 'screen.png'),[System.Drawing.Imaging.ImageFormat]::Png)
} catch {
    $_.Exception.Message | Set-Content -LiteralPath (Join-Path $output 'visual-unavailable.txt')
    Write-Output '屏幕不可用：保留非视觉数据，不将本轮标记为画面验收通过。'
}
$graphics.Dispose();$bitmap.Dispose()
$samples=[System.Collections.Generic.List[object]]::new()
while(!$process.HasExited){
    $counter=Get-CimInstance Win32_PerfFormattedData_PerfProc_Process -Filter "IDProcess=$($process.Id)"
    if($counter){$samples.Add([pscustomobject]@{Time=(Get-Date).ToString('o');PrivateWorkingSet=$counter.WorkingSetPrivate;WorkingSet=$counter.WorkingSet;PrivateBytes=$counter.PrivateBytes})}
    Start-Sleep -Seconds 1
    $process.Refresh()
}
$samples | Export-Csv -LiteralPath (Join-Path $output 'memory.csv') -NoTypeInformation
$witness.WaitForExit(15000) | Out-Null
$present.WaitForExit(15000) | Out-Null
Write-Output "原生样机退出码=$($process.ExitCode)；报告目录=$output"

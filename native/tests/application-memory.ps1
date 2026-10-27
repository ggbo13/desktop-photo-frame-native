param([string]$Executable='native/out/2.0.0-preview.19-delivery/DesktopPhotoFrameNative.exe',[int]$Seconds=1810)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class FrameMemoryWindow {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")]static extern bool EnumWindows(EnumProc f,IntPtr p);
 [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetWindowText(IntPtr h,StringBuilder t,int n);
 [DllImport("user32.dll")]public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 public static IntPtr Find(int pid,string title){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);var t=new StringBuilder(512);GetWindowText(h,t,512);if(id==pid&&t.ToString().Contains(title))found=h;return true;},IntPtr.Zero);return found;}
}
'@
$projectRoot=Split-Path (Split-Path $PSScriptRoot)
$output=Join-Path $projectRoot ('native/artifacts/app-memory-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $output -Force | Out-Null
$fixtures=Join-Path $projectRoot 'test-artifacts/v3-1.25-1790082981600/photos'
$a=Join-Path $fixtures 'a-landscape.bmp';$b=Join-Path $fixtures 'b-portrait.bmp'
$state=@{global=@{paused=$false};bounds=@{x=120;y=120;width=480;height=360};playlists=@(@{id='baseline';name='原生性能对照';sources=@(@{type='file';path=$a},@{type='file';path=$b});config=@{ratio='auto';anchor='bottom-right';frame='minimal';thickness=18;radius=10;shadow=0;transition='fade';interval=10}})}
$state | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $output 'settings.json') -Encoding utf8
$process=Start-FrameTest -FilePath $Executable -ArgumentList @('--data-dir',('"'+$output+'"'),'--test-seconds',"$Seconds",'--metrics',('"'+$output+'\metrics.csv"')) -WindowStyle Hidden -PassThru
Write-Output "原生应用内存测试 PID=$($process.Id)；报告目录=$output"
$start=Get-Date;$phase='playback';$settingsOpened=$false;$viewerOpened=$false;$closed=$false
while(!$process.HasExited){
 $elapsed=((Get-Date)-$start).TotalSeconds
 $main=[FrameMemoryWindow]::Find($process.Id,'原生版')
 if($elapsed -ge 45 -and !$settingsOpened){[FrameMemoryWindow]::SendMessage($main,273,[IntPtr]10,[IntPtr]::Zero)|Out-Null;$settingsOpened=$true;$phase='settings'}
 if($elapsed -ge 65 -and !$viewerOpened){[FrameMemoryWindow]::SendMessage($main,273,[IntPtr]29,[IntPtr]::Zero)|Out-Null;$viewerOpened=$true;$phase='viewer'}
 if($elapsed -ge 85 -and !$closed){foreach($name in @('查看大图','照片与设置')){$window=[FrameMemoryWindow]::Find($process.Id,$name);if($window -ne [IntPtr]::Zero){[FrameMemoryWindow]::SendMessage($window,16,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}};$closed=$true;$phase='playback-after-close'}
 $counter=Get-CimInstance Win32_PerfFormattedData_PerfProc_Process -Filter "IDProcess=$($process.Id)"
 if($counter){[pscustomobject]@{Seconds=$elapsed;Phase=$phase;PrivateWorkingSet=$counter.WorkingSetPrivate;WorkingSet=$counter.WorkingSet;PrivateBytes=$counter.PrivateBytes} | Export-Csv -LiteralPath (Join-Path $output 'memory.csv') -Append -NoTypeInformation}
 Start-Sleep -Seconds 2;$process.Refresh()
}
$rows=Import-Csv (Join-Path $output 'memory.csv');$coverage=[double]$rows[-1].Seconds-[double]$rows[0].Seconds;$maxGap=0.0;for($i=1;$i -lt $rows.Count;$i++){$maxGap=[Math]::Max($maxGap,[double]$rows[$i].Seconds-[double]$rows[$i-1].Seconds)};$summary=@{requestedSeconds=$Seconds;observedSeconds=$coverage;maximumSamplingGap=$maxGap;continuousCoveragePassed=($coverage -ge ($Seconds-10) -and $maxGap -lt 10);exitCode=$process.ExitCode;phases=@()}
foreach($group in ($rows | Group-Object Phase)){$summary.phases+=@{name=$group.Name;samples=$group.Count;averagePrivateWorkingSetMiB=($group.Group | ForEach-Object {[double]$_.PrivateWorkingSet/1MB} | Measure-Object -Average).Average;peakPrivateWorkingSetMiB=($group.Group | ForEach-Object {[double]$_.PrivateWorkingSet/1MB} | Measure-Object -Maximum).Maximum;peakPrivateCommitMiB=($group.Group | ForEach-Object {[double]$_.PrivateBytes/1MB} | Measure-Object -Maximum).Maximum}}
$summary | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $output 'summary.json')
Write-Output "内存测试完成；$output"

param([string]$Executable='native/out/candidate-4/PhotoFramePrototype.exe')
$ErrorActionPreference='Stop'
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class FrameTestWindows {
 public delegate bool EnumProc(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr p);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder text,int n);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int cmd);
 public static IntPtr Find(int pid) { IntPtr found=IntPtr.Zero; EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);var text=new StringBuilder(256);GetWindowText(h,text,256);if(id==pid&&text.ToString().Contains("2.0")) found=h;return true;},IntPtr.Zero);return found; }
}
'@
$root=Split-Path (Split-Path $PSScriptRoot)
$output=Join-Path $root ('native/artifacts/interaction-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $output -Force | Out-Null
$actions=[System.Collections.Generic.List[object]]::new()
function Mark-Action([string]$name){$actions.Add([pscustomobject]@{name=$name;qpc=[System.Diagnostics.Stopwatch]::GetTimestamp();frequency=[System.Diagnostics.Stopwatch]::Frequency})}
$photos=Join-Path $root 'test-artifacts/v3-1.25-1790082981600/photos'
$process=Start-Process $Executable -WindowStyle Hidden -PassThru -ArgumentList @('--benchmark','--seconds','24','--output',('"'+$output+'/metrics.csv"'),('"'+$photos+'/a-landscape.bmp"'),('"'+$photos+'/b-portrait.bmp"'))
$sampler=Join-Path (Split-Path (Resolve-Path $Executable)) 'CaptureWitness.exe'
$witness=Start-Process $sampler -WindowStyle Hidden -PassThru -ArgumentList @(('"'+$output+'/capture"'),'24','--timing-only')
Start-Sleep -Seconds 2
$handle=[FrameTestWindows]::Find($process.Id)
if($handle -eq [IntPtr]::Zero){throw '找不到本轮样机窗口'}
# 仅向本轮进程发送消息，不移动用户光标或向其他应用发送按键。
Mark-Action 'pause'
[FrameTestWindows]::SendMessage($handle,256,[IntPtr]32,[IntPtr]::Zero) | Out-Null
Start-Sleep -Seconds 4
Mark-Action 'resume'
[FrameTestWindows]::SendMessage($handle,256,[IntPtr]32,[IntPtr]::Zero) | Out-Null
Start-Sleep -Seconds 2
Mark-Action 'hide'
[FrameTestWindows]::ShowWindow($handle,0) | Out-Null
Start-Sleep -Seconds 4
Mark-Action 'show'
[FrameTestWindows]::ShowWindow($handle,5) | Out-Null
Start-Sleep -Seconds 2
Mark-Action 'rapid-next'
1..30 | ForEach-Object {[FrameTestWindows]::SendMessage($handle,256,[IntPtr]39,[IntPtr]::Zero) | Out-Null;Start-Sleep -Milliseconds 10}
$process.WaitForExit(20000) | Out-Null
$witness.WaitForExit(15000) | Out-Null
$actions | ConvertTo-Json | Set-Content (Join-Path $output 'actions.json')
$rows=@(Import-Csv (Join-Path $output 'metrics.csv'))
$paused=@($rows | Where-Object {$_.paused -eq '1'})
$hidden=@($rows | Where-Object {$_.visible -eq '0'})
$report=@{exitCode=$process.ExitCode;pauseSamples=$paused.Count;pauseStable=(@($paused.transitions | Select-Object -Unique).Count -eq 1);hiddenSamples=$hidden.Count;hiddenStable=(@($hidden.transitions | Select-Object -Unique).Count -eq 1);hiddenPreservesPause=(@($hidden | Where-Object {$_.paused -ne '0'}).Count -eq 0);resumed=([int]$rows[-1].transitions -gt [int]$hidden[-1].transitions);decodeErrors=$rows[-1].decode_errors;errorFile=(Test-Path (Join-Path $output 'metrics.csv.error'));outsideHitFailures=@($rows | Where-Object {$_.outside_pass -ne '1'}).Count;limitations='消息级操作回归；不代替真实拖动和逐帧画面验收。'}
$report | ConvertTo-Json | Set-Content (Join-Path $output 'summary.json')
$report | ConvertTo-Json
Write-Output $output

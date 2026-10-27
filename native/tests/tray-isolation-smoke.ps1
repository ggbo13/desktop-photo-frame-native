param([string]$Executable='native/out/2.0.0-preview.17-delivery/DesktopPhotoFrameNative.exe',[ValidateSet('no-tray','timer','hooks','stress','normal')][string[]]$Modes=@('no-tray','timer','hooks','stress'),[switch]$ExpectTray,[switch]$IsolatedWindowsProfile)
$ErrorActionPreference='Stop'
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class TrayIsolationProbe {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int left,top,right,bottom;}
 [StructLayout(LayoutKind.Sequential)]public struct IconId{public uint size;public IntPtr window;public uint id;public Guid guid;}
 [DllImport("user32.dll")]public static extern bool EnumWindows(EnumProc callback,IntPtr p);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint process);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,StringBuilder value,int count);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint message,IntPtr w,IntPtr l);
 [DllImport("shell32.dll")]public static extern int Shell_NotifyIconGetRect(ref IconId id,out Rect rect);
 public static IntPtr Find(int processId){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);var title=new StringBuilder(512);GetWindowText(h,title,512);if(id==(uint)processId&&title.ToString().StartsWith("桌面相框 原生版"))found=h;return true;},IntPtr.Zero);return found;}
 public static int IconResult(IntPtr h){var id=new IconId{size=(uint)Marshal.SizeOf(typeof(IconId)),window=h,id=1};Rect rect;return Shell_NotifyIconGetRect(ref id,out rect);}
}
'@
$root=Split-Path (Split-Path $PSScriptRoot)
$binary=[IO.Path]::GetFullPath((Join-Path $root $Executable))
$version=[Diagnostics.FileVersionInfo]::GetVersionInfo($binary)
if([Version]::new($version.FileMajorPart,$version.FileMinorPart,$version.FileBuildPart,$version.FilePrivatePart) -lt [Version]'2.0.0.13'){throw '旧程序无法隔离托盘，禁止在当前账户重新运行旧版复现'}
if(($ExpectTray -or $Modes -contains 'normal') -and !$IsolatedWindowsProfile){throw '真实托盘登记验证只允许在独立 Windows 测试账户中显式指定 -IsolatedWindowsProfile 执行'}
$output=Join-Path $root ('native/artifacts/tray-isolation-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $output -Force|Out-Null
. (Join-Path $PSScriptRoot 'frame-tray-history.ps1')
function HistoryCount {return @(Get-FrameTestTrayHistory | Where-Object path -IEQ $binary).Count}
$before=HistoryCount;$results=@()
foreach($mode in $Modes){
 $data=Join-Path $output $mode;New-Item -ItemType Directory -Path $data -Force|Out-Null
 @{global=@{paused=$true;pauseFullscreen=$false}}|ConvertTo-Json|Set-Content (Join-Path $data 'settings.json') -Encoding utf8
 $arguments=@('--data-dir',('"'+$data+'"'))
 if($mode -eq 'no-tray'){$arguments+='--no-tray'}elseif($mode -eq 'timer'){$arguments+=@('--test-seconds','10')}elseif($mode -eq 'hooks'){$arguments+='--test-hooks'}elseif($mode -eq 'stress'){$arguments+='--stress'}
 $process=Start-Process -FilePath $binary -WindowStyle Hidden -ArgumentList $arguments -PassThru
 $main=[IntPtr]::Zero
 try{
  for($i=0;$i -lt 80 -and $main -eq [IntPtr]::Zero;$i++){Start-Sleep -Milliseconds 50;$main=[TrayIsolationProbe]::Find($process.Id)}
  if($main -eq [IntPtr]::Zero){throw '相框测试进程没有启动'}
  Start-Sleep -Milliseconds 200
  $iconResult=[TrayIsolationProbe]::IconResult($main)
  $results+=@{mode=$mode;registered=($iconResult -eq 0);hresult=$iconResult}
  if(($iconResult -eq 0) -ne [bool]$ExpectTray){throw "托盘登记不符合预期：$mode，registered=$($iconResult -eq 0)"}
 }finally{if(!$process.HasExited -and $main -ne [IntPtr]::Zero){[TrayIsolationProbe]::PostMessage($main,273,[IntPtr]17,[IntPtr]::Zero)|Out-Null;if(!$process.WaitForExit(5000)){Stop-Process -Id $process.Id -Force}}}
}
$after=HistoryCount
if(!$ExpectTray -and $after -ne $before){throw "测试新增托盘历史：$before -> $after"}
@{executable=$binary;results=$results;historyBefore=$before;historyAfter=$after;expectedTray=[bool]$ExpectTray}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $output 'summary.json') -Encoding utf8
Write-Output "托盘隔离验证通过：$($results.Count)种模式，历史条目 $before -> $after；$output"

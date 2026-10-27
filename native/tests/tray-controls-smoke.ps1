param([string]$Executable='native/out/2.0.0-preview.17-delivery/DesktopPhotoFrameNative.exe',[string]$ExpectedVersion='2.0.0-preview.17')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class TrayControlProbe {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback,IntPtr p);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder value,int length);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h,StringBuilder value,int length);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint message,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint message,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
 public static IntPtr Find(int processId,string title){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint pid;GetWindowThreadProcessId(h,out pid);if(pid==(uint)processId){var text=new StringBuilder(512);GetWindowText(h,text,512);if(text.ToString().Contains(title))found=h;}return true;},IntPtr.Zero);return found;}
 public static int PopupMenus(int processId){int count=0;EnumWindows((h,p)=>{uint pid;GetWindowThreadProcessId(h,out pid);if(pid==(uint)processId){var name=new StringBuilder(128);GetClassName(h,name,128);if(name.ToString()=="#32768")count++;}return true;},IntPtr.Zero);return count;}
 public static string Title(IntPtr h){var text=new StringBuilder(512);GetWindowText(h,text,512);return text.ToString();}
}
'@
$root=Split-Path (Split-Path $PSScriptRoot)
$binary=[IO.Path]::GetFullPath((Join-Path $root $Executable))
$output=Join-Path $root ('native/artifacts/tray-controls-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$data=Join-Path $output 'data';$photos=Join-Path $output 'photos'
New-Item -ItemType Directory -Path $data,$photos -Force | Out-Null
$fixtures=Join-Path $root 'test-artifacts/v3-1.25-1790082981600/photos'
Copy-Item -LiteralPath (Join-Path $fixtures 'a-landscape.bmp'),(Join-Path $fixtures 'b-portrait.bmp') -Destination $photos
$initial=@{playlists=@(@{id='first';name='风景照片';sources=@(@{type='folder';path=$photos});config=@{interval=300;transition='none'}},@{id='second';name='生活记录';sources=@(@{type='folder';path=$photos});config=@{interval=300;transition='none'}});activePlaylistId='first';global=@{paused=$false}}
$config=Join-Path $data 'settings.json';$initial | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $config -Encoding utf8
$process=Start-FrameTest $binary -WindowStyle Hidden -PassThru -ArgumentList @('--data-dir',('"'+$data+'"'),'--test-seconds','30')
$checks=[System.Collections.Generic.List[string]]::new()
function Check($condition,[string]$label){if(!$condition){throw $label};$checks.Add($label)}
function State {Get-Content -LiteralPath $config -Raw | ConvertFrom-Json}
function Command($window,[int]$id){[TrayControlProbe]::SendMessage($window,273,[IntPtr]$id,[IntPtr]::Zero)|Out-Null}
try {
 $main=[IntPtr]::Zero
 for($i=0;$i -lt 80 -and $main -eq [IntPtr]::Zero;$i++){Start-Sleep -Milliseconds 100;$main=[TrayControlProbe]::Find($process.Id,'桌面相框 原生版')}
 Check ($main -ne [IntPtr]::Zero) '相框启动'
 for($i=0;$i -lt 80 -and !([TrayControlProbe]::Title($main).Contains('.bmp'));$i++){Start-Sleep -Milliseconds 100}
 Check ([TrayControlProbe]::Title($main).Contains('.bmp')) '照片已显示'
 Start-Sleep -Milliseconds 350
 Check ([TrayControlProbe]::Find($process.Id,'相框工具栏') -eq [IntPtr]::Zero) '未创建悬浮工具栏'
 Command $main 13
 Check ((State).global.paused) '暂停命令生效'
 $before=[TrayControlProbe]::Title($main);Command $main 15
 for($i=0;$i -lt 80 -and [TrayControlProbe]::Title($main) -eq $before;$i++){Start-Sleep -Milliseconds 100}
 Check ([TrayControlProbe]::Title($main) -ne $before) '下一张命令生效'
 Command $main 29;Start-Sleep -Milliseconds 250
 Check ([TrayControlProbe]::Find($process.Id,'查看大图') -ne [IntPtr]::Zero) '大图命令生效'
 Command $main 16;Start-Sleep -Milliseconds 150
 Check (![TrayControlProbe]::IsWindowVisible($main)) '相框隐藏到托盘'
 $input=[TrayControlProbe]::Find($process.Id,'相框交互区域')
 [TrayControlProbe]::PostMessage($main,32788,[IntPtr]::Zero,[IntPtr]517)|Out-Null;Start-Sleep -Milliseconds 250
 Check ([TrayControlProbe]::PopupMenus($process.Id) -gt 0) '隐藏时托盘右键弹出菜单'
 [TrayControlProbe]::PostMessage($input,31,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 for($i=0;$i -lt 20 -and [TrayControlProbe]::PopupMenus($process.Id) -gt 0;$i++){Start-Sleep -Milliseconds 50}
 Check ([TrayControlProbe]::PopupMenus($process.Id) -eq 0) '托盘菜单可关闭'
 [TrayControlProbe]::PostMessage($main,32788,[IntPtr]::Zero,[IntPtr]514)|Out-Null;Start-Sleep -Milliseconds 200
 Check ([TrayControlProbe]::IsWindowVisible($main)) '托盘左键恢复相框'
 Command $main 10;Start-Sleep -Milliseconds 250
 $settings=[TrayControlProbe]::Find($process.Id,'照片与设置')
 Check ($settings -ne [IntPtr]::Zero -and [TrayControlProbe]::Title($settings).Contains($ExpectedVersion)) '设置命令与版本号'
 $list=[TrayControlProbe]::GetDlgItem($settings,100)
 [TrayControlProbe]::SendMessage($list,390,[IntPtr]1,[IntPtr]::Zero)|Out-Null
 [TrayControlProbe]::SendMessage($settings,273,[IntPtr]65636,[IntPtr]::Zero)|Out-Null
 Check ((State).activePlaylistId -eq 'second') '播放列表可切换'
 Command $main 17
 Check ($process.WaitForExit(5000)) '退出命令结束进程'
 @{checks=$checks;passed=$checks.Count;executable=$binary} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output 'summary.json') -Encoding utf8
 "通过 $($checks.Count) 项工具栏移除与托盘命令检查：$output"
} finally {if(!$process.HasExited){Stop-Process -Id $process.Id -Force}}

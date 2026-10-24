param([string]$Executable='native/out/2.0.0-preview.13-delivery/DesktopPhotoFrameNative.exe')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class ResizeEdgeProbe {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left,top,right,bottom; }
 [StructLayout(LayoutKind.Sequential)] public struct MonitorInfo { public int size; public Rect screen,work; public int flags; }
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f,IntPtr p);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint message,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
 [DllImport("user32.dll")] public static extern IntPtr MonitorFromRect(ref Rect rect,uint flags);
 [DllImport("user32.dll")] public static extern bool GetMonitorInfo(IntPtr h,ref MonitorInfo info);
 public static IntPtr Find(int processId){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);if(id==(uint)processId){var title=new StringBuilder(128);GetWindowText(h,title,128);if(title.ToString()=="相框交互区域")found=h;}return true;},IntPtr.Zero);return found;}
 public static IntPtr Point(int x,int y){return (IntPtr)(((long)(ushort)y<<16)|(ushort)x);}
}
'@
[ResizeEdgeProbe]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
$root=Split-Path (Split-Path $PSScriptRoot)
$binary=[IO.Path]::GetFullPath((Join-Path $root $Executable))
$output=Join-Path $root ('native/artifacts/resize-edges-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $output -Force | Out-Null
$virtualX=[ResizeEdgeProbe]::GetSystemMetrics(76)
$virtualY=[ResizeEdgeProbe]::GetSystemMetrics(77)
$probe=[ResizeEdgeProbe+Rect]::new();$probe.left=0;$probe.top=0;$probe.right=1;$probe.bottom=1
$monitor=[ResizeEdgeProbe+MonitorInfo]::new();$monitor.size=[Runtime.InteropServices.Marshal]::SizeOf($monitor)
if(![ResizeEdgeProbe]::GetMonitorInfo([ResizeEdgeProbe]::MonitorFromRect([ref]$probe,2),[ref]$monitor)){throw '无法读取显示器工作区'}

function State($path){Get-Content -LiteralPath $path -Raw | ConvertFrom-Json}
function Drag($window,$bounds,[string]$side){
 $x=[int]($bounds.x+$bounds.width/2-$virtualX);$y=[int]($bounds.y+$bounds.height/2-$virtualY)
 $dx=0;$dy=0
 switch($side){
  'left' {$x=[int]($bounds.x+3-$virtualX);$dx=-40}
  'right' {$x=[int]($bounds.x+$bounds.width-3-$virtualX);$dx=40}
  'top' {$y=[int]($bounds.y+3-$virtualY);$dy=-40}
  'bottom' {$y=[int]($bounds.y+$bounds.height-3-$virtualY);$dy=40}
 }
 [ResizeEdgeProbe]::SendMessage($window,513,[IntPtr]::Zero,[ResizeEdgeProbe]::Point($x,$y)) | Out-Null
 [ResizeEdgeProbe]::SendMessage($window,512,[IntPtr]1,[ResizeEdgeProbe]::Point(($x+$dx),($y+$dy))) | Out-Null
 [ResizeEdgeProbe]::SendMessage($window,514,[IntPtr]::Zero,[ResizeEdgeProbe]::Point(($x+$dx),($y+$dy))) | Out-Null
}
function Run-Case([string]$name,[string]$side,[string]$ratio,[bool]$locked,[bool]$atRight){
 $data=Join-Path $output $name;New-Item -ItemType Directory -Path $data -Force | Out-Null
 $x=if($atRight){$monitor.work.right-200}else{$monitor.work.left+200}
 $y=$monitor.work.top+200
 $style=if($atRight){'borderless'}else{'minimal'}
 $initial=@{playlists=@(@{id='resize';name='缩放测试';sources=@();config=@{ratio=$ratio;frame=$style;anchor='top-right';thickness=18;transition='none'}});activePlaylistId='resize';global=@{paused=$true;lockPosition=$locked};bounds=@{x=$x;y=$y;width=200;height=150}}
 $config=Join-Path $data 'settings.json';$initial | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $config -Encoding utf8
 $process=Start-FrameTest $binary -WindowStyle Hidden -PassThru -ArgumentList @('--data-dir',('"'+$data+'"'),'--test-seconds','20')
 try {
  $window=[IntPtr]::Zero
  for($i=0;$i -lt 50 -and $window -eq [IntPtr]::Zero;$i++){Start-Sleep -Milliseconds 100;$window=[ResizeEdgeProbe]::Find($process.Id)}
  if($window -eq [IntPtr]::Zero){throw "$name：相框没有启动"}
  for($i=0;$i -lt 50 -and (State $config).bounds.height -eq 150;$i++){Start-Sleep -Milliseconds 100}
  $before=(State $config).bounds
  if($before.height -eq 150){throw "$name：相框未完成初始化"}
  Drag $window $before $side
  $after=(State $config).bounds
  if($side -eq 'left' -and !($after.x -lt $before.x -and $after.width -gt $before.width)){throw "$name：左边缘无效"}
  if($side -eq 'right' -and !($after.width -gt $before.width)){throw "$name：右边缘无效"}
  if($side -eq 'top' -and !($after.y -lt $before.y -and $after.height -gt $before.height)){throw "$name：上边缘无效"}
  if($side -eq 'bottom' -and !($after.height -gt $before.height)){throw "$name：下边缘无效"}
  if($atRight -and !($after.x -lt $before.x -and $after.width -gt $before.width -and [Math]::Abs(($after.x+$after.width)-($before.x+$before.width)) -lt 2)){throw "$name：贴右边时没有向左扩展"}
  if($ratio -eq 'auto'){$border=if($style -eq 'borderless'){0}else{18};$oldRatio=($before.width-2*$border)/($before.height-2*$border);$newRatio=($after.width-2*$border)/($after.height-2*$border);if([Math]::Abs($oldRatio-$newRatio) -gt .02){throw "$name：自适应比例发生变化"}}
  [pscustomobject]@{case=$name;side=$side;locked=$locked;before=$before;after=$after}
 } finally {if(!$process.HasExited){Stop-Process -Id $process.Id -Force}}
}
$results=@()
$results+=Run-Case 'free-left-locked' 'left' 'free' $true $false
$results+=Run-Case 'free-right' 'right' 'free' $false $false
$results+=Run-Case 'free-top' 'top' 'free' $false $false
$results+=Run-Case 'free-bottom' 'bottom' 'free' $false $false
$results+=Run-Case 'auto-left' 'left' 'auto' $false $false
$results+=Run-Case 'auto-right' 'right' 'auto' $false $false
$results+=Run-Case 'auto-top' 'top' 'auto' $false $false
$results+=Run-Case 'auto-bottom' 'bottom' 'auto' $false $false
$results+=Run-Case 'auto-bottom-right-edge' 'bottom' 'auto' $false $true
$results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'summary.json') -Encoding utf8
"通过 $($results.Count) 项边缘缩放检查：$output"

param([string]$Executable='native/out/2.0.0-preview.19-delivery/DesktopPhotoFrameNative.exe')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
. (Join-Path $PSScriptRoot 'frame-tray-history.ps1')
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class Review15Probe {
 public delegate bool Proc(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int left,top,right,bottom;}
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]public static extern bool EnumWindows(Proc f,IntPtr p);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,StringBuilder value,int n);
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr z,int x,int y,int width,int height,uint flags);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint flags,uint timeout,out IntPtr value);
 public static IntPtr Find(int pid,string part){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);var t=new StringBuilder(512);GetWindowText(h,t,512);if(id==pid&&t.ToString().Contains(part))found=h;return true;},IntPtr.Zero);return found;}
}
'@
Add-Type -AssemblyName System.Drawing
[Review15Probe]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
$root=Split-Path (Split-Path $PSScriptRoot);$output=Join-Path $root ('native/artifacts/settings-review-'+(Get-Date -Format yyyyMMdd-HHmmss));$data=Join-Path $output 'data';$photos=Join-Path $output 'photos'
New-Item -ItemType Directory -Path $data,$photos -Force|Out-Null
foreach($i in 0,1){$b=[Drawing.Bitmap]::new(720,480);$g=[Drawing.Graphics]::FromImage($b);$g.Clear([Drawing.Color]::FromArgb(50+$i*90,140,160));$b.Save((Join-Path $photos "$i.bmp"));$g.Dispose();$b.Dispose()}
@{global=@{paused=$true;theme='dark'};playlists=@(@{id='review';name='审查回归';sources=@(@{type='folder';path=$photos});config=@{ratio='auto';transition='none'}});settingsBounds=@{x=30;y=30;width=1280;height=800}}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $data 'settings.json') -Encoding utf8
$before=@(Get-FrameTestTrayHistory);$checks=@();$main=[IntPtr]::Zero
$process=Start-FrameTest $Executable -WindowStyle Hidden -PassThru -ArgumentList @('--data-dir',('"'+$data+'"'),'--test-hooks','--test-seconds','50')
function Message($h,$m,[long]$w=0,[long]$l=0){$result=[IntPtr]::Zero;if([Review15Probe]::SendMessageTimeout($h,$m,[IntPtr]$w,[IntPtr]$l,2,2500,[ref]$result)-eq[IntPtr]::Zero){throw '窗口消息超时'};return $result.ToInt64()}
function Control($id){[Review15Probe]::GetDlgItem($settings,$id)}
function Command($id){Message $settings 273 $id|Out-Null}
function Caption{$text=[Text.StringBuilder]::new(512);[Review15Probe]::GetWindowText((Control 702),$text,512)|Out-Null;$text.ToString()}
function Bounds($h){$r=[Review15Probe+Rect]::new();[Review15Probe]::GetWindowRect($h,[ref]$r)|Out-Null;$r}
try{
 for($i=0;$i-lt60-and$main-eq[IntPtr]::Zero;$i++){Start-Sleep -Milliseconds 80;$main=[Review15Probe]::Find($process.Id,'原生版')};if($main-eq[IntPtr]::Zero){throw '启动失败'};Message $main 273 10|Out-Null;$settings=[Review15Probe]::Find($process.Id,'照片与设置')
 foreach($i in 0,1){Message $main 32829 $i|Out-Null;Start-Sleep -Milliseconds 650};$oldIndex=Message $main 32830 0
 if((Message $main 32834 ([int][char]'A') 1)-ne1){throw '缺少线程内键盘测试接口'};Message $main 32834 37 1|Out-Null
 $checks+=@{name='Ctrl方向键后预览与焦点同步';passed=((Caption)-eq'0.bmp')};$checks+=@{name='焦点移动不改变轮播照片';passed=((Message $main 32830 0)-eq$oldIndex)}
 Message $main 32831 100|Out-Null;[Review15Probe]::SetWindowPos($settings,[IntPtr]::Zero,20,20,640,480,64)|Out-Null;Command 401
 $checks+=@{name='最小窗口仍可见外观参数';passed=([Review15Probe]::IsWindowVisible((Control 410)) -or [Review15Probe]::IsWindowVisible((Control 440)))}
 $pinned=Bounds (Control 201);foreach($i in 1..20){Message $settings 277 3|Out-Null}
 $checks+=@{name='最小窗口可滚动到尺寸参数';passed=[Review15Probe]::IsWindowVisible((Control 453))};$checks+=@{name='最小窗口预览保持固定';passed=((Bounds (Control 201)).top-eq$pinned.top)}
 $after=@(Get-FrameTestTrayHistory);$checks+=@{name='没有新增测试托盘';passed=(@($after|Where-Object{$_.path-ieq$process.Path-and$_.key-notin@($before.key)}).Count-eq0)}
 $passed=@($checks|Where-Object {!$_.passed}).Count-eq0;@{passed=$passed;checks=$checks;window=(Bounds $settings);realSystemDpi=$false}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $output 'summary.json') -Encoding utf8;$checks|ConvertTo-Json;if(!$passed){throw "审查回归未通过：$output"};Write-Output "6 项审查回归通过；$output"
}finally{if(!$process.HasExited){if($main-ne[IntPtr]::Zero){[Review15Probe]::PostMessage($main,273,[IntPtr]17,[IntPtr]::Zero)|Out-Null};if(!$process.WaitForExit(5000)){Stop-Process -Id $process.Id}}}

param([string]$Executable='native/out/2.0.0-preview.18-delivery/DesktopPhotoFrameNative.exe',[switch]$FullSweep,[ValidateRange(0,1000)][int]$PhotoCount=24,[ValidateSet('light','dark')][string]$Theme='dark')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
. (Join-Path $PSScriptRoot 'frame-tray-history.ps1')
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class SettingsResizeProbe {
 public delegate bool Proc(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int left,top,right,bottom;}
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]public static extern bool EnumWindows(Proc f,IntPtr p);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,StringBuilder value,int n);
 [DllImport("user32.dll")]public static extern bool GetClientRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern bool SystemParametersInfo(uint action,uint parameter,out Rect r,uint flags);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr z,int x,int y,int width,int height,uint flags);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint flags,uint timeout,out IntPtr value);
 public static IntPtr Find(int pid,string part){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);if(id!=pid)return true;var t=new StringBuilder(512);GetWindowText(h,t,512);if(t.ToString().Contains(part))found=h;return true;},IntPtr.Zero);return found;}
}
'@
Add-Type -AssemblyName System.Drawing
[SettingsResizeProbe]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
$work=[SettingsResizeProbe+Rect]::new();if(![SettingsResizeProbe]::SystemParametersInfo(48,0,[ref]$work,0)){throw '无法读取测试显示器工作区'}
$maxTestWidth=$work.right-$work.left-40;$maxTestHeight=$work.bottom-$work.top-40
$root=Split-Path (Split-Path $PSScriptRoot);$output=Join-Path $root ('native/artifacts/settings-resize-'+(Get-Date -Format yyyyMMdd-HHmmss));$data=Join-Path $output 'data';$photos=Join-Path $output 'photos'
New-Item -ItemType Directory -Path $data,$photos -Force|Out-Null
for($i=0;$i-lt$PhotoCount;$i++){$b=[Drawing.Bitmap]::new(96,64);$g=[Drawing.Graphics]::FromImage($b);$g.Clear([Drawing.Color]::FromArgb(40+($i%24)*7,130,160));$b.Save((Join-Path $photos "$i.bmp"));$g.Dispose();$b.Dispose()}
@{global=@{paused=$true;theme=$Theme};playlists=@(@{id='resize';name='窗口缩放回归';sources=@(@{type='folder';path=$photos});config=@{ratio='auto';transition='none'}});settingsBounds=@{x=30;y=30;width=1280;height=800}}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $data 'settings.json') -Encoding utf8
$before=@(Get-FrameTestTrayHistory);$samples=[Collections.Generic.List[object]]::new();$main=[IntPtr]::Zero;$failure=$null;$binaryPath=(Resolve-Path $Executable).Path
$process=Start-FrameTest $Executable -WindowStyle Hidden -PassThru -ArgumentList @('--data-dir',('"'+$data+'"'),'--test-hooks','--test-seconds','600')
function Message($h,$m,[long]$w=0,[long]$l=0){$result=[IntPtr]::Zero;if([SettingsResizeProbe]::SendMessageTimeout($h,$m,[IntPtr]$w,[IntPtr]$l,2,2000,[ref]$result)-eq[IntPtr]::Zero){throw "窗口消息超时：$m / $w"};return $result.ToInt64()}
try{
 for($i=0;$i-lt60-and$main-eq[IntPtr]::Zero;$i++){Start-Sleep -Milliseconds 80;$main=[SettingsResizeProbe]::Find($process.Id,'原生版')};if($main-eq[IntPtr]::Zero){throw '启动失败'}
 Message $main 273 10|Out-Null;$settings=[SettingsResizeProbe]::Find($process.Id,'照片与设置');Message $main 32831 100|Out-Null
 $pages=if($FullSweep){@(400,401,402,403)}else{@(400)};$scales=if($FullSweep){@(100,125,150)}else{@(100)}
 foreach($scale in $scales){Message $main 32831 $scale|Out-Null;foreach($page in $pages){Message $settings 273 $page|Out-Null;Write-Output "检查设置页 $page，布局比例 $scale%"
  $sizes=if($FullSweep){foreach($height in @(480,619,620,720,900,1100)){foreach($width in @(1280,1180,1100,1040,1010,1000,990,980,970,960,900,850,840,830,820,810,800,700,640)){@{w=[int]($width*$scale/100);h=[int]($height*$scale/100)}}}}else{foreach($width in @(1280,1100,1010,1000,990,980,970,960,1050,1280)){@{w=$width;h=720}}}
  foreach($size in $sizes){$requestedWidth=$size.w;$requestedHeight=$size.h;$size.w=[Math]::Min($size.w,$maxTestWidth);$size.h=[Math]::Min($size.h,$maxTestHeight);$sample=@{scale=$scale;page=$page;width=$size.w;height=$size.h;requestedWidth=$requestedWidth;requestedHeight=$requestedHeight;responsive=$false};$samples.Add($sample)
   # 异步提交跨线程缩放，测试程序自身不能被被测窗口的布局死循环拖住。
   [SettingsResizeProbe]::SetWindowPos($settings,[IntPtr]::Zero,$work.left+20,$work.top+20,$size.w,$size.h,16404)|Out-Null
   $watch=[Diagnostics.Stopwatch]::StartNew();$actual=[SettingsResizeProbe+Rect]::new()
   do{Start-Sleep -Milliseconds 12;Message $settings 0|Out-Null;[SettingsResizeProbe]::GetWindowRect($settings,[ref]$actual)|Out-Null;$applied=($actual.right-$actual.left-eq$size.w-and$actual.bottom-$actual.top-eq$size.h)}while(!$applied-and$watch.ElapsedMilliseconds-lt2000)
   $sample.actualWidth=$actual.right-$actual.left;$sample.actualHeight=$actual.bottom-$actual.top
   if(!$applied){throw "目标窗口尺寸未生效：$($size.w)×$($size.h)"};Message $main 32830 20|Out-Null
   if($page-eq400){$title=[SettingsResizeProbe+Rect]::new();$source=[SettingsResizeProbe+Rect]::new();[SettingsResizeProbe]::GetWindowRect([SettingsResizeProbe]::GetDlgItem($settings,700),[ref]$title)|Out-Null;[SettingsResizeProbe]::GetWindowRect([SettingsResizeProbe]::GetDlgItem($settings,461),[ref]$source)|Out-Null;if($title.top-eq$source.top){$sample.headingNoOverlap=$title.right-le$source.left;if(!$sample.headingNoOverlap){throw '照片页标题遮挡来源按钮'}}}
   $sample.responseMs=$watch.Elapsed.TotalMilliseconds;$sample.responsive=$true
  }
 }}
}catch{$failure=$_.Exception.Message}finally{
 if(!$process.HasExited){if($main-ne[IntPtr]::Zero){[SettingsResizeProbe]::PostMessage($main,273,[IntPtr]17,[IntPtr]::Zero)|Out-Null};if(!$process.WaitForExit(5000)){Stop-Process -Id $process.Id;if(!$failure){$failure='缩放检查后无法正常退出'}}}
 if(!$failure-and$process.ExitCode-ne0){$failure="退出码异常：$($process.ExitCode)"}
 $after=@(Get-FrameTestTrayHistory);$added=@($after|Where-Object{$_.path-ieq$binaryPath-and$_.key-notin@($before.key)}).Count
 $summary=@{passed=(!$failure-and$added-eq0);failure=$failure;count=$samples.Count;exitCode=$process.ExitCode;newTestTrayRecords=$added;photoCount=$PhotoCount;theme=$Theme;realSystemDpi=$false;samples=$samples}
 $summary|ConvertTo-Json -Depth 7|Set-Content (Join-Path $output 'summary.json') -Encoding utf8
 Write-Output (@{passed=$summary.passed;failure=$failure;count=$samples.Count;last=$samples[-1];output=$output}|ConvertTo-Json -Depth 4)
}
if($failure){throw $failure};if($added-ne0){throw '出现测试托盘记录'}

param([string]$Executable='native/out/2.0.0-preview.15-delivery/DesktopPhotoFrameNative.exe',[int]$SampleMilliseconds=2200)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
. (Join-Path $PSScriptRoot 'frame-tray-history.ps1')
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class IdleCompositionProbe {
 public delegate bool Callback(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int left,top,right,bottom;}
 [DllImport("user32.dll")]static extern bool EnumWindows(Callback f,IntPtr p);
 [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr h,out uint id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint msg,IntPtr w,IntPtr l);
 [DllImport("user32.dll",SetLastError=true)]static extern IntPtr SendMessageTimeout(IntPtr h,uint msg,IntPtr w,IntPtr l,uint flags,uint timeout,out IntPtr result);
 public static IntPtr Find(int pid){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);if(id==(uint)pid){var title=new StringBuilder(256);GetWindowText(h,title,title.Capacity);if(title.ToString().StartsWith("桌面相框 原生版"))found=h;}return true;},IntPtr.Zero);return found;}
 public static long Message(IntPtr h,uint msg,long w=0){IntPtr value;if(SendMessageTimeout(h,msg,new IntPtr(w),IntPtr.Zero,2,500,out value)==IntPtr.Zero)throw new Exception("测试窗口响应超时");return value.ToInt64();}
}
'@
[IdleCompositionProbe]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
$root=Split-Path (Split-Path $PSScriptRoot)
$output=Join-Path $root ('native/artifacts/composition-idle-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$data=Join-Path $output 'data';$photos=Join-Path $output 'photos'
New-Item -ItemType Directory -Path $data,$photos -Force|Out-Null
foreach($shape in @(@(960,640),@(640,960))){$b=[Drawing.Bitmap]::new($shape[0],$shape[1]);$g=[Drawing.Graphics]::FromImage($b);$g.Clear([Drawing.Color]::FromArgb(32,122,127));$b.Save((Join-Path $photos ($shape[0].ToString()+'.bmp')));$g.Dispose();$b.Dispose()}
@{activePlaylistId='idle';playlists=@(@{id='idle';name='合成空闲测试';sources=@(@{type='folder';path=$photos});config=@{frame='minimal';ratio='auto';anchor='bottom-right';transition='fade';interval=3600}});global=@{paused=$true;pauseFullscreen=$false};bounds=@{x=100;y=100;width=420;height=300}}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $data 'settings.json') -Encoding utf8
$before=@(Get-FrameTestTrayHistory)
$process=Start-FrameTest $Executable -ArgumentList @('--data-dir',('"'+$data+'"'),'--test-hooks','--test-seconds','45') -WindowStyle Hidden -PassThru
$main=[IntPtr]::Zero;$phases=@();$checks=@()
function Query([int]$Index){[IdleCompositionProbe]::Message($main,32830,$Index)}
function Command([int]$Id){[IdleCompositionProbe]::Message($main,273,$Id)|Out-Null}
function Wait-Photo {for($i=0;$i -lt 100;$i++){if((Query 23) -and !((Query 7)-band 48)){return};Start-Sleep -Milliseconds 40};throw '测试照片未准备完成'}
function Measure-Idle([string]$Name){
 Start-Sleep -Milliseconds 250
 $ticks=Query 14;$commits=Query 15;$paints=Query 16;$clock=[Diagnostics.Stopwatch]::StartNew();$process.Refresh();$cpu=$process.TotalProcessorTime.TotalSeconds
 Start-Sleep -Milliseconds $SampleMilliseconds
 $seconds=$clock.Elapsed.TotalSeconds;$process.Refresh();$used=$process.TotalProcessorTime.TotalSeconds-$cpu
 $r=[IdleCompositionProbe+Rect]::new();[IdleCompositionProbe]::GetWindowRect($main,[ref]$r)|Out-Null
 [pscustomobject]@{Phase=$Name;Seconds=$seconds;WakeupsPerSecond=((Query 14)-$ticks)/$seconds;Commits=(Query 15)-$commits;Paints=(Query 16)-$paints;CoreEquivalentCpuPercent=$used/$seconds*100;TimerMilliseconds=(Query 17);CanvasWidth=$r.right-$r.left;CanvasHeight=$r.bottom-$r.top;PhotoWidth=(Query 1);PhotoHeight=(Query 2)}
}
try{
 for($i=0;$i -lt 80 -and $main -eq [IntPtr]::Zero;$i++){Start-Sleep -Milliseconds 40;$main=[IdleCompositionProbe]::Find($process.Id)}
 if($main -eq [IntPtr]::Zero -or (Query 20) -ne 1){throw '缺少合成回归观测接口'}
 Wait-Photo;$phases+=Measure-Idle 'paused'
 Command 13;Wait-Photo;Start-Sleep -Milliseconds 300;$phases+=Measure-Idle 'playing-between-transitions'
 Command 16;$phases+=Measure-Idle 'hidden';Command 16
 foreach($phase in $phases){
  $checks+=@{name=($phase.Phase+'：空闲无重复提交');passed=($phase.Commits -eq 0)}
  $limit=if($phase.Phase -eq 'hidden'){3}else{12}
  $checks+=@{name=($phase.Phase+'：低频唤醒');passed=($phase.WakeupsPerSecond -le $limit)}
  $ratio=($phase.CanvasWidth*$phase.CanvasHeight)/[Math]::Max(1,($phase.PhotoWidth*$phase.PhotoHeight))
  $checks+=@{name=($phase.Phase+'：画布不铺满双屏');passed=($ratio -le 4)}
 }
 $after=@(Get-FrameTestTrayHistory)
 $new=@($after|Where-Object {$_.path -ieq $process.Path -and $_.key -notin @($before.key)});$checks+=@{name='无新增托盘历史';passed=($new.Count -eq 0)}
 $report=@{executable=$Executable;phases=$phases;checks=$checks;passed=@($checks|Where-Object {!$_.passed}).Count -eq 0;limits='不截图、不调用GetPixel；CPU为单核等效，仅作观察，不据此断言DWM因果。'}
 $report|ConvertTo-Json -Depth 7|Set-Content (Join-Path $output 'summary.json') -Encoding utf8
 $report|ConvertTo-Json -Depth 7
 if(!$report.passed){throw "合成空闲回归未通过：$output"}
 Write-Output "合成空闲回归通过；$output"
}finally{if(!$process.HasExited){if($main -ne [IntPtr]::Zero){[IdleCompositionProbe]::PostMessage($main,273,[IntPtr]17,[IntPtr]::Zero)|Out-Null};if(!$process.WaitForExit(5000)){Stop-Process -Id $process.Id}}}

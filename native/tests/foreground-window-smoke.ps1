param([string]$Executable='native/out/2.0.0-preview.20-delivery/DesktopPhotoFrameNative.exe',[int]$FrameX=140,[switch]$AlwaysOnTop)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class FrameForegroundProbe {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int left,top,right,bottom;}
 [StructLayout(LayoutKind.Sequential)]public struct Point{public int x,y;}
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 [DllImport("user32.dll")]public static extern bool EnumWindows(EnumProc callback,IntPtr p);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint process);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,StringBuilder value,int length);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect rect);
 [DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int action);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr z,int x,int y,int width,int height,uint flags);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern IntPtr WindowFromPoint(Point point);
 [DllImport("user32.dll")]public static extern IntPtr GetWindowLongPtr(IntPtr h,int index);
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]public static extern void mouse_event(uint flags,uint x,uint y,uint data,UIntPtr extra);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint message,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern IntPtr SendMessage(IntPtr h,uint message,IntPtr w,IntPtr l);
 public static IntPtr Find(int processId,string title){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);if(id==(uint)processId){var value=new StringBuilder(512);GetWindowText(h,value,512);if(value.ToString().Contains(title))found=h;}return true;},IntPtr.Zero);return found;}
}
'@
Add-Type -AssemblyName System.Drawing
[FrameForegroundProbe]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
$root=Split-Path (Split-Path $PSScriptRoot)
$binary=[IO.Path]::GetFullPath((Join-Path $root $Executable))
$output=Join-Path $root ('native/artifacts/foreground-window-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$data=Join-Path $output 'data';$photos=Join-Path $output 'photos'
New-Item -ItemType Directory -Path $data,$photos -Force|Out-Null
foreach($i in 0,1){$picture=[Drawing.Bitmap]::new(640,400);$paint=[Drawing.Graphics]::FromImage($picture);$paint.Clear([Drawing.Color]::FromArgb(80+$i*80,145,170));$picture.Save((Join-Path $photos "sample-$i.bmp"));$paint.Dispose();$picture.Dispose()}
$settings=Join-Path $data 'settings.json'
@{bounds=@{x=$FrameX;y=120;width=420;height=300};global=@{paused=$true;pauseFullscreen=$false;alwaysOnTop=[bool]$AlwaysOnTop};playlists=@(@{id='focus';name='前台验证';sources=@(@{type='folder';path=$photos});config=@{ratio='free';transition='none'}})}|ConvertTo-Json -Depth 7|Set-Content $settings -Encoding utf8
$process=Start-FrameTest -FilePath $binary -PassThru -ArgumentList @('--data-dir',('"'+$data+'"'),'--test-hooks','--test-seconds','60')
$checks=[Collections.Generic.List[string]]::new();$observations=[Collections.Generic.List[object]]::new()
function Check($condition,[string]$name){if(!$condition){throw "失败：$name"};$checks.Add($name)}
function Rect($h){$r=[FrameForegroundProbe+Rect]::new();[FrameForegroundProbe]::GetWindowRect($h,[ref]$r)|Out-Null;return $r}
function Width($r){$r.right-$r.left}
function Height($r){$r.bottom-$r.top}
function Mouse([int]$x,[int]$y,[uint32]$down,[uint32]$up){[FrameForegroundProbe]::SetCursorPos($x,$y)|Out-Null;Start-Sleep -Milliseconds 80;[FrameForegroundProbe]::mouse_event($down,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 90;[FrameForegroundProbe]::mouse_event($up,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 180}
function Foreground([string]$action){$h=[FrameForegroundProbe]::GetForegroundWindow();$r=Rect $h;$observations.Add(@{action=$action;handle=$h.ToInt64();width=(Width $r);height=(Height $r)});return $h}
function SendMouse($h,[uint32]$message,[int]$flags,[int]$screenX,[int]$screenY){[FrameForegroundProbe]::SetCursorPos($screenX,$screenY)|Out-Null;$r=Rect $h;$x=$screenX-$r.left;$y=$screenY-$r.top;$point=($x -band 65535) -bor (($y -band 65535) -shl 16);[FrameForegroundProbe]::SendMessage($h,$message,[IntPtr]$flags,[IntPtr]$point)|Out-Null}
try{
 $main=[IntPtr]::Zero;for($i=0;$i -lt 80 -and $main -eq [IntPtr]::Zero;$i++){Start-Sleep -Milliseconds 100;$main=[FrameForegroundProbe]::Find($process.Id,'桌面相框 原生版')}
 Check ($main -ne [IntPtr]::Zero) '独立相框启动'
 $input=[FrameForegroundProbe]::Find($process.Id,'相框交互区域');Check ($input -ne [IntPtr]::Zero) '交互窗口存在'
 if(![FrameForegroundProbe]::IsWindowVisible($main)){[FrameForegroundProbe]::ShowWindow($main,4)|Out-Null;[FrameForegroundProbe]::ShowWindow($input,4)|Out-Null;Start-Sleep -Milliseconds 120}
 Check ([FrameForegroundProbe]::IsWindowVisible($main) -and [FrameForegroundProbe]::IsWindowVisible($input)) '测试窗口可见'
 $visual=Rect $main;$hit=Rect $input
 Check ((Width $hit) -lt 800 -and (Height $hit) -lt 600 -and (Width $visual) -gt (Width $hit)) '交互窗口不覆盖桌面'
 Check (([FrameForegroundProbe]::GetWindowLongPtr($main,-20).ToInt64() -band 0x08000000) -ne 0) '全屏绘图窗口不会因点击激活'
 # 把受控窗口放到普通窗口组前方，避免把其他窗口遮挡误判成点击区域回归；不改变置顶开关。
 [FrameForegroundProbe]::SetWindowPos($main,[IntPtr]::Zero,0,0,0,0,19)|Out-Null
 [FrameForegroundProbe]::SetWindowPos($input,[IntPtr]::Zero,0,0,0,0,19)|Out-Null
 $inside=[FrameForegroundProbe+Point]::new();$inside.x=$hit.left+[int]((Width $hit)/2);$inside.y=$hit.top+[int]((Height $hit)/2)
 $insideWindow=[FrameForegroundProbe]::WindowFromPoint($inside);$observations.Add(@{action='内部命中';x=$inside.x;y=$inside.y;inputRect=$hit;inputVisible=[FrameForegroundProbe]::IsWindowVisible($input);visualVisible=[FrameForegroundProbe]::IsWindowVisible($main);frameState=[FrameForegroundProbe]::SendMessage($main,32830,[IntPtr]7,[IntPtr]::Zero).ToInt64();handle=$insideWindow.ToInt64();input=$input.ToInt64();visual=$main.ToInt64()})
 Check ($insideWindow -eq $input) '照片内部点击命中交互窗口'
 $outside=[FrameForegroundProbe+Point]::new();$outside.x=$hit.right+20;$outside.y=$hit.bottom+20
 Check ([FrameForegroundProbe]::WindowFromPoint($outside) -ne $input) '相框外仍可点击桌面'
 Mouse $inside.x $inside.y 2 4
 $front=Foreground '左键'
 Check ($front -ne $main) '左键不会激活全屏绘图窗口'
 Check ((Width (Rect $input)) -lt 800 -and (Height (Rect $input)) -lt 600) '左键后交互窗口仍是小窗口'
 $before=Rect $input;$cx=$before.left+[int]((Width $before)/2);$cy=$before.top+[int]((Height $before)/2)
 SendMouse $input 513 1 $cx $cy
 foreach($step in 1..5){SendMouse $input 512 1 ($cx+$step*9) ($cy+$step*5)}
 SendMouse $input 514 0 ($cx+45) ($cy+25)
 $moved=Rect $input;Check ($moved.left -ge $before.left+30 -and $moved.top -ge $before.top+15) '左键拖动后小窗口跟随照片'
 $resizeX=$moved.left+[int]((Width $moved)/2);$resizeY=$moved.bottom-8
 SendMouse $input 513 1 $resizeX $resizeY
 foreach($step in 1..5){SendMouse $input 512 1 $resizeX ($resizeY+$step*10)}
 SendMouse $input 514 0 $resizeX ($resizeY+50)
 $sized=Rect $input;Check ((Height $sized) -ge (Height $moved)+35) '下边缘拖动可调整相框尺寸'
 $hit=Rect $input
 Mouse ($hit.left+10) ($hit.top+10) 8 16
 $front=Foreground '右键菜单'
 Check ($front -ne $main -and (Width (Rect $front)) -lt 800) '右键菜单不会激活全屏绘图窗口'
 [FrameForegroundProbe]::PostMessage($input,31,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;Start-Sleep -Milliseconds 180
 [FrameForegroundProbe]::PostMessage($main,273,[IntPtr]17,[IntPtr]::Zero)|Out-Null;Check ($process.WaitForExit(5000)) '正常退出'
 @{passed=$checks.Count;checks=$checks;observations=$observations;visual=$visual;initialHit=$hit;moved=$moved;resized=$sized;executable=$binary}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $output 'summary.json') -Encoding utf8
 Write-Output "$($checks.Count) 项前台窗口与鼠标交互检查通过；$output"
}catch{@{error=$_.Exception.Message;checks=$checks;observations=$observations;before=$before;moved=$moved;resized=$sized}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $output 'failure.json') -Encoding utf8;throw}
finally{if(!$process.HasExited){[FrameForegroundProbe]::PostMessage($main,273,[IntPtr]17,[IntPtr]::Zero)|Out-Null;if(!$process.WaitForExit(3000)){Stop-Process -Id $process.Id -Force}}}

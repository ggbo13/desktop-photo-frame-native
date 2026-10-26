param([string]$Executable='native/out/2.0.0-preview.14-delivery/DesktopPhotoFrameNative.exe')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class LayoutProbe {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int left,top,right,bottom;}
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]public static extern bool EnumWindows(EnumProc f,IntPtr p);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,StringBuilder text,int n);
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")]public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]public static extern bool GetClientRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]public static extern int GetScrollPos(IntPtr h,int bar);
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int width,int height,uint flags);
 [DllImport("user32.dll")]public static extern bool RedrawWindow(IntPtr h,IntPtr r,IntPtr region,uint flags);
 [DllImport("user32.dll")]public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 public static IntPtr Find(int pid,string title){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);var t=new StringBuilder(512);GetWindowText(h,t,512);if(id==pid&&t.ToString().Contains(title))found=h;return true;},IntPtr.Zero);return found;}
}
'@
Add-Type -AssemblyName System.Drawing
[LayoutProbe]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
$output=Join-Path (Split-Path (Split-Path $PSScriptRoot)) ('native/artifacts/settings-layout-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$data=Join-Path $output 'data';$photos=Join-Path $output 'photos';New-Item -ItemType Directory -Path $data,$photos -Force|Out-Null
for($i=0;$i -lt 32;$i++){$b=[Drawing.Bitmap]::new(640,400);$g=[Drawing.Graphics]::FromImage($b);$g.Clear([Drawing.Color]::FromArgb(50+$i*5,125+$i*3,150));$brush=[Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(60,210,190));$g.FillEllipse($brush,360,70,260,260);$font=[Drawing.Font]::new('Segoe UI',60);$g.DrawString(('{0:00}' -f $i),$font,[Drawing.Brushes]::White,35,115);$b.Save((Join-Path $photos ('photo-{0:00}.bmp' -f $i)));$font.Dispose();$brush.Dispose();$g.Dispose();$b.Dispose()}
$config=Join-Path $data 'settings.json'
@{global=@{paused=$true};playlists=@(@{id='layout';name='布局验证';sources=@(@{type='folder';path=$photos});config=@{frame='wood';ratio='auto';transition='none'}});settingsBounds=@{x=40;y=40;width=980;height=890}}|ConvertTo-Json -Depth 7|Set-Content $config -Encoding utf8
$process=Start-FrameTest $Executable -WindowStyle Hidden -PassThru -ArgumentList @('--data-dir',('"'+$data+'"'),'--test-hooks','--test-seconds','120')
$checks=[Collections.Generic.List[string]]::new()
function Check($value,$name){if(!$value){throw "失败：$name"};$checks.Add($name)}
function Msg($h,[int]$m,[long]$w=0,[long]$l=0){[LayoutProbe]::SendMessage($h,$m,[IntPtr]$w,[IntPtr]$l).ToInt64()}
function Command([int]$id){Msg $settings 273 $id|Out-Null}
function Control([int]$id){[LayoutProbe]::GetDlgItem($settings,$id)}
function Rect($h){$r=[LayoutProbe+Rect]::new();[LayoutProbe]::GetWindowRect($h,[ref]$r)|Out-Null;return $r}
function Client($h){$r=[LayoutProbe+Rect]::new();[LayoutProbe]::GetClientRect($h,[ref]$r)|Out-Null;return $r}
function Point([int]$x,[int]$y){$x -bor ($y -shl 16)}
function State {Get-Content $config -Raw|ConvertFrom-Json}
function Capture([string]$name){[LayoutProbe]::RedrawWindow($settings,[IntPtr]::Zero,[IntPtr]::Zero,385)|Out-Null;Start-Sleep -Milliseconds 100;$r=Rect $settings;$b=[Drawing.Bitmap]::new($r.right-$r.left,$r.bottom-$r.top);$g=[Drawing.Graphics]::FromImage($b);$g.CopyFromScreen($r.left,$r.top,0,0,$b.Size);$b.Save((Join-Path $output $name));$g.Dispose();$b.Dispose()}
try{
 $main=[IntPtr]::Zero;for($i=0;$i -lt 60 -and $main -eq [IntPtr]::Zero;$i++){Start-Sleep -Milliseconds 100;$main=[LayoutProbe]::Find($process.Id,'原生版')};Check ($main -ne [IntPtr]::Zero) '程序启动'
 Msg $main 273 10|Out-Null;Start-Sleep -Milliseconds 400;$settings=[LayoutProbe]::Find($process.Id,'照片与设置');Check ($settings -ne [IntPtr]::Zero) '设置打开';[LayoutProbe]::SetWindowPos($settings,[IntPtr](-1),20,20,980,890,64)|Out-Null
 $dpi=[LayoutProbe]::GetDpiForWindow($settings)/96;Check (![LayoutProbe]::IsWindowVisible((Control 103)) -and ![LayoutProbe]::IsWindowVisible((Control 101))) '管理区默认收起'
 $grid=Control 102;$view=Client $grid;Check ($view.bottom -ge 480) '用户截图尺寸下照片区至少480物理像素';$gridHeight=$view.bottom;Check ([LayoutProbe]::GetScrollPos($settings,1) -eq 0) '照片页没有外层滚动'
 Capture 'photos-grid.png';Command 431;Capture 'photos-list.png';Check ([int]($view.bottom/(64*$dpi)) -ge 5) '列表同时显示至少五张照片'
 Command 460;Check ([LayoutProbe]::IsWindowVisible((Control 103))) '列表管理可展开';Command 460
 Command 461;Check ([LayoutProbe]::IsWindowVisible((Control 101))) '来源管理可展开';Command 461;Check ((Client $grid).bottom -eq $gridHeight) '收起来源恢复照片区域'
 $selected=(State).playlists[0].photoOrder[7];Msg $main 32829 7|Out-Null;for($i=0;$i -lt 8;$i++){Command 28};$index=@((State).playlists[0].photoOrder).IndexOf($selected);$scroll=[LayoutProbe]::GetScrollPos($grid,1);Check ($index -eq 15) '连续后移保持选择';Check ($index*64*$dpi-$scroll -ge 0 -and ($index+1)*64*$dpi-$scroll -le (Client $grid).bottom) '排序后选中照片保持可见'
 Msg $grid 260 38 536870912|Out-Null;Start-Sleep -Milliseconds 100;Check (@((State).playlists[0].photoOrder).IndexOf($selected) -eq 14) 'Alt上方向键前移';Msg $grid 260 40 536870912|Out-Null;Start-Sleep -Milliseconds 100;Check (@((State).playlists[0].photoOrder).IndexOf($selected) -eq 15) 'Alt下方向键后移'
 Msg $grid 256 36|Out-Null;$before=[LayoutProbe]::GetScrollPos($grid,1);$bottom=(Client $grid).bottom-5;Msg $grid 513 1 (Point 25 25)|Out-Null;Msg $grid 512 1 (Point 25 $bottom)|Out-Null;Start-Sleep -Milliseconds 550;$after=[LayoutProbe]::GetScrollPos($grid,1);Check ($after -ge $before+100) '拖到边缘停住后仍持续滚动';Msg $grid 514 0 (Point 25 $bottom)|Out-Null;Start-Sleep -Milliseconds 100;Check ([LayoutProbe]::GetScrollPos($grid,1) -gt 0) '拖动排序完成并保持目标可见'
 Msg $grid 256 35|Out-Null;$scroll=[LayoutProbe]::GetScrollPos($grid,1);$before=@((State).playlists[0].photoOrder);$penultimate=[int](30*64*$dpi-$scroll+20);Msg $grid 513 1 (Point 25 $penultimate)|Out-Null;Msg $grid 512 1 (Point 25 ((Client $grid).bottom-5))|Out-Null;Msg $grid 514 0 (Point 25 ((Client $grid).bottom-5))|Out-Null;Start-Sleep -Milliseconds 100;Check ((State).playlists[0].photoOrder[-1] -eq $before[30]) '照片可拖到最后一张之后'
 Command 401;$preview=Control 201;$initial=Rect $preview;for($i=0;$i -lt 5;$i++){Msg $settings 277 3|Out-Null};$last=Rect $preview;Check ($last.top -eq $initial.top -and $last.bottom -eq $initial.bottom -and [LayoutProbe]::IsWindowVisible($preview)) '滚动参数时预览固定';Check ([LayoutProbe]::IsWindowVisible((Control 453))) '底部参数能够访问';Capture 'appearance-bottom.png'
 Msg (Control 441) 1029 1 24|Out-Null;Msg $settings 276 8 (Control 441).ToInt64()|Out-Null;Check ((State).playlists[0].config.radius -eq 24) '滚动后调节滑块正常保存';Check ((Rect $preview).top -eq $initial.top) '参数变化不移动预览';Capture 'appearance-adjusted.png'
 foreach($scale in 100,125,150){Msg $main 32831 $scale|Out-Null;[LayoutProbe]::SetWindowPos($settings,[IntPtr](-1),20,20,980,890,64)|Out-Null;Command 401;$r=Rect $preview;for($i=0;$i -lt 4;$i++){Msg $settings 277 3|Out-Null};Check ((Rect $preview).top -eq $r.top) "预览固定：布局比例$scale%";Capture "appearance-$scale.png";Command 400;Check ((Client $grid).bottom -gt 300) "照片区可用：布局比例$scale%"}
 Msg $main 32831 125|Out-Null;[LayoutProbe]::SetWindowPos($settings,[IntPtr](-1),20,20,800,650,64)|Out-Null;Command 401;$r=Rect $preview;Msg $settings 277 3|Out-Null;Check ((Rect $preview).top -eq $r.top) '最小窄窗口采用固定顶部预览';Capture 'appearance-narrow.png'
 Msg $main 273 17|Out-Null;Check ($process.WaitForExit(5000) -and $process.ExitCode -eq 0) '正常退出'
 @{passed=$checks.Count;checks=$checks;photoViewportPixels=$gridHeight;executable=$Executable;limitations=@('100/125/150为布局比例模拟，未改变系统DPI')}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $output 'summary.json');Write-Output "$($checks.Count) 项布局与排序检查通过；$output"
}catch{@{passed=$checks;error=$_.Exception.Message}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $output 'failure.json');throw}
finally{if(!$process.HasExited -and $main -ne [IntPtr]::Zero){[LayoutProbe]::PostMessage($main,273,[IntPtr]17,[IntPtr]::Zero)|Out-Null}}

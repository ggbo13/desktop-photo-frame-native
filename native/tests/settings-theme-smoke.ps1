param([string]$Executable='native/out/2.0.0-preview.20-delivery/DesktopPhotoFrameNative.exe',[string]$SamplePhotosDirectory='',[switch]$LowLoad=$true)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
. (Join-Path $PSScriptRoot 'frame-tray-history.ps1')
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class Settings15Probe{
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int left,top,right,bottom;}
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]public static extern bool EnumWindows(EnumProc f,IntPtr p);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,StringBuilder text,int n);
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")]public static extern bool EnumChildWindows(IntPtr h,EnumProc f,IntPtr p);
 [DllImport("user32.dll")]public static extern int GetDlgCtrlID(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint flags,uint ms,out IntPtr value);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int width,int height,uint flags);
 [DllImport("user32.dll")]public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 [DllImport("user32.dll")]public static extern bool RedrawWindow(IntPtr h,IntPtr r,IntPtr region,uint flags);
 [DllImport("user32.dll")]public static extern uint GetGuiResources(IntPtr process,uint flag);
 public static IntPtr Find(int pid,string title){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);var t=new StringBuilder(512);GetWindowText(h,t,512);if(id==pid&&t.ToString().Contains(title))found=h;return true;},IntPtr.Zero);return found;}
 public static IntPtr Child(IntPtr parent,int id){IntPtr found=GetDlgItem(parent,id);if(found!=IntPtr.Zero)return found;EnumChildWindows(parent,(h,p)=>{if(GetDlgCtrlID(h)==id){found=h;return false;}return true;},IntPtr.Zero);return found;}
}
'@
Add-Type -AssemblyName System.Drawing
[Settings15Probe]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
$root=Split-Path (Split-Path $PSScriptRoot);$output=Join-Path $root ('native/artifacts/settings-theme-'+(Get-Date -Format 'yyyyMMdd-HHmmss'));$data=Join-Path $output 'data';$photos=Join-Path $output 'photos'
New-Item -ItemType Directory -Path $data,$photos -Force|Out-Null
if($SamplePhotosDirectory){Get-ChildItem -LiteralPath $SamplePhotosDirectory -File |Where-Object Extension -Match '^\.(jpg|png|webp)$'|ForEach-Object{Copy-Item -LiteralPath $_.FullName -Destination $photos}}
else{$photoCount=if($LowLoad){4}else{24};for($i=0;$i -lt $photoCount;$i++){$b=[Drawing.Bitmap]::new(720,480);$g=[Drawing.Graphics]::FromImage($b);$g.Clear([Drawing.Color]::FromArgb(35+$i*6,130,160));$b.Save((Join-Path $photos ('photo-{0:00}.bmp' -f $i)));$g.Dispose();$b.Dispose()}}
$config=Join-Path $data 'settings.json'
@{unknownFixture='preserve-theme';global=@{paused=$true};playlists=@(@{id='theme';name='山野之间';sources=@(@{type='folder';path=$photos});config=@{frame='minimal';ratio='auto';transition='none'}});settingsBounds=@{x=30;y=30;width=1280;height=800}}|ConvertTo-Json -Depth 7|Set-Content $config -Encoding utf8
$before=@(Get-FrameTestTrayHistory);$checks=[Collections.Generic.List[string]]::new();$main=[IntPtr]::Zero
$process=Start-FrameTest $Executable -WindowStyle Hidden -PassThru -ArgumentList @('--data-dir',('"'+$data+'"'),'--test-hooks','--test-seconds','180')
function Check($v,$name){if(!$v){throw "失败：$name"};$checks.Add($name)}
function Msg($h,[int]$m,[long]$w=0,[long]$l=0){$value=[IntPtr]::Zero;if([Settings15Probe]::SendMessageTimeout($h,$m,[IntPtr]$w,[IntPtr]$l,2,2500,[ref]$value)-eq [IntPtr]::Zero){throw '窗口消息超时'};return $value.ToInt64()}
function Cmd($id){Msg $settings 273 $id|Out-Null}
function C($id){[Settings15Probe]::Child($settings,$id)}
function Get-ControlRect($h){$r=[Settings15Probe+Rect]::new();[Settings15Probe]::GetWindowRect($h,[ref]$r)|Out-Null;return $r}
function State{Get-Content $config -Raw|ConvertFrom-Json}
function Capture($name){if($LowLoad){return};[Settings15Probe]::RedrawWindow($settings,[IntPtr]::Zero,[IntPtr]::Zero,385)|Out-Null;Start-Sleep -Milliseconds 150;$r=Get-ControlRect $settings;$b=[Drawing.Bitmap]::new($r.right-$r.left,$r.bottom-$r.top);$g=[Drawing.Graphics]::FromImage($b);$dc=$g.GetHdc();try{Check ([Settings15Probe]::PrintWindow($settings,$dc,2)) '设置窗口可渲染'}finally{$g.ReleaseHdc($dc)};$b.Save((Join-Path $output $name));$g.Dispose();$b.Dispose()}
try{
 for($i=0;$i -lt 70 -and $main -eq [IntPtr]::Zero;$i++){Start-Sleep -Milliseconds 80;$main=[Settings15Probe]::Find($process.Id,'原生版')};Check ($main-ne[IntPtr]::Zero) '程序启动';Msg $main 273 10|Out-Null;Start-Sleep -Milliseconds 400;$settings=[Settings15Probe]::Find($process.Id,'照片与设置');Check ($settings-ne[IntPtr]::Zero) '设置打开'
 Check ((C 462)-ne[IntPtr]::Zero) '浅色/夜间切换按钮存在'
 Check ((State).global.theme-eq'light') '旧配置默认浅色';Check ((State).unknownFixture-eq'preserve-theme') '保留未知配置'
 Msg $main 32831 100|Out-Null;[Settings15Probe]::SetWindowPos($settings,[IntPtr]::Zero,30,30,1280,800,64)|Out-Null;Cmd 400
 $grid=Get-ControlRect (C 102);$preview=Get-ControlRect (C 201);Check ($preview.left -ge $grid.right) '照片页右侧固定预览';Check ((Get-ControlRect (C 401)).top -ge (Get-ControlRect (C 400)).bottom) '侧栏纵向导航';Check (($grid.bottom-$grid.top)-ge 430) '照片区域保留足够高度'
 $current=Msg $main 32830 0;Msg $main 32829 3|Out-Null;$caption=[Text.StringBuilder]::new(512);[Settings15Probe]::GetWindowText((C 702),$caption,512)|Out-Null;Check ($caption.ToString()-eq[IO.Path]::GetFileName((State).playlists[0].photoOrder[3])) '右侧预览跟随选中照片';Check ((Msg $main 32830 0)-eq$current) '选中预览不改变实际播放'
 Start-Sleep -Milliseconds 1300;Capture 'photos-light.png';$commits=Msg $main 32830 15;Cmd 462;Check ((State).global.theme-eq'dark') '夜间模式即时保存';Check ((Msg $main 32830 15)-eq$commits) '主题切换不重建桌面相框';Capture 'photos-dark.png'
 Start-Sleep -Milliseconds 1800;$loads=Msg $main 32830 24;Start-Sleep -Milliseconds 1200;Check ((Msg $main 32830 24)-eq$loads) '静止照片库不反复解码';Check ((Msg $main 32830 9)-le8388608) '预览与缩略图合计不超过8MiB'
 $gdi=[Settings15Probe]::GetGuiResources($process.Handle,0);$themeIterations=if($LowLoad){4}else{20};for($n=0;$n-lt$themeIterations;$n++){Cmd 462};Start-Sleep -Milliseconds 150;Check ([Settings15Probe]::GetGuiResources($process.Handle,0)-le$gdi+6) '反复切换主题不累积GDI资源'
 Cmd 401;Msg $settings 277 3|Out-Null;$pinned=Get-ControlRect (C 201);Msg $settings 277 3|Out-Null;Check ((Get-ControlRect (C 201)).top-eq$pinned.top) '滚动参数时预览固定';Capture 'appearance-dark.png'
 Cmd 403;Check ((Msg (C 463) 327)-eq1) '应用页同步夜间模式';Msg (C 112) 245|Out-Null;Check ((State).global.alwaysOnTop) '自绘复选框保留原生点击行为';Msg (C 112) 245|Out-Null;Msg (C 463) 334 0|Out-Null;Msg $settings 273 (463-bor(1-shl16))|Out-Null;Check ((State).global.theme-eq'light') '应用页下拉框可切换浅色';Cmd 462
 Msg $settings 16|Out-Null;Start-Sleep -Milliseconds 120;Msg $main 273 10|Out-Null;Start-Sleep -Milliseconds 200;$settings=[Settings15Probe]::Find($process.Id,'照片与设置');Cmd 403;Check ((Msg (C 463) 327)-eq1) '重新打开设置保留夜间模式'
 foreach($scale in 100,125,150){Msg $main 32831 $scale|Out-Null;[Settings15Probe]::SetWindowPos($settings,[IntPtr]::Zero,20,20,([int](1040*$scale/100)),([int](720*$scale/100)),64)|Out-Null;Cmd 400;Check ([Settings15Probe]::IsWindowVisible((C 201))) "照片页预览：$scale%布局模拟";Cmd 401;Check ([Settings15Probe]::IsWindowVisible((C 201))) "外观页预览：$scale%布局模拟"}
 Msg $main 32831 100|Out-Null;[Settings15Probe]::SetWindowPos($settings,[IntPtr]::Zero,20,20,700,600,64)|Out-Null;Cmd 400;Check ([Settings15Probe]::IsWindowVisible((C 432))) '窄窗口使用下拉列表';Check ((Get-ControlRect (C 11)).bottom-le(Get-ControlRect (C 115)).top-and(Get-ControlRect (C 115)).bottom-le(Get-ControlRect (C 102)).top) '窄窗口操作、搜索、照片不重叠';Cmd 401;Capture 'narrow-dark.png'
 Msg $main 273 17|Out-Null;Check ($process.WaitForExit(5000)-and$process.ExitCode-eq0) '正常退出';$process=Start-FrameTest $Executable -WindowStyle Hidden -PassThru -ArgumentList @('--data-dir',('"'+$data+'"'),'--test-hooks','--test-seconds','40');$main=[IntPtr]::Zero;for($i=0;$i-lt70-and$main-eq[IntPtr]::Zero;$i++){Start-Sleep -Milliseconds 80;$main=[Settings15Probe]::Find($process.Id,'原生版')};Msg $main 273 10|Out-Null;Start-Sleep -Milliseconds 200;$settings=[Settings15Probe]::Find($process.Id,'照片与设置');Cmd 403;Check ((Msg (C 463) 327)-eq1) '应用重启恢复夜间模式'
 $after=@(Get-FrameTestTrayHistory);Check (@($after|Where-Object{$_.path-ieq$process.Path-and$_.key-notin@($before.key)}).Count-eq0) '没有新增托盘历史'
 @{passed=$true;checks=$checks;realSystemDpi=$false;executable=$Executable}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $output 'summary.json') -Encoding utf8;Write-Output "设置与主题通过：$($checks.Count) 项；$output"
}catch{@{passed=$false;error=$_.Exception.Message;checks=$checks}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $output 'failure.json') -Encoding utf8;throw}
finally{if(!$process.HasExited){if($main-ne[IntPtr]::Zero){[Settings15Probe]::PostMessage($main,273,[IntPtr]17,[IntPtr]::Zero)|Out-Null};if(!$process.WaitForExit(5000)){Stop-Process -Id $process.Id}}}

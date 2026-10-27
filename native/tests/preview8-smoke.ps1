param([string]$Executable='native/out/2.0.0-preview.17-delivery/DesktopPhotoFrameNative.exe',[int]$ListRowHeight=64)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class V8Probe {
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int left,top,right,bottom;}
 [DllImport("user32.dll")]public static extern bool EnumWindows(EnumProc f,IntPtr p);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,StringBuilder t,int n);
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")]public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",EntryPoint="SendMessageW",CharSet=CharSet.Unicode)]public static extern IntPtr SendText(IntPtr h,uint m,IntPtr w,string text);
 [DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")]public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern int GetSystemMetrics(int n);
 [DllImport("user32.dll",EntryPoint="GetWindowLongPtrW")]public static extern IntPtr GetWindowLongPtr(IntPtr h,int n);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int width,int height,uint flags);
 [DllImport("user32.dll")]public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 [DllImport("user32.dll")]public static extern bool RedrawWindow(IntPtr h,IntPtr rect,IntPtr region,uint flags);
 public static IntPtr Find(int pid,string title){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);var t=new StringBuilder(512);GetWindowText(h,t,512);if(id==pid&&t.ToString().Contains(title))found=h;return true;},IntPtr.Zero);return found;}
 public static string Windows(int pid){var result=new StringBuilder();EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);if(id==pid){var t=new StringBuilder(512);GetWindowText(h,t,512);result.AppendLine(h+" visible="+IsWindowVisible(h)+" "+t);}return true;},IntPtr.Zero);return result.ToString();}
}
'@
Add-Type -AssemblyName System.Drawing
[V8Probe]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
$root=Split-Path (Split-Path $PSScriptRoot)
$output=Join-Path $root ('native/artifacts/preview8-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$data=Join-Path $output 'data';$photos=Join-Path $output 'photos'
New-Item -ItemType Directory -Path $data,$photos -Force | Out-Null
for($i=0;$i -lt 6;$i++){$w=if($i%2){600}else{960};$h=if($i%2){960}else{600};$b=[Drawing.Bitmap]::new($w,$h);$g=[Drawing.Graphics]::FromImage($b);$g.Clear([Drawing.Color]::FromArgb(40+$i*30,150-$i*12,170+$i*10));$g.DrawRectangle([Drawing.Pens]::White,4,4,$w-9,$h-9);$b.Save((Join-Path $photos "$i.bmp"));$g.Dispose();$b.Dispose()}
$config=Join-Path $data 'settings.json'
@{unknownFixture='preserve-v8';activePlaylistId='first';playlists=@(@{id='first';name='测试照片';sources=@(@{type='folder';path=$photos});config=@{ratio='auto';anchor='bottom-right';transition='fade';interval=3}},@{id='second';name='独立列表';sources=@(@{type='folder';path=$photos});config=@{interval=17}});global=@{paused=$true};bounds=@{x=120;y=120;width=480;height=360}} | ConvertTo-Json -Depth 7 | Set-Content $config -Encoding utf8
$process=Start-FrameTest $Executable -WindowStyle Hidden -PassThru -ArgumentList @('--data-dir',('"'+$data+'"'),'--test-hooks','--test-seconds','120')
$checks=[Collections.Generic.List[string]]::new()
function Check($condition,[string]$name){if(!$condition){throw "失败：$name"};$checks.Add($name)}
function Message($h,[int]$m,[long]$w=0,[long]$l=0){[V8Probe]::SendMessage($h,$m,[IntPtr]$w,[IntPtr]$l).ToInt64()}
function Command($h,[int]$id){Message $h 273 $id | Out-Null}
function Read-State {Get-Content $config -Raw | ConvertFrom-Json}
function Text($h,[int]$id,[string]$text){[V8Probe]::SendText([V8Probe]::GetDlgItem($h,$id),12,[IntPtr]::Zero,$text)|Out-Null}
function Combo($h,[int]$id,[int]$index){Message ([V8Probe]::GetDlgItem($h,$id)) 334 $index|Out-Null}
function Flag($h,[int]$id,[bool]$value){Message ([V8Probe]::GetDlgItem($h,$id)) 241 ([int]$value)|Out-Null}
function Query([int]$key){Message $main 32830 $key}
function Wait-Idle {for($i=0;$i -lt 150;$i++){if(((Query 7) -band 48) -eq 0){Start-Sleep -Milliseconds 50;return};Start-Sleep -Milliseconds 20};throw '等待切换超时'}
function Capture($h,[string]$name){$r=[V8Probe+Rect]::new();[V8Probe]::GetWindowRect($h,[ref]$r)|Out-Null;$image=[Drawing.Bitmap]::new($r.right-$r.left,$r.bottom-$r.top);$g=[Drawing.Graphics]::FromImage($image);$dc=$g.GetHdc();try{[V8Probe]::PrintWindow($h,$dc,2)|Out-Null}finally{$g.ReleaseHdc($dc)};$image.Save((Join-Path $output $name));$g.Dispose();$image.Dispose()}
try{
 $main=[IntPtr]::Zero;for($i=0;$i -lt 50 -and $main -eq [IntPtr]::Zero;$i++){Start-Sleep -Milliseconds 100;$main=[V8Probe]::Find($process.Id,'原生版')};Check ($main -ne [IntPtr]::Zero) '应用启动';Start-Sleep -Milliseconds 350
 Command $main 16;$duplicate=Start-FrameTest $Executable -WindowStyle Hidden -PassThru -ArgumentList @('--data-dir',('"'+$data+'"'),'--test-seconds','3');Check ($duplicate.WaitForExit(5000) -and $duplicate.ExitCode -eq 0) '重复启动及时退出';Start-Sleep -Milliseconds 80;Check (((Query 7)-band 1) -ne 0) '重复启动恢复已有相框'
 Command $main 10;Start-Sleep -Milliseconds 300;$settings=[V8Probe]::Find($process.Id,'照片与设置');if(![V8Probe]::IsWindowVisible($settings)){$process.Refresh();[pscustomobject]@{HasExited=$process.HasExited;ExitCode=$(if($process.HasExited){$process.ExitCode});Windows=[V8Probe]::Windows($process.Id)}|ConvertTo-Json|Set-Content (Join-Path $output 'window-open-diagnostic.json')};Check ([V8Probe]::IsWindowVisible($settings)) '四页设置打开'
 $state=Read-State;Check ($state.global.photoView -eq 'grid' -and !$state.playlists[0].config.limitAutoSize -and $state.playlists[0].config.maxWidth -eq 640) '旧配置补齐默认值'
 foreach($page in 400..403){Command $settings $page;Capture $settings "page-$page.png"};Check ($true) '四个设置页面可以切换'
 [V8Probe]::SetWindowPos($settings,[IntPtr]::Zero,0,0,700,600,6)|Out-Null;Command $settings 400;Check ([V8Probe]::IsWindowVisible([V8Probe]::GetDlgItem($settings,432)) -and ![V8Probe]::IsWindowVisible([V8Probe]::GetDlgItem($settings,100))) '窄窗口使用列表下拉框';Capture $settings 'narrow.png'
 [V8Probe]::SetWindowPos($settings,[IntPtr]::Zero,0,0,1120,800,6)|Out-Null;Check ([V8Probe]::IsWindowVisible([V8Probe]::GetDlgItem($settings,100))) '宽窗口恢复播放列表侧栏'
 $realScale=[V8Probe]::GetDpiForWindow($settings)*100/96
 foreach($scale in 100,125,150){Message $main 32831 $scale|Out-Null;[V8Probe]::SetWindowPos($settings,[IntPtr]::Zero,0,0,([int](1040*$scale/100)),([int](720*$scale/100)),6)|Out-Null;Command $settings 401;Capture $settings "layout-scale-$scale.png";Check ([V8Probe]::IsWindowVisible([V8Probe]::GetDlgItem($settings,100))) "布局比例模拟：$scale%";Message $settings 277 3|Out-Null;Capture $settings "appearance-lower-$scale.png";[V8Probe]::SetWindowPos($settings,[IntPtr](-1),0,0,0,0,3)|Out-Null;[V8Probe]::RedrawWindow($settings,[IntPtr]::Zero,[IntPtr]::Zero,385)|Out-Null;Start-Sleep -Milliseconds 100;$r=[V8Probe+Rect]::new();[V8Probe]::GetWindowRect($settings,[ref]$r)|Out-Null;$bitmap=[Drawing.Bitmap]::new($r.right-$r.left,$r.bottom-$r.top);$g=[Drawing.Graphics]::FromImage($bitmap);$g.CopyFromScreen($r.left,$r.top,0,0,$bitmap.Size);$bitmap.Save((Join-Path $output "appearance-screen-$scale.png"));$g.Dispose();$bitmap.Dispose()}
 Message $main 32831 $realScale|Out-Null;[V8Probe]::SetWindowPos($settings,[IntPtr]::Zero,0,0,1120,800,6)|Out-Null;Command $settings 400
 Command $settings 431;$grid=[V8Probe]::GetDlgItem($settings,102);$row=[int]($ListRowHeight*$realScale/100);$p0=(20 -bor (20 -shl 16));$p1=(20 -bor (($row+20) -shl 16));$p2=(20 -bor (($row*2+20) -shl 16));$original=@((Read-State).playlists[0].photoOrder)
 Message $grid 513 1 $p1|Out-Null;Message $grid 512 1 $p0|Out-Null;Message $grid 514 0 $p0|Out-Null;Start-Sleep -Milliseconds 80;Check ((Read-State).playlists[0].photoOrder[0] -eq $original[1]) '鼠标拖动排序'
 Message $grid 513 1 $p0|Out-Null;Message $grid 512 1 $p2|Out-Null;Message $grid 514 0 $p2|Out-Null;Start-Sleep -Milliseconds 80;Check (((Read-State).playlists[0].photoOrder -join '|') -eq ($original -join '|')) '拖动后顺序持久保存'
 Message $grid 515 0 $p1|Out-Null;Start-Sleep -Milliseconds 80;Wait-Idle;Check ((Query 0) -eq 1) '缩略图双击播放'
 Message $main 32829 2|Out-Null;[V8Probe]::PostMessage($grid,256,[IntPtr]13,[IntPtr]::Zero)|Out-Null;Start-Sleep -Milliseconds 80;Wait-Idle;Check ((Query 0) -eq 2 -and ((Query 7)-band 2)) 'Enter播放并保留暂停';Command $settings 430
 $before=Query 0;Message $main 32829 1 0|Out-Null;Check ((Query 0) -eq $before -and (Query 8) -eq 1) '单击选择不会切图';Message $main 32829 3 1|Out-Null;Check ((Query 8) -eq 2) 'Ctrl多选';Message $main 32829 5 2|Out-Null;Check ((Query 8) -eq 3) 'Shift区间多选'
 $selected=@((Read-State).playlists[0].photoOrder[3..5]);Command $settings 27;$order=(Read-State).playlists[0].photoOrder;Check ($order[2] -eq $selected[0] -and $order[4] -eq $selected[2]) '批量前移保留相对顺序';Command $settings 28
 Command $settings 431;Check ((Read-State).global.photoView -eq 'list') '列表视图保存';Command $settings 430;Check ((Read-State).global.photoView -eq 'grid') '网格视图保存'
 Text $settings 115 '4.bmp';Check ((Query 8) -eq 1) '筛选后按路径保留选择';Text $settings 115 ''
 Message $main 32829 1 0|Out-Null;Message $main 32829 2 1|Out-Null;[V8Probe]::PostMessage($settings,273,[IntPtr]26,[IntPtr]::Zero)|Out-Null
 $dialog=[IntPtr]::Zero;for($i=0;$i -lt 30 -and $dialog -eq [IntPtr]::Zero;$i++){Start-Sleep -Milliseconds 50;$dialog=[V8Probe]::Find($process.Id,'排除轮播')};Check ($dialog -ne [IntPtr]::Zero) '批量排除仅弹一次确认';Command $dialog 6;Start-Sleep -Milliseconds 120;Check ((Read-State).playlists[0].excluded.Count -eq 2) '批量排除保存'
 Flag $settings 116 $true;Command $settings 116;Message $main 32829 0 0|Out-Null;Message $main 32829 1 1|Out-Null;Command $settings 26;Check ((Read-State).playlists[0].excluded.Count -eq 0) '批量恢复';Flag $settings 116 $false;Command $settings 116
 Command $settings 401;Combo $settings 105 1;Flag $settings 450 $true;Text $settings 451 '420';Text $settings 452 '380';Command $settings 24;Check ((Read-State).playlists[0].config.maxWidth -eq 420 -and (Read-State).playlists[1].config.maxWidth -eq 640) '尺寸上限按列表隔离'
 $area=Query 10;for($i=0;$i -lt 8;$i++){Command $main 15;Wait-Idle;Check ((Query 1) -le 420 -and (Query 2) -le 380) "自适应限制：$i"};Check ((Query 10) -eq $area) '受限切图不反写目标面积'
 for($i=0;$i -lt 5;$i++){Command $settings (410+$i);Check ((Read-State).playlists[0].config.frame -eq @('minimal','wood','metal','polaroid','borderless')[$i]) "相框卡片选择：$i"};Command $settings 410
 for($i=0;$i -lt 4;$i++){Command $settings (420+$i);Check ((Read-State).playlists[0].config.anchor -eq @('top-left','top-right','bottom-left','bottom-right')[$i]) "四角按钮：$i"}
 Message ([V8Probe]::GetDlgItem($settings,440)) 1029 1 44|Out-Null;Message $settings 276 8 ([V8Probe]::GetDlgItem($settings,440).ToInt64())|Out-Null;Check ((Read-State).playlists[0].config.thickness -eq 44) '滑块松开后保存';Text $settings 110 '18';Command $settings 24
 Combo $settings 107 1;Command $settings 24;$base=Query 0;for($i=0;$i -lt 5;$i++){Command $main 15};Wait-Idle;Check ((Query 0) -eq (($base+5)%6)) '快速前进累计到最终目标'
 $history=Query 5;Command $main 14;Wait-Idle;Check ((Query 6) -eq -1 -and (Query 11) -lt ($history-1)) '返回使用反向滑动及实际历史'
 Command $main 15;Wait-Idle;$base=Query 0;for($i=0;$i -lt 5;$i++){Command $main 15};Command $main 14;Command $main 14;Wait-Idle;Check ((Query 0) -eq (($base+3)%6)) '快速方向反转以最新请求目标累计'
 Combo $settings 107 3;Combo $settings 108 1;Command $settings 24;$round=[Collections.Generic.HashSet[long]]::new();$round.Add((Query 0))|Out-Null;for($i=0;$i -lt 5;$i++){Command $main 15;Wait-Idle;$round.Add((Query 0))|Out-Null};Check ($round.Count -eq 6) '随机一轮内不重复';$last=Query 0;Command $main 14;Wait-Idle;Command $main 15;Wait-Idle;Check ((Query 0) -eq $last) '随机播放可按实际历史往返';Combo $settings 108 0;Command $settings 24
 Message $main 32828 4|Out-Null;Check (((Query 7)-band 5) -eq 4 -and (Read-State).global.paused) '全屏暂停不覆盖手动暂停';$index=Query 0;Start-Sleep -Milliseconds 300;Check ((Query 0) -eq $index) '自动隐藏期间不切图'
 Message $main 32828 5|Out-Null;Message $main 32828 1|Out-Null;Check (((Query 7)-band 1) -eq 0) '全屏解除但仍锁屏时保持隐藏';Message $main 32828 0|Out-Null;Check (((Query 7)-band 3) -eq 3) '解锁后仍保留手动暂停'
 Command $main 16;Message $main 32828 2|Out-Null;Message $main 32828 0|Out-Null;Check (((Query 7)-band 1) -eq 0) '休眠恢复不覆盖手动隐藏';Command $main 16
 Combo $settings 107 1;Command $settings 24;Message $main 32828 8|Out-Null;Check (((Query 7)-band 8) -ne 0) '电池状态关闭动画';Command $main 15;Wait-Idle;Check (((Query 7)-band 16) -eq 0) '电池切图直接完成';Flag $settings 455 $false;Command $settings 455;Check (((Query 7)-band 8) -eq 0) '电池动画节能可关闭';Flag $settings 455 $true;Command $settings 455;Message $main 32828 0|Out-Null;Check (((Query 7)-band 8) -eq 0) '插电恢复动画设置'
 Message $main 689 7|Out-Null;Check (((Query 7)-band 1) -eq 0) 'Windows锁屏消息处理';Message $main 689 8|Out-Null;Check (((Query 7)-band 1) -ne 0) 'Windows解锁消息处理';Message $main 536 4|Out-Null;Check (((Query 7)-band 1) -eq 0) 'Windows休眠消息处理';Message $main 536 18|Out-Null;Check (((Query 7)-band 1) -ne 0) 'Windows唤醒消息处理'
 Add-Type -AssemblyName System.Windows.Forms;$fullscreen=[Windows.Forms.Form]::new()
 try{$fullscreen.FormBorderStyle='None';$fullscreen.StartPosition='Manual';$fullscreen.Bounds=[Windows.Forms.Screen]::PrimaryScreen.Bounds;$fullscreen.TopMost=$true;$fullscreen.Show();[V8Probe]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null;[V8Probe]::SetWindowPos($fullscreen.Handle,[IntPtr](-1),0,0,[V8Probe]::GetSystemMetrics(0),[V8Probe]::GetSystemMetrics(1),64)|Out-Null;$focused=[V8Probe]::SetForegroundWindow($fullscreen.Handle);[Windows.Forms.Application]::DoEvents();Start-Sleep -Milliseconds 150;$actualRect=[V8Probe+Rect]::new();[V8Probe]::GetWindowRect($fullscreen.Handle,[ref]$actualRect)|Out-Null;@{requested=$fullscreen.Handle.ToInt64();foreground=[V8Probe]::GetForegroundWindow().ToInt64();focusGranted=$focused;bounds=$actualRect;style=[V8Probe]::GetWindowLongPtr($fullscreen.Handle,-16).ToInt64()}|ConvertTo-Json|Set-Content (Join-Path $output 'fullscreen-probe.json');Check ((Message $main 32833 0 $fullscreen.Handle.ToInt64()) -eq 1) '同屏无边框窗口几何识别'
 $fullscreen.FormBorderStyle='Sizable';$fullscreen.WindowState='Maximized';[Windows.Forms.Application]::DoEvents();Check ((Message $main 32833 0 $fullscreen.Handle.ToInt64()) -eq 0) '普通最大化窗口不触发全屏'
 if([Windows.Forms.Screen]::AllScreens.Count -gt 1){$fullscreen.WindowState='Normal';$fullscreen.FormBorderStyle='None';$fullscreen.Bounds=([Windows.Forms.Screen]::AllScreens|Where-Object {!$_.Primary}|Select-Object -First 1).Bounds;[Windows.Forms.Application]::DoEvents();Check ((Message $main 32833 0 $fullscreen.Handle.ToInt64()) -eq 0) '其他显示器全屏不影响相框'}
 }finally{$fullscreen.Close();$fullscreen.Dispose()}
 Command $main 29;Start-Sleep -Milliseconds 250;Check (((Query 7)-band 4) -ne 0) '大图临时暂停';Message $main 32828 4|Out-Null;Check ([V8Probe]::Find($process.Id,'查看大图') -eq [IntPtr]::Zero) '自动隐藏关闭大图';Message $main 32828 0|Out-Null
 Copy-Item (Join-Path $photos '0.bmp') (Join-Path $photos 'a-new.bmp');[IO.File]::WriteAllText((Join-Path $photos 'z-broken.bmp'),'unreadable image fixture');for($i=0;$i -lt 70 -and (Read-State).playlists[0].photoOrder.Count -lt 8;$i++){Start-Sleep -Milliseconds 100};Check ((Read-State).playlists[0].photoOrder.Count -eq 8 -and (Read-State).playlists[0].photoOrder[-2].EndsWith('a-new.bmp')) '文件夹新增按名称追加到末尾'
 Command $settings 400;Message $main 32829 1|Out-Null;Command $settings 25;Wait-Idle;Message $main 32829 7|Out-Null;Command $settings 25;Wait-Idle;Check ((Query 0) -ne 7) '损坏照片跳过并保留可用照片'
 Check ((Query 9) -le 8388608) '缩略图缓存不超过8MiB';Message $settings 16|Out-Null;Check ((Query 9) -eq 0) '关闭设置释放缩略图';Check ((Read-State).unknownFixture -eq 'preserve-v8') '未知配置字段保留'
 Command $main 17;Check ($process.WaitForExit(5000) -and $process.ExitCode -eq 0) '正常退出'
 @{passed=$checks.Count;checks=$checks;data=$data;executable=$Executable;limitations=@('系统DPI与物理输入需人工验证')}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $output 'summary.json');Write-Output "$($checks.Count) 项 preview.8 检查通过；$output"
}catch{@{passed=$checks;error=$_.Exception.Message}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $output 'failure.json');throw}
finally{if(!$process.HasExited -and $main -ne [IntPtr]::Zero){[V8Probe]::PostMessage($main,273,[IntPtr]17,[IntPtr]::Zero)|Out-Null}}

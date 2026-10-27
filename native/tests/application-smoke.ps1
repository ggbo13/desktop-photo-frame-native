param([string]$Executable='native/out/2.0.0-preview.20-delivery/DesktopPhotoFrameNative.exe',[switch]$SkipPointerInjection)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class NativeAppTest {
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int left,top,right,bottom;}
 [StructLayout(LayoutKind.Sequential)]public struct Monitor{public int size;public Rect screen,work;public uint flags;}
 [DllImport("user32.dll")]public static extern IntPtr MonitorFromRect(ref Rect r,uint flags);
 [DllImport("user32.dll")]public static extern bool GetMonitorInfo(IntPtr h,ref Monitor info);
 [DllImport("user32.dll")]public static extern bool EnumWindows(EnumProc f,IntPtr p);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,StringBuilder t,int n);
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")]public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",EntryPoint="SendMessageW",CharSet=CharSet.Unicode)]public static extern IntPtr SendText(IntPtr h,uint m,IntPtr w,string s);
 [DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")]public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int height,uint flags);
 [StructLayout(LayoutKind.Sequential)]public struct Point{public int x,y;}
 [DllImport("user32.dll")]public static extern bool GetCursorPos(out Point point);
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]public static extern void mouse_event(uint flags,uint dx,uint dy,uint data,UIntPtr extraInfo);
 public static IntPtr Find(int pid,string title){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);var t=new StringBuilder(512);GetWindowText(h,t,512);if(id==pid&&t.ToString().Contains(title))found=h;return true;},IntPtr.Zero);return found;}
}
'@
[NativeAppTest]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
$root=Split-Path (Split-Path $PSScriptRoot)
$output=Join-Path $root ('native/artifacts/app-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$data=Join-Path $output 'data';$photos=Join-Path $output 'photos'
New-Item -ItemType Directory -Path $data,$photos -Force | Out-Null
$fixtures=Join-Path $root 'test-artifacts/v3-1.25-1790082981600/photos'
Copy-Item -LiteralPath (Join-Path $fixtures 'a-landscape.bmp'),(Join-Path $fixtures 'b-portrait.bmp') -Destination $photos
$initial=@{playlists=@(@{id='first';name='风景照片';sources=@(@{type='folder';path=$photos});config=@{interval=3;ratio='auto';anchor='bottom-right'}},@{id='second';name='生活记录';sources=@();config=@{interval=17}});activePlaylistId='first';global=@{paused=$false};unknownFixture='preserve-me'}
$initial.playlists[1].sources=@(@{type='folder';path=$photos});$initial.playlists[1].excluded=@((Join-Path $photos 'b-portrait.bmp'))
$config=Join-Path $data 'settings.json';$initial | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $config -Encoding utf8
$process=Start-FrameTest $Executable -WindowStyle Hidden -PassThru -ArgumentList @('--data-dir',('"'+$data+'"'),'--test-seconds','90')
$checks=[System.Collections.Generic.List[string]]::new()
function Assert-Case($condition,[string]$name){if(!$condition){throw "失败：$name"};$checks.Add($name)}
function Read-State { Get-Content -LiteralPath $config -Raw | ConvertFrom-Json }
function Command($h,[int]$id){[NativeAppTest]::SendMessage($h,273,[IntPtr]$id,[IntPtr]::Zero) | Out-Null}
function Set-Text($h,[int]$id,[string]$text){$control=[NativeAppTest]::GetDlgItem($h,$id);if($control -eq [IntPtr]::Zero){throw "找不到输入框：$id"};if([NativeAppTest]::SendText($control,12,[IntPtr]::Zero,$text) -eq [IntPtr]::Zero){throw "输入框写入失败：$id"}}
function Window-Title($h){$text=[Text.StringBuilder]::new(512);[NativeAppTest]::GetWindowText($h,$text,512)|Out-Null;return $text.ToString()}
function Select-Combo($h,[int]$id,[int]$index){[NativeAppTest]::SendMessage([NativeAppTest]::GetDlgItem($h,$id),334,[IntPtr]$index,[IntPtr]::Zero)|Out-Null}
function Wait-ChangedTitle($h,[string]$before){for($attempt=0;$attempt -lt 100;$attempt++){Start-Sleep -Milliseconds 30;$next=Window-Title $h;if($next -ne $before){return $next}};throw "照片没有及时切换：$before（当前：$next）"}
try {
 $main=[IntPtr]::Zero
 for($n=0;$n -lt 30 -and $main -eq [IntPtr]::Zero;$n++){Start-Sleep -Milliseconds 200;$main=[NativeAppTest]::Find($process.Id,'原生版')}
 Assert-Case ($main -ne [IntPtr]::Zero) '原生相框启动'
 Command $main 10;Start-Sleep -Milliseconds 700
 $settings=[NativeAppTest]::Find($process.Id,'照片与设置')
 Assert-Case ([NativeAppTest]::IsWindowVisible($settings)) '独立设置可见'
 $originalSettings=$settings;Command $main 10
 Assert-Case ([NativeAppTest]::Find($process.Id,'照片与设置') -eq $originalSettings) '重复打开复用设置窗口'
 Add-Type -AssemblyName System.Drawing
 $rect=[NativeAppTest+Rect]::new();[NativeAppTest]::GetWindowRect($settings,[ref]$rect) | Out-Null
 $image=[Drawing.Bitmap]::new($rect.right-$rect.left,$rect.bottom-$rect.top);$graphics=[Drawing.Graphics]::FromImage($image)
 try {$graphics.CopyFromScreen($rect.left,$rect.top,0,0,$image.Size);$image.Save((Join-Path $output 'settings.png'))} catch { $_.Exception.Message | Set-Content (Join-Path $output 'screen-unavailable.txt') } finally {$graphics.Dispose();$image.Dispose()}
 Set-Text $settings 103 '测试独立列表';Command $settings 20
 $state=Read-State;Assert-Case ($state.playlists.Count -eq 3) '新建播放列表'
 Set-Text $settings 103 '已改名';Command $settings 21
 Set-Text $settings 109 '19';[NativeAppTest]::SendMessage([NativeAppTest]::GetDlgItem($settings,105),334,[IntPtr]1,[IntPtr]::Zero) | Out-Null;Command $settings 24
 $state=Read-State;$current=$state.playlists | Where-Object id -eq $state.activePlaylistId
 Assert-Case ($current.name -eq '已改名' -and $current.config.interval -eq 19 -and $current.config.ratio -eq 'auto') '改名和设置持久化'
 Assert-Case (($state.playlists | Where-Object id -eq 'first').config.interval -eq 3 -and ($state.playlists | Where-Object id -eq 'second').config.interval -eq 17) '列表配置隔离'
 $list=[NativeAppTest]::GetDlgItem($settings,100);[NativeAppTest]::SendMessage($list,390,[IntPtr]1,[IntPtr]::Zero) | Out-Null;Command $settings 65636
 [NativeAppTest]::SendMessage([NativeAppTest]::GetDlgItem($settings,116),241,[IntPtr]1,[IntPtr]::Zero) | Out-Null;Command $settings 116
 Assert-Case ([NativeAppTest]::SendMessage([NativeAppTest]::GetDlgItem($settings,102),395,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -eq 1) '已排除视图显示照片'
 [NativeAppTest]::SendMessage([NativeAppTest]::GetDlgItem($settings,102),390,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null;Command $settings 26
 Assert-Case ((Read-State).playlists[1].excluded.Count -eq 0) '恢复排除照片并保存'
 [NativeAppTest]::SendMessage([NativeAppTest]::GetDlgItem($settings,116),241,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null;Command $settings 116
 Assert-Case ([NativeAppTest]::SendMessage([NativeAppTest]::GetDlgItem($settings,102),395,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -eq 2) '恢复照片回到播放目录'
 [NativeAppTest]::SetWindowPos($settings,[IntPtr]::Zero,0,0,700,540,6) | Out-Null
 [NativeAppTest]::SendMessage($settings,276,[IntPtr]7,[IntPtr]::Zero) | Out-Null
 $optionRect=[NativeAppTest+Rect]::new();$windowRect=[NativeAppTest+Rect]::new();[NativeAppTest]::GetWindowRect([NativeAppTest]::GetDlgItem($settings,24),[ref]$optionRect) | Out-Null;[NativeAppTest]::GetWindowRect($settings,[ref]$windowRect) | Out-Null
 Assert-Case ($optionRect.right -le $windowRect.right) '小窗口横向滚动可访问右侧设置'
 $list=[NativeAppTest]::GetDlgItem($settings,100);[NativeAppTest]::SendMessage($list,390,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null;Command $settings 65636
 Assert-Case ((Read-State).activePlaylistId -eq 'first') '切换播放列表'
 Set-Text $settings 115 'portrait'
 Assert-Case ([NativeAppTest]::SendMessage([NativeAppTest]::GetDlgItem($settings,102),395,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -eq 1) '按文件名筛选照片'
 Set-Text $settings 115 '不存在的照片'
 Assert-Case ([NativeAppTest]::SendMessage([NativeAppTest]::GetDlgItem($settings,102),395,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -eq 0) '无匹配搜索显示空列表'
 Set-Text $settings 115 ''
 $photoList=[NativeAppTest]::GetDlgItem($settings,102)
 $orderBefore=(Read-State).playlists[0].photoOrder
 [NativeAppTest]::SendMessage($photoList,513,[IntPtr]1,[IntPtr](38*65536+40)) | Out-Null
 [NativeAppTest]::SendMessage($photoList,512,[IntPtr]1,[IntPtr](114*65536+40)) | Out-Null
 [NativeAppTest]::SendMessage($photoList,514,[IntPtr]::Zero,[IntPtr](114*65536+40)) | Out-Null
 Assert-Case ((Read-State).playlists[0].photoOrder[0] -eq $orderBefore[1]) '缩略图拖动排序持久化'
 Copy-Item -LiteralPath (Join-Path $fixtures 'c-square.bmp') -Destination $photos
 Start-Sleep -Seconds 7
 Assert-Case ((Read-State).playlists[0].photoOrder.Count -eq 3) '目录新增自动更新'
 Command $main 13
 Select-Combo $settings 107 3
 for($style=0;$style -lt 5;$style++){
  Select-Combo $settings 104 $style;Set-Text $settings 110 $(if($style%2){'64'}else{'6'});Set-Text $settings 111 $(if($style%2){'32'}else{'0'});Set-Text $settings 119 $(if($style%2){'100'}else{'0'});Command $settings 24
  $appearance=(Read-State).playlists[0].config
  Assert-Case ($appearance.frame -eq @('minimal','wood','metal','polaroid','borderless')[$style] -and $appearance.radius -eq $(if($style%2){32}else{0})) "相框及外观极值保存：$style"
 }
 Select-Combo $settings 104 0;Set-Text $settings 110 '18';Set-Text $settings 111 '10';Set-Text $settings 119 '30';Command $settings 24
 $ratioValues=@(1,(4.0/3),(3.0/2),(16.0/9),(9.0/16))
 for($ratio=0;$ratio -lt 5;$ratio++){Select-Combo $settings 105 ($ratio+2);Command $settings 24;$b=(Read-State).bounds;Assert-Case ([Math]::Abs(($b.width-36)/($b.height-36)-$ratioValues[$ratio]) -lt .02) "照片内区比例：$($ratioValues[$ratio])"}
 Select-Combo $settings 105 1;Select-Combo $settings 108 1;Command $settings 24
 $sequence=[System.Collections.Generic.List[string]]::new();$sequence.Add((Window-Title $main));for($n=0;$n -lt 5;$n++){$before=$sequence[$sequence.Count-1];Command $main 15;$sequence.Add((Wait-ChangedTitle $main $before))}
 Assert-Case (@($sequence[0..2] | Select-Object -Unique).Count -eq 3 -and @($sequence[3..5] | Select-Object -Unique).Count -eq 3) '随机播放每轮不重复'
 Command $main 14;$previous=Wait-ChangedTitle $main $sequence[5]
 Assert-Case ($previous -eq $sequence[4]) '上一张返回实际随机历史'
 Select-Combo $settings 108 0;Select-Combo $settings 107 0;Command $settings 24
 [NativeAppTest]::SendMessage($settings,16,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 if(!$SkipPointerInjection){
 $wheelBounds=(Read-State).bounds;$wheelPoint=[NativeAppTest+Point]::new();$wheelPoint.x=[int]($wheelBounds.x+$wheelBounds.width/2);$wheelPoint.y=[int]($wheelBounds.y+$wheelBounds.height/2);$savedWheelCursor=[NativeAppTest+Point]::new();[NativeAppTest]::GetCursorPos([ref]$savedWheelCursor)|Out-Null
 try{
  [NativeAppTest]::SetCursorPos($wheelPoint.x,$wheelPoint.y)|Out-Null;$wheelBefore=Window-Title $main
  [NativeAppTest]::mouse_event(0x0800,0,0,[uint32]4294967176,[UIntPtr]::Zero);$wheelAfterNext=Wait-ChangedTitle $main $wheelBefore
  Assert-Case ($wheelAfterNext -ne $wheelBefore) '鼠标滚轮向下切到下一张'
  [NativeAppTest]::mouse_event(0x0800,0,0,120,[UIntPtr]::Zero);$wheelAfterPrevious=Wait-ChangedTitle $main $wheelAfterNext
  Assert-Case ($wheelAfterPrevious -eq $wheelBefore) '鼠标滚轮向上返回上一张'
  [NativeAppTest]::SetCursorPos($wheelBounds.x+$wheelBounds.width+100,$wheelBounds.y+$wheelBounds.height+100)|Out-Null;Start-Sleep -Milliseconds 150;$outsideBefore=Window-Title $main
  [NativeAppTest]::mouse_event(0x0800,0,0,[uint32]4294967176,[UIntPtr]::Zero);Start-Sleep -Milliseconds 250
  Assert-Case ((Window-Title $main) -eq $outsideBefore) '相框外滚轮不拦截桌面滚动'
 }finally{[NativeAppTest]::SetCursorPos($savedWheelCursor.x,$savedWheelCursor.y)|Out-Null}
 }
 Command $main 13
 Command $main 29;Start-Sleep -Milliseconds 700
 $viewer=[NativeAppTest]::Find($process.Id,'查看大图')
 Assert-Case ([NativeAppTest]::IsWindowVisible($viewer)) '独立大图窗口打开'
 Command $main 29
 Assert-Case ([NativeAppTest]::Find($process.Id,'查看大图') -eq $viewer) '重复打开复用大图窗口'
 $before=[Text.StringBuilder]::new(512);[NativeAppTest]::GetWindowText($main,$before,512) | Out-Null
 Start-Sleep -Seconds 4
 $during=[Text.StringBuilder]::new(512);[NativeAppTest]::GetWindowText($main,$during,512) | Out-Null
 Assert-Case ($before.ToString() -eq $during.ToString() -and !(Read-State).global.paused) '预览暂停轮播且不改变保存的暂停开关'
 Command $viewer 2;Command $viewer 1
 [NativeAppTest]::PostMessage([NativeAppTest]::GetDlgItem($viewer,1),256,[IntPtr]27,[IntPtr]::Zero) | Out-Null;Start-Sleep -Milliseconds 150
 Assert-Case (![NativeAppTest]::IsWindow($viewer)) '按钮聚焦时Esc也能关闭大图'
 Start-Sleep -Seconds 4
 $after=[Text.StringBuilder]::new(512);[NativeAppTest]::GetWindowText($main,$after,512) | Out-Null
 Assert-Case ($before.ToString() -ne $after.ToString()) '关闭大图后恢复轮播'
 [NativeAppTest]::SendMessage($main,256,[IntPtr]32,[IntPtr]::Zero) | Out-Null
 Assert-Case ((Read-State).global.paused) '暂停状态保存'
 $inputWindow=[NativeAppTest]::Find($process.Id,'相框交互区域');$hostRect=[NativeAppTest+Rect]::new();[NativeAppTest]::GetWindowRect($main,[ref]$hostRect) | Out-Null
 $b=(Read-State).bounds;$frameRect=[NativeAppTest+Rect]::new();$frameRect.left=[int]$b.x;$frameRect.top=[int]$b.y;$frameRect.right=[int]($b.x+$b.width);$frameRect.bottom=[int]($b.y+$b.height);$monitor=[NativeAppTest+Monitor]::new();$monitor.size=[Runtime.InteropServices.Marshal]::SizeOf($monitor);[NativeAppTest]::GetMonitorInfo([NativeAppTest]::MonitorFromRect([ref]$frameRect,2),[ref]$monitor)|Out-Null
 foreach($corner in @('top-left','top-right','bottom-left','bottom-right')){
  $bounds=(Read-State).bounds;$x=[int]($bounds.x-$hostRect.left+40);$y=[int]($bounds.y-$hostRect.top+40)
  $targetX=$monitor.work.left-$hostRect.left+12;$targetY=$monitor.work.top-$hostRect.top+12
  if($corner.EndsWith('right')){$targetX=[int]($monitor.work.right-$hostRect.left-$bounds.width-12)}
  if($corner.StartsWith('bottom')){$targetY=[int]($monitor.work.bottom-$hostRect.top-$bounds.height-12)}
  [NativeAppTest]::SendMessage($inputWindow,513,[IntPtr]1,[IntPtr](($y -shl 16) -bor ($x -band 65535))) | Out-Null
  $position=[IntPtr]((($targetY+43) -shl 16) -bor (($targetX+43) -band 65535))
  [NativeAppTest]::SendMessage($inputWindow,512,[IntPtr]1,$position) | Out-Null
  [NativeAppTest]::SendMessage($inputWindow,514,[IntPtr]::Zero,$position) | Out-Null
  $snapped=Read-State
  @{corner=$corner;host=$hostRect;targetX=$targetX;targetY=$targetY;bounds=$snapped.bounds;anchor=$snapped.playlists[0].config.anchor} | ConvertTo-Json -Depth 4 | Add-Content (Join-Path $output 'snap-checks.jsonl')
  Assert-Case ([Math]::Abs($snapped.bounds.x-$hostRect.left-$targetX) -le 1 -and [Math]::Abs($snapped.bounds.y-$hostRect.top-$targetY) -le 1 -and $snapped.playlists[0].config.anchor -eq $corner) "吸附并同步固定角：$corner"
 }
 Command $main 29;Start-Sleep -Milliseconds 250
 $viewer=[NativeAppTest]::Find($process.Id,'查看大图')
 [NativeAppTest]::SendMessage($settings,16,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
 Assert-Case (![NativeAppTest]::IsWindow($settings) -and [NativeAppTest]::IsWindowVisible($main)) '关闭设置保留相框'
 [NativeAppTest]::SendMessage($viewer,16,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 [NativeAppTest]::SetWindowPos($main,[IntPtr](-1),0,0,0,0,19)|Out-Null;[NativeAppTest]::SetWindowPos($inputWindow,[IntPtr](-1),0,0,0,0,19)|Out-Null
 $savedCursor=[NativeAppTest+Point]::new();[NativeAppTest]::GetCursorPos([ref]$savedCursor)|Out-Null
 try{
  $b=(Read-State).bounds;[NativeAppTest]::SetCursorPos([int]($b.x+$b.width/2),[int]($b.y+$b.height/2))|Out-Null;Start-Sleep -Milliseconds 700
  Assert-Case ([NativeAppTest]::Find($process.Id,'相框工具栏') -eq [IntPtr]::Zero) '悬停不创建工具栏'
 }finally{[NativeAppTest]::SetCursorPos($savedCursor.x,$savedCursor.y)|Out-Null}
 Command $main 29;Start-Sleep -Milliseconds 250;$viewer=[NativeAppTest]::Find($process.Id,'查看大图')
 [NativeAppTest]::SendMessage($main,16,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
 Assert-Case (![NativeAppTest]::IsWindowVisible($main) -and !$process.HasExited) '关闭相框隐藏并保持进程'
 Assert-Case (![NativeAppTest]::IsWindow($viewer) -and (Read-State).global.paused) '隐藏相框关闭预览并保留手动暂停'
 [NativeAppTest]::PostMessage($main,32788,[IntPtr]::Zero,[IntPtr]514) | Out-Null;Start-Sleep -Milliseconds 250
 Assert-Case ([NativeAppTest]::IsWindowVisible($main)) '托盘左键命令恢复相框'
 Command $main 16
 $other=Start-FrameTest $Executable -WindowStyle Hidden -PassThru -ArgumentList @('--data-dir',('"'+$data+'"'),'--test-seconds','5')
 Assert-Case ($other.WaitForExit(5000)) '重复实例及时退出'
 Start-Sleep -Milliseconds 500
 Assert-Case ([NativeAppTest]::IsWindowVisible($main) -and !$process.HasExited) '重复启动恢复已有相框'
 Assert-Case ((Read-State).unknownFixture -eq 'preserve-me') '未知配置字段保留'
 Command $main 17;Assert-Case ($process.WaitForExit(5000)) '退出命令结束程序'
 @{checks=$checks;passed=$checks.Count;exitCode=$process.ExitCode;data=$data} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $output 'summary.json')
 Write-Output "$($checks.Count) 项窗口回归通过；$output"
} catch { @{passed=$checks;error=$_.Exception.Message} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $output 'failure.json');throw }

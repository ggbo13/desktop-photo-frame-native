param([string]$Executable='native/out/2.0.0-preview.20-delivery/DesktopPhotoFrameNative.exe',[ValidateRange(10,120)][int]$Seconds=25,[switch]$WithPhotos,[switch]$Automated,[switch]$ScrollTest,[switch]$AutomatedScroll,[switch]$IdleProbe,[switch]$MultipleLists)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class ManualResizeProfile {
 public delegate bool Proc(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int left,top,right,bottom;}
 [StructLayout(LayoutKind.Sequential)]public struct ScrollInfo{public uint cbSize,fMask;public int nMin,nMax;public uint nPage;public int nPos,nTrackPos;}
 [DllImport("user32.dll")]public static extern bool EnumWindows(Proc f,IntPtr p);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,StringBuilder t,int n);
 [DllImport("user32.dll")]public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint timeout,out IntPtr result);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int width,int height,uint flags);
 [DllImport("user32.dll")]public static extern IntPtr GetTopWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool GetScrollInfo(IntPtr h,int bar,ref ScrollInfo info);
 [DllImport("user32.dll",EntryPoint="GetWindowLongPtrW")]public static extern IntPtr GetWindowLongPtr(IntPtr h,int index);
 [DllImport("user32.dll",EntryPoint="SendMessageTimeoutW")]public static extern IntPtr SizeMessage(IntPtr h,uint m,IntPtr w,ref Rect r,uint f,uint timeout,out IntPtr result);
 public static IntPtr Find(int pid,string name){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);if(id==pid){var t=new StringBuilder(512);GetWindowText(h,t,512);if(t.ToString().Contains(name))found=h;}return true;},IntPtr.Zero);return found;}
}
'@
$root=Split-Path (Split-Path $PSScriptRoot)
$output=Join-Path $root ('native/artifacts/manual-resize-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$data=Join-Path $output 'data';New-Item -ItemType Directory -Path $data -Force|Out-Null
$sources=@();if($WithPhotos){Add-Type -AssemblyName System.Drawing;$photos=Join-Path $output 'photos';New-Item -ItemType Directory -Path $photos -Force|Out-Null;for($i=0;$i-lt4;$i++){$bitmap=[Drawing.Bitmap]::new(96,64);$graphics=[Drawing.Graphics]::FromImage($bitmap);$graphics.Clear([Drawing.Color]::FromArgb(40+$i*40,140,180));$bitmap.Save((Join-Path $photos "$i.bmp"));$graphics.Dispose();$bitmap.Dispose()};$sources=@(@{type='folder';path=$photos})}
$lists=@(@{id='manual';name='拖动测试';sources=$sources;config=@{ratio='free';transition='none'}});if($MultipleLists){foreach($i in 1..5){$lists+=@{id="manual-$i";name="测试列表 $i";sources=$sources;config=@{ratio='free';transition='none'}}}}
@{global=@{paused=$true;theme='dark'};playlists=$lists;settingsBounds=@{x=80;y=80;width=1280;height=800}}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $data 'settings.json') -Encoding utf8
$process=Start-FrameTest $Executable -PassThru -ArgumentList @('--data-dir',('"'+$data+'"'),'--test-hooks','--test-seconds','180')
$main=[IntPtr]::Zero
function Message($h,$m,[long]$w=0){$r=[IntPtr]::Zero;if([ManualResizeProfile]::SendMessageTimeout($h,$m,[IntPtr]$w,[IntPtr]::Zero,2,2000,[ref]$r)-eq[IntPtr]::Zero){throw "窗口消息超时：$m / $w"};$r.ToInt64()}
try{
 for($i=0;$i-lt60-and$main-eq[IntPtr]::Zero;$i++){Start-Sleep -Milliseconds 100;$main=[ManualResizeProfile]::Find($process.Id,'原生版')}
 if($main-eq[IntPtr]::Zero){throw '测试程序未启动'}
 Message $main 273 10|Out-Null
 $settings=[ManualResizeProfile]::Find($process.Id,'照片与设置');if($settings-eq[IntPtr]::Zero){throw '测试设置窗口未打开'}
 if($ScrollTest-or$AutomatedScroll){Message $settings 273 401|Out-Null;Start-Sleep -Milliseconds 500}
 if((Message $main 32835)-ne1){throw '测试计时接口不可用'}
 if($AutomatedScroll){
  $beforeStyle=[ManualResizeProfile]::GetWindowLongPtr($settings,-20).ToInt64();$beforeLayouts=Message $main 32830 50;$info=[ManualResizeProfile+ScrollInfo]::new();$info.cbSize=[Runtime.InteropServices.Marshal]::SizeOf([type][ManualResizeProfile+ScrollInfo]);$info.fMask=23;if(![ManualResizeProfile]::GetScrollInfo($settings,1,[ref]$info)){throw '无法读取滚动状态'};$target=[Math]::Min(180,[Math]::Max(0,$info.nMax-[int]$info.nPage+1));if($target-lt1){throw '测试窗口没有可滚动范围'}
  Message $settings 277 5|Out-Null;Message $settings 277 (4+($target-shl16))|Out-Null;Message $settings 277 8|Out-Null;$info.fMask=23;if(![ManualResizeProfile]::GetScrollInfo($settings,1,[ref]$info)-or$info.nPos-ne$target){throw '松开滚动条后的最终位置不正确'};if([ManualResizeProfile]::GetWindowLongPtr($settings,-20).ToInt64()-ne$beforeStyle){throw '滚动结束后窗口样式未恢复'};if((Message $main 32830 50)-ne$beforeLayouts){throw '滚动时触发了整页重排'};Write-Output '低负载滚动位置与独立容器检查通过'
 }elseif($Automated){
  $originalStyle=[ManualResizeProfile]::GetWindowLongPtr($settings,-20).ToInt64();$beforeTop=[ManualResizeProfile]::GetTopWindow($settings);$rect=[ManualResizeProfile+Rect]::new();if(![ManualResizeProfile]::GetWindowRect($settings,[ref]$rect)){throw '无法读取窗口尺寸'}
  Message $settings 561|Out-Null;$sizingResult=[IntPtr]::Zero;if([ManualResizeProfile]::SizeMessage($settings,532,[IntPtr]8,[ref]$rect,2,2000,[ref]$sizingResult)-eq[IntPtr]::Zero){throw '无法发送尺寸调整消息'}
  $cover=[ManualResizeProfile]::GetTopWindow($settings);if($cover-eq[IntPtr]::Zero-or$cover-eq$beforeTop){throw '未创建临时缓存窗口'};if(([ManualResizeProfile]::GetWindowLongPtr($settings,-20).ToInt64()-band 0x02000000)-ne0){throw '拖动期间仍在合成整个控件树'}
  foreach($delta in @(30,60,0)){if(![ManualResizeProfile]::SetWindowPos($settings,[IntPtr]::Zero,0,0,($rect.right-$rect.left+$delta),($rect.bottom-$rect.top+$delta),20)){throw '调整测试窗口失败'};Start-Sleep -Milliseconds 150}
  Message $settings 562|Out-Null;if([ManualResizeProfile]::IsWindow($cover)){throw '拖动结束后临时窗口未销毁'};if([ManualResizeProfile]::GetWindowLongPtr($settings,-20).ToInt64()-ne$originalStyle){throw '拖动结束后窗口样式未恢复'};Write-Output '低负载缩放生命周期检查通过'
 }else{if($IdleProbe){Write-Output "测试设置窗口已打开，正在观察静止状态；$Seconds 秒后自动结束。";$idleCounts=@();for($i=0;$i-lt$Seconds;$i++){Start-Sleep -Seconds 1;$idleCounts+=Message $main 32830 56}}else{if($ScrollTest){Write-Output "测试设置窗口已打开，请拖动右侧滚动条约 5 秒；$Seconds 秒后测试自动结束。"}else{Write-Output "测试设置窗口已打开，请拖动其右下角约 5 秒；$Seconds 秒后测试自动结束。"};Start-Sleep -Seconds $Seconds}}
 $names=@('layout','redraw','preview','planning','position','visibility');$result=@{note=if($Automated-or$AutomatedScroll){'低频模拟窗口消息；无截图或持续采集'}elseif($IdleProbe){'静止状态；无截图或持续采集'}else{'用户真实鼠标操作；仅记录程序累计处理耗时，无截图或持续采集'};seconds=$Seconds;thumbnailLoads=(Message $main 32830 24)}
 for($i=0;$i-lt$names.Count;$i++){$base=50+$i*3;$result[$names[$i]]=@{calls=(Message $main 32830 $base);totalUs=(Message $main 32830 ($base+1));maxUs=(Message $main 32830 ($base+2))}}
 if($IdleProbe){$result.idlePreviewCounts=$idleCounts}
 $result|ConvertTo-Json -Depth 5|Set-Content (Join-Path $output 'summary.json') -Encoding utf8
 $result|ConvertTo-Json -Depth 5;Write-Output "报告：$output"
}finally{
 if(!$process.HasExited){if($main-ne[IntPtr]::Zero){[ManualResizeProfile]::PostMessage($main,273,[IntPtr]17,[IntPtr]::Zero)|Out-Null};if(!$process.WaitForExit(5000)){Stop-Process -Id $process.Id}}
}

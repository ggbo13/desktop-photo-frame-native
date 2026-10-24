param([string]$Executable='native/out/2.0.0-preview.13-delivery/DesktopPhotoFrameNative.exe',[int[]]$Deltas=@(-50,-51),[string]$Ratio='1:1',[ValidateSet('top-left','top-right','bottom-left','bottom-right','left','right','top','bottom')][string]$Handle='bottom-right',[switch]$ReplayQueuedMove)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class ResizeContinuityProbe {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int left,top,right,bottom;}
 [StructLayout(LayoutKind.Sequential)]public struct Point{public int x,y;}
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 [DllImport("user32.dll")]public static extern bool EnumWindows(EnumProc callback,IntPtr p);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint process);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,StringBuilder text,int count);
 [DllImport("user32.dll")]public static extern IntPtr SendMessage(IntPtr h,uint message,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint message,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect rect);
 [DllImport("user32.dll")]public static extern bool ScreenToClient(IntPtr h,ref Point point);
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]public static extern bool GetCursorPos(out Point point);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int action);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int width,int height,uint flags);
 public static IntPtr Find(int processId,string title){IntPtr result=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);var text=new StringBuilder(512);GetWindowText(h,text,512);if(id==(uint)processId&&text.ToString().Contains(title))result=h;return true;},IntPtr.Zero);return result;}
 public static string Title(IntPtr h){var text=new StringBuilder(512);GetWindowText(h,text,512);return text.ToString();}
 public static Rect lastRect;public static Point lastCursor;
 public static void Mouse(IntPtr h,uint message,int x,int y){SetCursorPos(x,y);System.Threading.Thread.Sleep(20);var point=new Point{x=x,y=y};ScreenToClient(h,ref point);int packed=(point.x&65535)|((point.y&65535)<<16);SendMessage(h,message,new IntPtr(message==514?0:1),new IntPtr(packed));GetWindowRect(h,out lastRect);GetCursorPos(out lastCursor);}
 public static Rect[] QueuedMove(IntPtr h,int x,int y){var point=new Point{x=x,y=y};ScreenToClient(h,ref point);int packed=(point.x&65535)|((point.y&65535)<<16);SetCursorPos(x,y);System.Threading.Thread.Sleep(20);SendMessage(h,512,new IntPtr(1),new IntPtr(packed));Rect first;GetWindowRect(h,out first);SendMessage(h,512,new IntPtr(1),new IntPtr(packed));Rect second;GetWindowRect(h,out second);return new[]{first,second};}
}
'@
Add-Type -AssemblyName System.Drawing
[ResizeContinuityProbe]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
$root=Split-Path (Split-Path $PSScriptRoot)
$output=Join-Path $root ('native/artifacts/resize-continuity-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$data=Join-Path $output 'data';New-Item -ItemType Directory -Path $data -Force|Out-Null
$photo=Join-Path $output 'square.bmp';$bitmap=[Drawing.Bitmap]::new(500,500);$g=[Drawing.Graphics]::FromImage($bitmap);$g.Clear([Drawing.Color]::DarkCyan);$bitmap.Save($photo);$g.Dispose();$bitmap.Dispose()
@{bounds=@{x=250;y=180;width=320;height=320};global=@{paused=$true;pauseFullscreen=$false};playlists=@(@{id='resize';name='连续缩放验证';sources=@(@{type='file';path=$photo});config=@{ratio=$Ratio;transition='none'}})}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $data 'settings.json') -Encoding utf8
$binary=[IO.Path]::GetFullPath((Join-Path $root $Executable));$process=Start-FrameTest $binary -PassThru -ArgumentList @('--data-dir',('"'+$data+'"'),'--test-hooks','--test-seconds','35')
$main=[IntPtr]::Zero;$samples=[Collections.Generic.List[object]]::new();$queuedSamples=[Collections.Generic.List[object]]::new();$queuedError=0
try{
 for($i=0;$i -lt 80;$i++){Start-Sleep -Milliseconds 80;$main=[ResizeContinuityProbe]::Find($process.Id,'桌面相框 原生版');if($main -ne [IntPtr]::Zero -and [ResizeContinuityProbe]::Title($main).Contains('.bmp')){break}}
 if($main -eq [IntPtr]::Zero){throw '相框未启动'}
 $input=[ResizeContinuityProbe]::Find($process.Id,'相框交互区域')
 foreach($window in $main,$input){[ResizeContinuityProbe]::ShowWindow($window,4)|Out-Null;[ResizeContinuityProbe]::SetWindowPos($window,[IntPtr](-1),0,0,0,0,19)|Out-Null}
 $rect=[ResizeContinuityProbe+Rect]::new();[ResizeContinuityProbe]::GetWindowRect($input,[ref]$rect)|Out-Null
 $left=$Handle.Contains('left');$right=$Handle.Contains('right');$top=$Handle.Contains('top');$bottom=$Handle.Contains('bottom')
 $corner=($left -or $right) -and ($top -or $bottom);$signX=if($left){-1}else{1};$signY=if($top){-1}else{1}
 $startX=if($left){$rect.left+8}elseif($right){$rect.right-8}else{[int](($rect.left+$rect.right)/2)}
 $startY=if($top){$rect.top+8}elseif($bottom){$rect.bottom-8}else{[int](($rect.top+$rect.bottom)/2)}
 $initialWidth=$rect.right-$rect.left-1;$initialHeight=$rect.bottom-$rect.top-1
 [ResizeContinuityProbe]::Mouse($input,513,$startX,$startY)
 foreach($dy in $Deltas){
  $moveX=if($corner){$signX*50}elseif($left -or $right){$signX*$dy}else{0}
  $moveY=if($top -or $bottom){$signY*$dy}else{0}
  if($ReplayQueuedMove){foreach($queuedRect in [ResizeContinuityProbe]::QueuedMove($input,($startX+$moveX),($startY+$moveY))){$queuedWidth=$queuedRect.right-$queuedRect.left-1;$queuedHeight=$queuedRect.bottom-$queuedRect.top-1;$queuedSamples.Add(@{dx=$moveX;dy=$moveY;width=$queuedWidth;height=$queuedHeight});if($Ratio -eq 'free'){$expectedWidth=$initialWidth+$(if($left -or $right){$signX*$moveX}else{0});$expectedHeight=$initialHeight+$(if($top -or $bottom){$signY*$moveY}else{0});$queuedError=[Math]::Max($queuedError,[Math]::Max([Math]::Abs($queuedWidth-$expectedWidth),[Math]::Abs($queuedHeight-$expectedHeight)))}}}else{[ResizeContinuityProbe]::Mouse($input,512,($startX+$moveX),($startY+$moveY))}
  if($ReplayQueuedMove){$sampleRect=$queuedRect}else{$sampleRect=[ResizeContinuityProbe]::lastRect}
  $width=$sampleRect.right-$sampleRect.left-1;$height=$sampleRect.bottom-$sampleRect.top-1
  $samples.Add(@{dx=$moveX;dy=$moveY;width=$width;height=$height;cursor=[ResizeContinuityProbe]::lastCursor;target=@(($startX+$moveX),($startY+$moveY))})
 }
 $maxJump=0;for($i=1;$i -lt $samples.Count;$i++){$maxJump=[Math]::Max($maxJump,[Math]::Max([Math]::Abs($samples[$i].width-$samples[$i-1].width),[Math]::Abs($samples[$i].height-$samples[$i-1].height)))}
 $after=[ResizeContinuityProbe+Rect]::new();[ResizeContinuityProbe]::GetWindowRect($input,[ref]$after)|Out-Null
 $anchorErrorX=if($left){[Math]::Abs($after.right-$rect.right)}elseif($right){[Math]::Abs($after.left-$rect.left)}else{0}
 $anchorErrorY=if($top){[Math]::Abs($after.bottom-$rect.bottom)}elseif($bottom){[Math]::Abs($after.top-$rect.top)}else{0}
 [ResizeContinuityProbe]::Mouse($input,512,$startX,$startY)
 $outX=if($left -or $right){$signX*20}else{0};$outY=if($top -or $bottom){$signY*20}else{0}
 [ResizeContinuityProbe]::Mouse($input,512,($startX+$outX),($startY+$outY))
 $grownRect=[ResizeContinuityProbe]::lastRect;$grownWidth=$grownRect.right-$grownRect.left-1;$grownHeight=$grownRect.bottom-$grownRect.top-1
 [ResizeContinuityProbe]::Mouse($input,512,$startX,$startY)
 $returnedRect=[ResizeContinuityProbe]::lastRect;$returnedWidth=$returnedRect.right-$returnedRect.left-1;$returnedHeight=$returnedRect.bottom-$returnedRect.top-1
 [ResizeContinuityProbe]::Mouse($input,514,$startX,$startY)
 @{samples=$samples;maxJump=$maxJump;ratio=$Ratio;handle=$Handle;queuedReplay=[bool]$ReplayQueuedMove;queuedSamples=$queuedSamples;queuedError=$queuedError;anchorErrorX=$anchorErrorX;anchorErrorY=$anchorErrorY;initial=@($initialWidth,$initialHeight);grown=@($grownWidth,$grownHeight);returned=@($returnedWidth,$returnedHeight);executable=$binary}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $output 'samples.json') -Encoding utf8
 if($maxJump -gt 4){throw "鼠标每步移动1像素，窗口宽度却跳变 $maxJump 像素；记录：$output"}
 if($queuedError -gt 1){throw "旧鼠标消息导致窗口尺寸偏差 $queuedError 像素；记录：$output"}
 if($Ratio -eq 'free'){$expectedWidth=$initialWidth+$(if($left -or $right){$signX*$moveX}else{0});$expectedHeight=$initialHeight+$(if($top -or $bottom){$signY*$moveY}else{0});if([Math]::Abs($samples[-1].width-$expectedWidth) -gt 1 -or [Math]::Abs($samples[-1].height-$expectedHeight) -gt 1){throw '鼠标停住后尺寸与拖动位置不符'}}
 if($anchorErrorX -gt 1 -or $anchorErrorY -gt 1){throw '缩放时对侧固定位置偏移超过1像素'}
 if([Math]::Max($grownWidth-$initialWidth,$grownHeight-$initialHeight) -lt 8){throw '向外拖动未连续放大'}
 if([Math]::Abs($returnedWidth-$initialWidth) -gt 1 -or [Math]::Abs($returnedHeight-$initialHeight) -gt 1){throw '反向拖回起点未恢复原尺寸'}
 Write-Output "连续缩放检查通过：$Ratio / $Handle，最大相邻变化 $maxJump 像素；$output"
}finally{if(!$process.HasExited -and $main -ne [IntPtr]::Zero){[ResizeContinuityProbe]::PostMessage($main,273,[IntPtr]17,[IntPtr]::Zero)|Out-Null;if(!$process.WaitForExit(5000)){Stop-Process -Id $process.Id -Force}}}

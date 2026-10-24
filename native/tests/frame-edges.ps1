param([string]$Executable='native/out/2.0.0-preview.13-delivery/DesktopPhotoFrameNative.exe')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class FrameEdgeUI {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)]public struct Point{public int x,y;public Point(int a,int b){x=a;y=b;}}
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]static extern bool EnumWindows(EnumProc f,IntPtr p);
 [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")]public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",EntryPoint="SendMessageW",CharSet=CharSet.Unicode)]public static extern IntPtr SendText(IntPtr h,uint m,IntPtr w,string text);
 [DllImport("user32.dll")]public static extern IntPtr WindowFromPoint(Point p);
 public static IntPtr Find(int pid,string name){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);var text=new StringBuilder(512);GetWindowText(h,text,512);if(id==pid&&text.ToString().Contains(name))found=h;return true;},IntPtr.Zero);return found;}
}
'@
[FrameEdgeUI]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
$root=Split-Path (Split-Path $PSScriptRoot);$output=Join-Path $root ('native/artifacts/edges-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $output -Force|Out-Null
$photo=Join-Path $output 'solid-photo.bmp';$image=[Drawing.Bitmap]::new(1280,720);$graphics=[Drawing.Graphics]::FromImage($image);$graphics.Clear([Drawing.Color]::FromArgb(219,71,59));$image.Save($photo);$graphics.Dispose();$image.Dispose()
$checks=[System.Collections.Generic.List[object]]::new()
function Send-Command($h,[int]$id){[FrameEdgeUI]::SendMessage($h,273,[IntPtr]$id,[IntPtr]::Zero)|Out-Null}
function Set-Value($h,[int]$id,[string]$value){[FrameEdgeUI]::SendText([FrameEdgeUI]::GetDlgItem($h,$id),12,[IntPtr]::Zero,$value)|Out-Null}
function Is-Photo($color){return [Math]::Abs($color.R-219) -le 3 -and [Math]::Abs($color.G-71) -le 3 -and [Math]::Abs($color.B-59) -le 3}
foreach($scale in @(1,1.25,1.5)){
 $data=Join-Path $output "scale-$scale";New-Item -ItemType Directory -Path $data -Force|Out-Null;$config=Join-Path $data 'settings.json'
 @{global=@{paused=$true;alwaysOnTop=$true};bounds=@{x=120;y=120;width=(480*$scale);height=(360*$scale)};playlists=@(@{id='edges';name='边缘验证';sources=@(@{type='file';path=$photo});config=@{ratio='auto';transition='none';anchor='top-left';shadow=0}})}|ConvertTo-Json -Depth 9|Set-Content $config -Encoding utf8
 $process=Start-FrameTest $Executable -ArgumentList @('--data-dir',('"'+$data+'"'),'--test-seconds','60') -WindowStyle Hidden -PassThru
 Start-Sleep -Milliseconds 700;$main=[FrameEdgeUI]::Find($process.Id,'原生版');$inputWindow=[FrameEdgeUI]::Find($process.Id,'相框交互区域');if($main -eq [IntPtr]::Zero){throw '相框未启动'};Send-Command $main 10;Start-Sleep -Milliseconds 250;$settings=[FrameEdgeUI]::Find($process.Id,'照片与设置')
 for($style=0;$style -lt 5;$style++){foreach($thickness in @(6,64)){
  [FrameEdgeUI]::SendMessage([FrameEdgeUI]::GetDlgItem($settings,104),334,[IntPtr]$style,[IntPtr]::Zero)|Out-Null;Set-Value $settings 110 "$thickness";Set-Value $settings 111 $(if($thickness -eq 6){'0'}else{'32'});Set-Value $settings 119 '0';Send-Command $settings 24;Start-Sleep -Milliseconds 180
  $state=Get-Content $config -Raw|ConvertFrom-Json;$bounds=$state.bounds;$border=if($style -eq 4){0}else{$thickness};$width=[int]$bounds.width;$height=[int]$bounds.height
  $capture=[Drawing.Bitmap]::new($width,$height);$dc=[Drawing.Graphics]::FromImage($capture);$dc.CopyFromScreen([int]$bounds.x,[int]$bounds.y,0,0,$capture.Size);$dc.Dispose()
  $points=@(@($border,[int]($height/2)),@(($width-$border-1),[int]($height/2)),@([int]($width/2),$border),@([int]($width/2),($height-$border-1)))
  $pass=$true;foreach($point in $points){if(!(Is-Photo ($capture.GetPixel($point[0],$point[1])))){$pass=$false}}
  $capture.Save((Join-Path $output "frame-$scale-$style-$thickness.png"));$capture.Dispose()
  $hit=[FrameEdgeUI]::WindowFromPoint([FrameEdgeUI+Point]::new([int]$bounds.x,[int]$bounds.y));$roundCorrect=if($thickness -eq 64){$hit -ne $inputWindow}else{$hit -eq $inputWindow}
  $checks.Add(@{scale=$scale;style=$style;thickness=$thickness;photoTouchesAllEdges=$pass;cornerHitRegionCorrect=$roundCorrect;width=$width;height=$height})
 }}
 Send-Command $main 17;if(!$process.WaitForExit(5000)){throw '边缘测试程序未正常退出'}
}
$failures=@($checks|Where-Object {!$_.photoTouchesAllEdges -or !$_.cornerHitRegionCorrect});@{checks=$checks;checked=$checks.Count;failures=$failures;realDpiTest=$false} | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $output 'summary.json')
if($failures.Count){throw "$($failures.Count) 项边缘像素/圆角命中测试失败；$output"}
Write-Output "$($checks.Count) 项边缘像素与圆角命中测试通过；$output"

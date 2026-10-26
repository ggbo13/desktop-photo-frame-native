param([string]$Executable='native/out/2.0.0-preview.15-delivery/DesktopPhotoFrameNative.exe')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class ViewerClickProbe {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")]public static extern bool EnumWindows(EnumProc callback,IntPtr p);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint process);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,StringBuilder value,int length);
 [DllImport("user32.dll")]public static extern IntPtr SendMessage(IntPtr h,uint message,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint message,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern uint GetDoubleClickTime();
 public static IntPtr Find(int processId,string title){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);if(id==(uint)processId){var value=new StringBuilder(512);GetWindowText(h,value,512);if(value.ToString().Contains(title))found=h;}return true;},IntPtr.Zero);return found;}
 public static string Title(IntPtr h){var value=new StringBuilder(512);GetWindowText(h,value,512);return value.ToString();}
}
'@
Add-Type -AssemblyName System.Drawing
$root=Split-Path (Split-Path $PSScriptRoot)
$binary=[IO.Path]::GetFullPath((Join-Path $root $Executable))
$output=Join-Path $root ('native/artifacts/viewer-doubleclick-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$photos=Join-Path $output 'photos';New-Item -ItemType Directory -Path $photos -Force|Out-Null
$picture=[Drawing.Bitmap]::new(400,300);$paint=[Drawing.Graphics]::FromImage($picture);$paint.Clear([Drawing.Color]::DarkCyan);$picture.Save((Join-Path $photos 'test-photo.bmp'));$paint.Dispose();$picture.Dispose()
$checks=[Collections.Generic.List[string]]::new()
function Check($condition,[string]$name){if(!$condition){throw "失败：$name"};$checks.Add($name)}
function Message($h,[int]$message,[int]$w=0,[int]$l=0){[ViewerClickProbe]::SendMessage($h,[uint32]$message,[IntPtr]$w,[IntPtr]$l)|Out-Null}
function Viewer($processId){[ViewerClickProbe]::Find($processId,'查看大图 ·')}
function Click($h){Message $h 513 1 6553700;Message $h 514 0 6553700}
function DoubleClick($h){Message $h 515 1 6553700;Message $h 514 0 6553700}
foreach($run in 1,2){
 $data=Join-Path $output "data-$run";New-Item -ItemType Directory -Path $data -Force|Out-Null
 @{bounds=@{x=150;y=130;width=420;height=320};global=@{paused=$true;pauseFullscreen=$false};playlists=@(@{id='click';name='双击验证';sources=@(@{type='folder';path=$photos});config=@{ratio='free';transition='none'}})}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $data 'settings.json') -Encoding utf8
 $process=Start-FrameTest -FilePath $binary -PassThru -ArgumentList @('--data-dir',('"'+$data+'"'),'--test-seconds','35')
 $main=[IntPtr]::Zero
 try{
  for($i=0;$i -lt 80 -and $main -eq [IntPtr]::Zero;$i++){Start-Sleep -Milliseconds 100;$main=[ViewerClickProbe]::Find($process.Id,'桌面相框 原生版')}
  Check ($main -ne [IntPtr]::Zero) "第$run 次启动"
  for($i=0;$i -lt 80 -and !([ViewerClickProbe]::Title($main).Contains('.bmp'));$i++){Start-Sleep -Milliseconds 100}
  Check ([ViewerClickProbe]::Title($main).Contains('.bmp')) "第$run 次照片加载"
  $input=[ViewerClickProbe]::Find($process.Id,'相框交互区域');Check ($input -ne [IntPtr]::Zero) "第$run 次交互窗口"
  DoubleClick $input;Start-Sleep -Milliseconds 100
  Check ((Viewer $process.Id) -eq [IntPtr]::Zero) "第$run 次启动后孤立双击消息不会打开大图"
  Start-Sleep -Milliseconds ([int][ViewerClickProbe]::GetDoubleClickTime()+80)
  Click $input;Check ((Viewer $process.Id) -eq [IntPtr]::Zero) "第$run 次首次完整单击不会打开大图"
  DoubleClick $input;Start-Sleep -Milliseconds 100
  $viewer=Viewer $process.Id;Check ($viewer -ne [IntPtr]::Zero) "第$run 次真正双击打开大图"
  Message $viewer 16;Start-Sleep -Milliseconds 100
  Check ((Viewer $process.Id) -eq [IntPtr]::Zero) "第$run 次关闭大图"
  Message $main 273 16;Message $main 273 16
  Click $input;DoubleClick $input;Start-Sleep -Milliseconds 100
  Check ((Viewer $process.Id) -eq [IntPtr]::Zero) "第$run 次托盘恢复后的启动点击不会打开大图"
  Start-Sleep -Milliseconds ([int][ViewerClickProbe]::GetDoubleClickTime()+80)
  Click $input;DoubleClick $input;Start-Sleep -Milliseconds 100
  $viewer=Viewer $process.Id;Check ($viewer -ne [IntPtr]::Zero) "第$run 次恢复后正常双击可用"
  Message $viewer 16;Message $main 273 29;Start-Sleep -Milliseconds 100
  $viewer=Viewer $process.Id;Check ($viewer -ne [IntPtr]::Zero) "第$run 次托盘大图命令可用"
  Message $viewer 16;Message $main 273 17
  Check ($process.WaitForExit(5000)) "第$run 次正常退出"
 }finally{if(!$process.HasExited){if($main -ne [IntPtr]::Zero){[ViewerClickProbe]::PostMessage($main,273,[IntPtr]17,[IntPtr]::Zero)|Out-Null};if(!$process.WaitForExit(3000)){Stop-Process -Id $process.Id -Force}}}
}
@{passed=$checks.Count;checks=$checks;executable=$binary;doubleClickTime=[ViewerClickProbe]::GetDoubleClickTime()}|ConvertTo-Json -Depth 4|Set-Content (Join-Path $output 'summary.json') -Encoding utf8
Write-Output "$($checks.Count) 项启动与双击检查通过；$output"

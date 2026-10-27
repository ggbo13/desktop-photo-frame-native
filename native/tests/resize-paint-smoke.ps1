param([string]$Executable='native/out/2.0.0-preview.17-delivery/DesktopPhotoFrameNative.exe',[switch]$RecordVideo)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
. (Join-Path $PSScriptRoot 'frame-tray-history.ps1')
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class PaintResizeProbe {
 public delegate bool Proc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")]public static extern bool EnumWindows(Proc f,IntPtr p);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,StringBuilder text,int n);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint flags,uint timeout,out IntPtr value);
 public static IntPtr Find(int pid,string part){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);if(id!=pid)return true;var t=new StringBuilder(512);GetWindowText(h,t,512);if(t.ToString().Contains(part))found=h;return true;},IntPtr.Zero);return found;}
}
'@
Add-Type -AssemblyName System.Drawing
$root=Split-Path (Split-Path $PSScriptRoot);$output=Join-Path $root ('native/artifacts/resize-paint-'+(Get-Date -Format yyyyMMdd-HHmmss));$photos=Join-Path $output 'photos';$data=Join-Path $output 'data';New-Item -ItemType Directory -Path $photos,$data -Force|Out-Null
$witnessPath=Join-Path $root 'native/out/resize-paint-witness/ResizePaintWitness.exe';$witnessSources=@('resize-paint-witness.cpp','wgc-source.h','video-writer.h')|ForEach-Object{Get-Item (Join-Path $PSScriptRoot $_)}
if(!(Test-Path $witnessPath)-or(Get-Item $witnessPath).LastWriteTimeUtc-lt($witnessSources|Sort-Object LastWriteTimeUtc -Descending|Select-Object -First 1).LastWriteTimeUtc){New-Item -ItemType Directory -Path (Split-Path $witnessPath) -Force|Out-Null;& (Join-Path $root 'native/toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/clang++.exe') -std=c++20 -O2 -municode -mwindows -static -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 (Join-Path $PSScriptRoot 'resize-paint-witness.cpp') -o $witnessPath -ld3d11 -ldwmapi -lole32 -lshell32 -luuid -luser32 -lgdi32 -lruntimeobject -lmfplat -lmfreadwrite -lmfuuid;if($LASTEXITCODE-ne0){throw '窗口采集工具构建失败'}}
for($i=0;$i-lt24;$i++){$b=[Drawing.Bitmap]::new(720,480);$g=[Drawing.Graphics]::FromImage($b);$g.Clear([Drawing.Color]::FromArgb(20,190,155));$b.Save((Join-Path $photos "$i.bmp"));$g.Dispose();$b.Dispose()}
@{global=@{paused=$true;theme='dark'};playlists=@(@{id='paint';name='缩放重绘';sources=@(@{type='folder';path=$photos});config=@{ratio='free';transition='none'}});settingsBounds=@{x=60;y=60;width=1280;height=800}}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $data 'settings.json') -Encoding utf8
$before=@(Get-FrameTestTrayHistory);$binary=(Resolve-Path $Executable).Path;$main=[IntPtr]::Zero;$results=@();$process=Start-FrameTest $Executable -WindowStyle Hidden -PassThru -ArgumentList @('--data-dir',('"'+$data+'"'),'--test-hooks','--test-seconds','90')
function Message($h,$m,[long]$w=0,[long]$l=0){$value=[IntPtr]::Zero;if([PaintResizeProbe]::SendMessageTimeout($h,$m,[IntPtr]$w,[IntPtr]$l,2,2500,[ref]$value)-eq[IntPtr]::Zero){throw "被测窗口消息超时：$m / $w"};$value.ToInt64()}
function Witness($window,$name){$directory=Join-Path $output $name;$arguments=@($window.ToInt64(),('"'+$directory+'"'),$name);if($RecordVideo){$arguments+='--video'};$helper=Start-Process $witnessPath -WindowStyle Hidden -PassThru -ArgumentList $arguments;if(!$helper.WaitForExit(15000)){Stop-Process -Id $helper.Id;throw '窗口采集超时'};if(!(Test-Path (Join-Path $directory 'summary.json'))){throw "采集失败：$directory"};$s=Get-Content (Join-Path $directory 'summary.json') -Raw|ConvertFrom-Json;$s|Add-Member mode $name;$s|Add-Member exitCode $helper.ExitCode;return $s}
try{
 for($i=0;$i-lt60-and$main-eq[IntPtr]::Zero;$i++){Start-Sleep -Milliseconds 80;$main=[PaintResizeProbe]::Find($process.Id,'原生版')};if($main-eq[IntPtr]::Zero){throw '相框未启动'}
 for($i=0;$i-lt40-and(Message $main 32830 23)-eq0;$i++){Start-Sleep -Milliseconds 100};Message $main 273 10|Out-Null;Message $main 32831 100|Out-Null;Start-Sleep -Milliseconds 1800;$settings=[PaintResizeProbe]::Find($process.Id,'照片与设置');$results+=Witness $settings 'settings';Message $settings 16|Out-Null
 Message $main 273 29|Out-Null;Start-Sleep -Milliseconds 1200;$viewer=[PaintResizeProbe]::Find($process.Id,'查看大图');if($viewer-eq[IntPtr]::Zero){throw '大图未打开'};$results+=Witness $viewer 'viewer';Message $viewer 16|Out-Null
 Message $main 273 17|Out-Null;if(!$process.WaitForExit(5000)-or$process.ExitCode-ne0){throw '相框未正常退出'}
 $after=@(Get-FrameTestTrayHistory);$added=@($after|Where-Object{$_.path-ieq$binary-and$_.key-notin@($before.key)}).Count;$passed=(@($results|Where-Object {!$_.passed}).Count-eq0-and$added-eq0);@{passed=$passed;source='Windows Graphics Capture window frames';results=$results;newTestTrayRecords=$added}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $output 'summary.json') -Encoding utf8;$results|ConvertTo-Json;Write-Output $output;if(!$passed){throw '缩放期间出现内容丢失或重复隐藏'}
}finally{if(!$process.HasExited){[PaintResizeProbe]::PostMessage($main,273,[IntPtr]17,[IntPtr]::Zero)|Out-Null;if(!$process.WaitForExit(3000)){Stop-Process -Id $process.Id}}}

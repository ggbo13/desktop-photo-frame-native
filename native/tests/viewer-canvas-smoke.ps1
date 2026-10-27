param([string]$Executable='native/out/2.0.0-preview.20-delivery/DesktopPhotoFrameNative.exe')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
. (Join-Path $PSScriptRoot 'frame-tray-history.ps1')
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class ViewerCanvasProbe {
 public delegate bool Proc(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int left,top,right,bottom;}
 [StructLayout(LayoutKind.Sequential)]public struct Point{public int x,y;}
 [DllImport("user32.dll")]public static extern bool EnumWindows(Proc f,IntPtr p);
 [DllImport("user32.dll")]public static extern bool EnumChildWindows(IntPtr h,Proc f,IntPtr p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetClassName(IntPtr h,StringBuilder text,int n);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,StringBuilder text,int n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string name,string title);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool GetClientRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]public static extern bool ClientToScreen(IntPtr h,ref Point p);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr z,int x,int y,int width,int height,uint flags);
 [DllImport("user32.dll")]public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint flags,uint timeout,out IntPtr value);
 public static IntPtr Find(int pid,string part){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);if(id!=pid)return true;var t=new StringBuilder(512);GetWindowText(h,t,512);if(t.ToString().Contains(part))found=h;return true;},IntPtr.Zero);return found;}
 public static string Children(IntPtr root){var result=new StringBuilder();EnumChildWindows(root,(h,p)=>{var name=new StringBuilder(256);GetClassName(h,name,256);result.AppendLine(h.ToInt64()+" "+name);return true;},IntPtr.Zero);return result.ToString();}
 public static IntPtr Canvas(IntPtr root){return FindWindowEx(root,IntPtr.Zero,"NativePhotoFrameViewerCanvas",null);}
}
'@
Add-Type -AssemblyName System.Drawing
[ViewerCanvasProbe]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
$root=Split-Path (Split-Path $PSScriptRoot);$output=Join-Path $root ('native/artifacts/viewer-canvas-'+(Get-Date -Format yyyyMMdd-HHmmss));$data=Join-Path $output 'data';New-Item -ItemType Directory -Path $data -Force|Out-Null;$photo=Join-Path $output 'photo.bmp';$b=[Drawing.Bitmap]::new(720,480);$g=[Drawing.Graphics]::FromImage($b);$g.Clear([Drawing.Color]::Teal);$b.Save($photo);$g.Dispose();$b.Dispose()
@{global=@{paused=$false};playlists=@(@{id='viewer';name='大图交互';sources=@(@{type='file';path=$photo});config=@{transition='none';interval=30}})}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $data 'settings.json') -Encoding utf8
$before=@(Get-FrameTestTrayHistory);$binary=(Resolve-Path $Executable).Path;$main=[IntPtr]::Zero;$checks=[Collections.Generic.List[string]]::new();$process=Start-FrameTest $Executable -WindowStyle Hidden -PassThru -ArgumentList @('--data-dir',('"'+$data+'"'),'--test-hooks','--test-seconds','60')
function Message($h,$m,[long]$w=0,[long]$l=0){$value=[IntPtr]::Zero;if([ViewerCanvasProbe]::SendMessageTimeout($h,$m,[IntPtr]$w,[IntPtr]$l,2,2500,[ref]$value)-eq[IntPtr]::Zero){throw "窗口消息超时：$m"};$value.ToInt64()}
function Check($value,$name){if(!$value){throw "失败：$name"};$checks.Add($name)}
function Query($id){Message $main 32830 $id}
function XY($x,$y){([long]$y-shl16)-bor([long]$x-band65535)}
try{
 for($i=0;$i-lt60-and$main-eq[IntPtr]::Zero;$i++){Start-Sleep -Milliseconds 80;$main=[ViewerCanvasProbe]::Find($process.Id,'原生版')};Check ($main-ne[IntPtr]::Zero) '程序启动'
 for($i=0;$i-lt40-and(Query 23)-eq0;$i++){Start-Sleep -Milliseconds 80};Message $main 273 29|Out-Null;$viewer=[ViewerCanvasProbe]::Find($process.Id,'查看大图');Check ($viewer-ne[IntPtr]::Zero) '打开大图';$canvas=[ViewerCanvasProbe]::Canvas($viewer);if($canvas-eq[IntPtr]::Zero){[ViewerCanvasProbe]::Children($viewer)|Set-Content (Join-Path $output 'child-windows.txt') -Encoding utf8};Check ($canvas-ne[IntPtr]::Zero) '独立照片画布存在'
 for($i=0;$i-lt60-and(Query 44)-eq0;$i++){Start-Sleep -Milliseconds 80};Check ((Query 44)-eq1) '图片已解码';Check (((Query 7)-band4)-ne0) '大图临时暂停相框';Check ((Get-Content (Join-Path $data 'settings.json') -Raw|ConvertFrom-Json).global.paused-eq$false) '临时暂停不写入用户开关'
 $origin=[ViewerCanvasProbe+Point]::new();[ViewerCanvasProbe]::ClientToScreen($canvas,[ref]$origin)|Out-Null;Message $viewer 256 48|Out-Null;$fit=Query 40;Message $canvas 522 (120-shl16) (XY ($origin.x+200) ($origin.y+180))|Out-Null;Check ((Query 40)-gt$fit) '画布滚轮放大';Message $canvas 522 (-120-shl16) (XY ($origin.x+200) ($origin.y+180))|Out-Null;Check ([Math]::Abs((Query 40)-$fit)-le2) '画布滚轮缩小'
 Message $viewer 273 2|Out-Null;Check ((Query 40)-eq1000-and(Query 43)-eq0) '原始尺寸按钮'
 Message $canvas 513 1 (XY 100 100)|Out-Null;Message $canvas 512 1 (XY 130 125)|Out-Null;Message $canvas 514 0 (XY 130 125)|Out-Null;Check ((Query 41)-eq30000-and(Query 42)-eq25000) '画布拖动坐标映射正确'
 Message $viewer 256 48|Out-Null;Check ((Query 43)-eq1-and(Query 41)-eq0-and(Query 42)-eq0) '快捷键0恢复适应窗口'
 [ViewerCanvasProbe]::SetWindowPos($viewer,[IntPtr]::Zero,60,60,900,660,20)|Out-Null;$parent=[ViewerCanvasProbe+Rect]::new();$child=[ViewerCanvasProbe+Rect]::new();[ViewerCanvasProbe]::GetClientRect($viewer,[ref]$parent)|Out-Null;[ViewerCanvasProbe]::GetClientRect($canvas,[ref]$child)|Out-Null;Check ($child.right-eq$parent.right-and$child.bottom-eq$parent.bottom-44) '缩放后画布紧贴照片区域';Check ((Query 44)-eq1-and(Query 43)-eq1) '缩放保留图片和适应模式'
 Message $viewer 256 49|Out-Null;Check ((Query 40)-eq1000) '快捷键1恢复原始尺寸';Message $viewer 256 27|Out-Null;Check ([ViewerCanvasProbe]::Find($process.Id,'查看大图')-eq[IntPtr]::Zero) 'Esc关闭大图';Check (((Query 7)-band4)-eq0) '关闭大图恢复轮播'
 Message $main 273 29|Out-Null;Start-Sleep -Milliseconds 150;Message $main 273 16|Out-Null;Check ([ViewerCanvasProbe]::Find($process.Id,'查看大图')-eq[IntPtr]::Zero) '隐藏相框同时关闭大图';Message $main 273 17|Out-Null;Check ($process.WaitForExit(5000)-and$process.ExitCode-eq0) '正常退出'
 $after=@(Get-FrameTestTrayHistory);Check (@($after|Where-Object{$_.path-ieq$binary-and$_.key-notin@($before.key)}).Count-eq0) '无新增测试托盘记录';@{passed=$true;checks=$checks}|ConvertTo-Json -Depth 4|Set-Content (Join-Path $output 'summary.json') -Encoding utf8;Write-Output "$($checks.Count)项大图画布交互通过；$output"
}catch{@{passed=$false;error=$_.Exception.Message;checks=$checks}|ConvertTo-Json -Depth 4|Set-Content (Join-Path $output 'failure.json') -Encoding utf8;throw}finally{if(!$process.HasExited){[ViewerCanvasProbe]::PostMessage($main,273,[IntPtr]17,[IntPtr]::Zero)|Out-Null;if(!$process.WaitForExit(3000)){Stop-Process -Id $process.Id}}}

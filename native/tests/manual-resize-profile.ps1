param([string]$Executable='native/out/2.0.0-preview.18-delivery/DesktopPhotoFrameNative.exe',[ValidateRange(10,60)][int]$Seconds=25)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class ManualResizeProfile {
 public delegate bool Proc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")]public static extern bool EnumWindows(Proc f,IntPtr p);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,StringBuilder t,int n);
 [DllImport("user32.dll")]public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint timeout,out IntPtr result);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 public static IntPtr Find(int pid,string name){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);if(id==pid){var t=new StringBuilder(512);GetWindowText(h,t,512);if(t.ToString().Contains(name))found=h;}return true;},IntPtr.Zero);return found;}
}
'@
$root=Split-Path (Split-Path $PSScriptRoot)
$output=Join-Path $root ('native/artifacts/manual-resize-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$data=Join-Path $output 'data';New-Item -ItemType Directory -Path $data -Force|Out-Null
@{global=@{paused=$true;theme='dark'};playlists=@(@{id='manual';name='拖动测试（无照片）';sources=@();config=@{ratio='free';transition='none'}});settingsBounds=@{x=80;y=80;width=1050;height=740}}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $data 'settings.json') -Encoding utf8
$process=Start-FrameTest $Executable -PassThru -ArgumentList @('--data-dir',('"'+$data+'"'),'--test-hooks','--test-seconds','100')
$main=[IntPtr]::Zero
function Message($h,$m,[long]$w=0){$r=[IntPtr]::Zero;if([ManualResizeProfile]::SendMessageTimeout($h,$m,[IntPtr]$w,[IntPtr]::Zero,2,2000,[ref]$r)-eq[IntPtr]::Zero){throw "窗口消息超时：$m / $w"};$r.ToInt64()}
try{
 for($i=0;$i-lt60-and$main-eq[IntPtr]::Zero;$i++){Start-Sleep -Milliseconds 100;$main=[ManualResizeProfile]::Find($process.Id,'原生版')}
 if($main-eq[IntPtr]::Zero){throw '测试程序未启动'}
 Message $main 273 10|Out-Null
 $settings=[ManualResizeProfile]::Find($process.Id,'照片与设置');if($settings-eq[IntPtr]::Zero){throw '测试设置窗口未打开'}
 if((Message $main 32835)-ne1){throw '测试计时接口不可用'}
 Write-Output "测试设置窗口已打开，请拖动其右下角约 5 秒；$Seconds 秒后测试自动结束。"
 Start-Sleep -Seconds $Seconds
 $names=@('layout','redraw','preview','planning','position','visibility');$result=@{note='用户真实鼠标操作；仅记录程序累计处理耗时，无截图或持续采集';seconds=$Seconds}
 for($i=0;$i-lt$names.Count;$i++){$base=50+$i*3;$result[$names[$i]]=@{calls=(Message $main 32830 $base);totalUs=(Message $main 32830 ($base+1));maxUs=(Message $main 32830 ($base+2))}}
 $result|ConvertTo-Json -Depth 5|Set-Content (Join-Path $output 'summary.json') -Encoding utf8
 $result|ConvertTo-Json -Depth 5;Write-Output "报告：$output"
}finally{
 if(!$process.HasExited){if($main-ne[IntPtr]::Zero){[ManualResizeProfile]::PostMessage($main,273,[IntPtr]17,[IntPtr]::Zero)|Out-Null};if(!$process.WaitForExit(5000)){Stop-Process -Id $process.Id}}
}

param([string]$Executable='native/out/2.0.0-preview.20-delivery/DesktopPhotoFrameNative.exe',[int]$Cycles=13)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
. (Join-Path $PSScriptRoot 'frame-tray-history.ps1')
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class EnvelopeProbe {
 public delegate bool Callback(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int left,top,right,bottom;}
 [DllImport("user32.dll")]static extern bool EnumWindows(Callback f,IntPtr p);
 [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr h,out uint id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")]public static extern int GetSystemMetrics(int n);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",SetLastError=true)]static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint flags,uint timeout,out IntPtr result);
 public static IntPtr Find(int pid,string prefix){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);if(id==(uint)pid){var s=new StringBuilder(256);GetWindowText(h,s,s.Capacity);if(s.ToString().StartsWith(prefix))found=h;}return true;},IntPtr.Zero);return found;}
 public static long Message(IntPtr h,uint m,long w=0){IntPtr result;if(SendMessageTimeout(h,m,new IntPtr(w),IntPtr.Zero,2,1000,out result)==IntPtr.Zero)throw new Exception("回归窗口响应超时");return result.ToInt64();}
}
'@
[EnvelopeProbe]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
$root=Split-Path (Split-Path $PSScriptRoot);$output=Join-Path $root ('native/artifacts/composition-envelope-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$data=Join-Path $output 'data';$photos=Join-Path $output 'photos';New-Item -ItemType Directory -Path $data,$photos -Force|Out-Null
foreach($shape in @(@(960,640),@(640,960))){$b=[Drawing.Bitmap]::new($shape[0],$shape[1]);$g=[Drawing.Graphics]::FromImage($b);$g.Clear([Drawing.Color]::FromArgb(32,122,127));$b.Save((Join-Path $photos ($shape[0].ToString()+'.bmp')));$g.Dispose();$b.Dispose()}
@{activePlaylistId='envelope';playlists=@(@{id='envelope';name='画布包络回归';sources=@(@{type='folder';path=$photos});config=@{ratio='auto';frame='minimal';anchor='bottom-right';transition='fade';interval=300}});global=@{paused=$true;pauseFullscreen=$false};bounds=@{x=300;y=300;width=420;height=300}}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $data 'settings.json') -Encoding utf8
$beforeHistory=@(Get-FrameTestTrayHistory);$process=Start-FrameTest $Executable -ArgumentList @('--data-dir',('"'+$data+'"'),'--test-hooks','--test-seconds','180') -WindowStyle Hidden -PassThru
$main=[IntPtr]::Zero;$settings=[IntPtr]::Zero;$checks=[Collections.Generic.List[object]]::new()
function Q([int]$Index){[EnvelopeProbe]::Message($main,32830,$Index)}
function Command($Window,[int]$Id){[EnvelopeProbe]::Message($Window,273,$Id)|Out-Null}
function Wait-Idle {for($i=0;$i -lt 150;$i++){if((Q 23) -and !((Q 7)-band 48)){return};Start-Sleep -Milliseconds 10};throw '换图未在时限内完成'}
try{
 for($i=0;$i -lt 100 -and $main -eq [IntPtr]::Zero;$i++){Start-Sleep -Milliseconds 30;$main=[EnvelopeProbe]::Find($process.Id,'桌面相框 原生版')}
 if($main -eq [IntPtr]::Zero){throw '回归程序未启动'};Wait-Idle;Command $main 10;$settings=[EnvelopeProbe]::Find($process.Id,'桌面相框 2.0.0');if($settings -eq [IntPtr]::Zero){throw '设置窗口未打开'}
 $areaX=[EnvelopeProbe]::GetSystemMetrics(76);$areaY=[EnvelopeProbe]::GetSystemMetrics(77)
 for($anchor=0;$anchor -lt 4;$anchor++){
  Command $settings (420+$anchor)
  for($effect=0;$effect -lt 4;$effect++){
   [EnvelopeProbe]::Message([EnvelopeProbe]::GetDlgItem($settings,107),334,$effect)|Out-Null;Command $settings 24;Wait-Idle
   for($n=0;$n -lt $Cycles;$n++){
    $x=Q 3;$y=Q 4;$w=Q 1;$h=Q 2;$index=Q 0
    $fixedX=$x+$(if($anchor%2){$w}else{0});$fixedY=$y+$(if($anchor -ge 2){$h}else{0})
    Command $main 15;Wait-Idle;$x=Q 3;$y=Q 4;$w=Q 1;$h=Q 2
    $afterX=$x+$(if($anchor%2){$w}else{0});$afterY=$y+$(if($anchor -ge 2){$h}else{0})
    $r=[EnvelopeProbe+Rect]::new();[EnvelopeProbe]::GetWindowRect($main,[ref]$r)|Out-Null
    $covered=$r.left -le $areaX+$x -and $r.top -le $areaY+$y -and $r.right -ge $areaX+$x+$w -and $r.bottom -ge $areaY+$y+$h
    $bounded=(($r.right-$r.left)*($r.bottom-$r.top)) -le 4*$w*$h
    $shift=[Math]::Max([Math]::Abs($afterX-$fixedX),[Math]::Abs($afterY-$fixedY))
    $row=@{anchor=$anchor;effect=$effect;iteration=$n;fixedCornerShift=$shift;covered=$covered;bounded=$bounded;photoChanged=((Q 0) -ne $index)};$checks.Add($row)
    if($shift -gt 1 -or !$covered -or !$bounded -or !$row.photoChanged){throw '连续换图出现固定角或画布边界异常'}
   }
  }
  Write-Output "固定角 $anchor 完成：$($checks.Count) 次换图"
 }
 $afterHistory=@(Get-FrameTestTrayHistory);$new=@($afterHistory|Where-Object {$_.path -ieq $process.Path -and $_.key -notin @($beforeHistory.key)})
 if($new.Count){throw '回归新增托盘记录'}
 @{passed=$true;transitions=$checks.Count;checks=$checks;limits='真实窗口几何验证，不是呈现掉帧率或黑闪录像验收；未进行高频截图。'}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $output 'summary.json') -Encoding utf8
 Write-Output "连续换图包络通过：$($checks.Count) 次；$output"
}catch{@{passed=$false;error=$_.Exception.Message;checks=$checks}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $output 'failure.json') -Encoding utf8;throw}
finally{if(!$process.HasExited){if($main -ne [IntPtr]::Zero){[EnvelopeProbe]::PostMessage($main,273,[IntPtr]17,[IntPtr]::Zero)|Out-Null};if(!$process.WaitForExit(5000)){Stop-Process -Id $process.Id}}}

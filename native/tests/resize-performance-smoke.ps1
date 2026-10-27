param(
 [Parameter(Mandatory)][string]$CandidateExecutable,
 [string]$BaselineExecutable='native/out/2.0.0-preview.17-delivery/DesktopPhotoFrameNative.exe',
 [ValidateRange(1,10)][int]$Pairs=3,
 [ValidateSet('photos','appearance','playback','application')][string]$Page='photos',
 [ValidateSet('light','dark')][string]$Theme='dark',
 [ValidateRange(0,200)][int]$PhotoCount=24,
 [ValidateSet(30,60)][int]$RateHz=30,
 [switch]$NoResizeLifecycle,
 [switch]$ProfileLayout
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
. (Join-Path $PSScriptRoot 'frame-tray-history.ps1')
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class ResizePerformanceProbe {
 public delegate bool Proc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")]public static extern bool EnumWindows(Proc f,IntPtr p);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,StringBuilder text,int n);
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint flags,uint timeout,out IntPtr value);
 [DllImport("user32.dll",EntryPoint="SendMessageTimeoutW",CharSet=CharSet.Unicode)]static extern IntPtr SendText(IntPtr h,uint m,IntPtr w,StringBuilder text,uint flags,uint timeout,out IntPtr value);
 public static IntPtr Find(int pid,string part){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);if(id!=pid)return true;var t=new StringBuilder(512);GetWindowText(h,t,512);if(t.ToString().Contains(part))found=h;return true;},IntPtr.Zero);return found;}
 public static string ReadText(IntPtr h){var text=new StringBuilder(512);IntPtr value;if(SendText(h,13,new IntPtr(512),text,2,2000,out value)==IntPtr.Zero)throw new InvalidOperationException("无法读取窗口状态文字");return text.ToString();}
}
'@
Add-Type -AssemblyName System.Drawing
$root=Split-Path (Split-Path $PSScriptRoot)
function Resolve-Binary($path){$value=if([IO.Path]::IsPathRooted($path)){$path}else{Join-Path $root $path};return (Resolve-Path $value).Path}
$candidate=Resolve-Binary $CandidateExecutable
$baseline=if($BaselineExecutable){Resolve-Binary $BaselineExecutable}else{$null}
$paths=@($candidate);if($baseline){$paths+=@($baseline)}
$output=Join-Path $root ('native/artifacts/resize-performance-'+(Get-Date -Format yyyyMMdd-HHmmss-fff))
$photos=Join-Path $output 'photos';New-Item -ItemType Directory -Path $photos -Force|Out-Null
$witness=Join-Path $root 'native/out/resize-performance-witness/ResizePerformanceWitness.exe'
$sources=@('resize-performance-witness.cpp','wgc-source.h')|ForEach-Object{Get-Item (Join-Path $PSScriptRoot $_)}
if(!(Test-Path $witness)-or(Get-Item $witness).LastWriteTimeUtc-lt($sources|Sort-Object LastWriteTimeUtc -Descending|Select-Object -First 1).LastWriteTimeUtc){
 New-Item -ItemType Directory -Path (Split-Path $witness) -Force|Out-Null
 & (Join-Path $root 'native/toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/clang++.exe') -std=c++20 -O2 -municode -mwindows -static -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 (Join-Path $PSScriptRoot 'resize-performance-witness.cpp') -o $witness -ld3d11 -lole32 -lshell32 -luuid -luser32 -lgdi32 -lruntimeobject
 if($LASTEXITCODE-ne0){throw '性能采集工具构建失败'}
}
# 测试照片只在工作区生成；所有重复运行复用同一套合成图，不读取用户相册。
for($i=0;$i-lt$PhotoCount;$i++){
 $b=[Drawing.Bitmap]::new(720,480);$g=[Drawing.Graphics]::FromImage($b);$g.Clear([Drawing.Color]::FromArgb(20,190,155));$g.FillRectangle([Drawing.Brushes]::DarkSlateGray,80,90,160,110);$g.FillEllipse([Drawing.Brushes]::LightGoldenrodYellow,360,120,120,120);$b.Save((Join-Path $photos ('photo-{0:000}.bmp' -f $i)));$g.Dispose();$b.Dispose()
}
function Message($h,$m,[long]$w=0,[long]$l=0){$value=[IntPtr]::Zero;if([ResizePerformanceProbe]::SendMessageTimeout($h,$m,[IntPtr]$w,[IntPtr]$l,2,2500,[ref]$value)-eq[IntPtr]::Zero){throw "窗口消息超时：$m / $w"};$value.ToInt64()}
function Percentile($items,[double]$fraction){$sorted=@($items|Sort-Object);if(!$sorted.Count){return $null};$rank=($sorted.Count-1)*$fraction;$left=[int][Math]::Floor($rank);$right=[int][Math]::Ceiling($rank);return [Math]::Round([double]$sorted[$left]+([double]$sorted[$right]-[double]$sorted[$left])*($rank-$left),3)}
function Read-Measurement($directory,$label,$pair,$executable,$exitCode){
 $resultPath=Join-Path $directory 'capture.json';if(!(Test-Path $resultPath)){$errorPath=Join-Path $directory 'error.txt';$detail=if(Test-Path $errorPath){Get-Content $errorPath -Raw}else{'没有采集报告'};throw "窗口采集失败：$detail"}
 $c=Get-Content $resultPath -Raw|ConvertFrom-Json
 if($exitCode-ne0-or!$c.valid){throw "窗口采集未完成：$directory；退出码 $exitCode"}
 $freq=[double]$c.qpcFrequency;$activeStart=[double]$c.activeStartQpc*10000000/$freq;$activeEnd=[double]$c.activeEndQpc*10000000/$freq
 $allFrames=@(Import-Csv (Join-Path $directory 'frames.csv'));$active=@($allFrames|Where-Object{[double]$_.time100ns-ge$activeStart-and[double]$_.time100ns-le$activeEnd})
 if($active.Count-lt30){throw "有效活动帧不足，或 WGC 与 QPC 时间域无法对齐：$directory"}
 $gaps=[Collections.Generic.List[double]]::new();$ages=[Collections.Generic.List[double]]::new();$previousSize=$null;$lastChange=$null;$lastStamp=[double]0;$changes=0;$duplicates=0;$outOfOrder=0
 foreach($f in $active){
  $stamp=[double]$f.time100ns;if($stamp-lt$lastStamp){$outOfOrder++;continue};if($stamp-eq$lastStamp){$duplicates++;continue};$lastStamp=$stamp
  $age=(([double]$f.observed_qpc*10000000/$freq)-$stamp)/10000;if($age-ge0-and$age-lt1000){$ages.Add($age)}
  $size=('{0},{1}' -f $f.width,$f.height);if($null-eq$previousSize){$previousSize=$size;continue}
  if($size-ne$previousSize){if($null-ne$lastChange){$gaps.Add(($stamp-$lastChange)/10000)};$lastChange=$stamp;$previousSize=$size;$changes++}
 }
 if($gaps.Count-lt10){throw "可用的画面尺寸变化不足：$directory"}
 $slots=@(Import-Csv (Join-Path $directory 'slots.csv'));$completed=@($slots|Where-Object{[double]$_.applied_qpc-gt0});$latencies=@($completed|ForEach-Object{([double]$_.applied_qpc-[double]$_.scheduled_qpc)*1000/$freq});$submitLags=@($slots|Where-Object{[double]$_.issued_qpc-gt0}|ForEach-Object{([double]$_.issued_qpc-[double]$_.scheduled_qpc)*1000/$freq})
 $last=$slots[-1];$finalLag=([double]$last.applied_qpc-[double]$last.scheduled_qpc)*1000/$freq
 return [pscustomobject]@{label=$label;pair=$pair;executable=$executable;executableSha256=(Get-FileHash $executable).Hash;windowDpi=$c.windowDpi;initialWidth=$c.initialWidth;initialHeight=$c.initialHeight;minimumWidth=$c.minimumWidth;minimumHeight=$c.minimumHeight;spanWidth=$c.spanWidth;spanHeight=$c.spanHeight;workWidth=$c.workWidth;workHeight=$c.workHeight;simulatedLifecycle=$c.simulatedLifecycle;enteredQpc=$c.enteredQpc;exitedQpc=$c.exitedQpc;plannedSlots=$c.plannedSlots;issuedSlots=$c.issuedSlots;appliedSlots=$c.appliedSlots;supersededSlots=$c.supersededSlots;supersededPercent=[Math]::Round(100*$c.supersededSlots/[Math]::Max(1,$c.plannedSlots),2);lastTargetApplied=$c.lastTargetApplied;lastTargetAckLagMs=[Math]::Round($finalLag,3);submissionLagMedianMs=(Percentile $submitLags .5);submissionLagP95Ms=(Percentile $submitLags .95);ackLagMedianMs=(Percentile $latencies .5);ackLagP95Ms=(Percentile $latencies .95);capturedFrames=$allFrames.Count;activeFrames=$active.Count;changedSizeSamples=$changes;sizeChangeGapMedianMs=(Percentile $gaps .5);sizeChangeGapP95Ms=(Percentile $gaps .95);sizeChangeGapsOver50ms=@($gaps|Where-Object{$_-gt50}).Count;sizeChangeGapMaxMs=(Percentile $gaps 1);captureAgeMedianMs=(Percentile $ages .5);captureAgeP95Ms=(Percentile $ages .95);captureAgeSamples=$ages.Count;outOfOrderFrames=$outOfOrder;duplicateTimestamps=$duplicates;directory=$directory}
}
function Invoke-Measurement($executable,$label,$pair){
 $directory=Join-Path $output (('{0:00}-{1}' -f $pair,$label));$data=Join-Path $directory 'data';$capture=Join-Path $directory 'capture';New-Item -ItemType Directory -Path $data -Force|Out-Null
 @{global=@{paused=$true;theme=$Theme};playlists=@(@{id='performance';name='缩放性能测试';sources=@(@{type='folder';path=$photos});config=@{ratio='free';transition='none'}});settingsBounds=@{width=1280;height=800}}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $data 'settings.json') -Encoding utf8
 $appArguments=@('--data-dir',('"'+$data+'"'));if($ProfileLayout){$appArguments+='--test-hooks'}
 $process=Start-FrameTest $executable -WindowStyle Hidden -PassThru -ArgumentList $appArguments;$main=[IntPtr]::Zero
 try{
  for($i=0;$i-lt70-and$main-eq[IntPtr]::Zero;$i++){Start-Sleep -Milliseconds 80;$main=[ResizePerformanceProbe]::Find($process.Id,'原生版')};if($main-eq[IntPtr]::Zero){throw '相框未启动'}
  Message $main 273 10|Out-Null;$settings=[ResizePerformanceProbe]::Find($process.Id,'照片与设置');if($settings-eq[IntPtr]::Zero){throw '设置窗口未打开'}
  $pages=@{photos=400;appearance=401;playback=402;application=403};Message $settings 273 $pages[$Page]|Out-Null
  # 通过可见状态文字等待目录扫描；额外固定暖机三秒，不依赖应用测试接口。
  if($PhotoCount-gt0){$loaded=$false;for($i=0;$i-lt50-and!$loaded;$i++){Start-Sleep -Milliseconds 200;$status=[ResizePerformanceProbe]::ReadText([ResizePerformanceProbe]::GetDlgItem($settings,200));$loaded=$status.StartsWith(('{0} 张可播放' -f $PhotoCount),[StringComparison]::Ordinal)};if(!$loaded){throw '合成照片目录扫描未完成'}}
  Start-Sleep -Seconds 3
  $profileSupported=$false;if($ProfileLayout){$profileSupported=(Message $main 32835)-eq1}
  $helperArguments=@($settings.ToInt64(),('"'+$capture+'"'),("--rate=$RateHz"));if($NoResizeLifecycle){$helperArguments+='--no-resize-lifecycle'}
  $helper=Start-Process $witness -WindowStyle Hidden -PassThru -ArgumentList $helperArguments
  if(!$helper.WaitForExit(18000)){Stop-Process -Id $helper.Id;throw '窗口性能采集超时'}
  $result=Read-Measurement $capture $label $pair $executable $helper.ExitCode
  if($ProfileLayout){if($profileSupported){$raw=[ordered]@{};foreach($index in 50..67){$raw["$index"]=Message $main 32830 $index};$profile=@{supported=$true;layoutCount=$raw['50'];layoutTotalUs=$raw['51'];layoutMaxUs=$raw['52'];redrawCount=$raw['53'];redrawTotalUs=$raw['54'];redrawMaxUs=$raw['55'];previewCount=$raw['56'];previewTotalUs=$raw['57'];previewMaxUs=$raw['58'];planningCount=$raw['59'];planningTotalUs=$raw['60'];planningMaxUs=$raw['61'];batchCount=$raw['62'];batchTotalUs=$raw['63'];batchMaxUs=$raw['64'];visibilityZCount=$raw['65'];visibilityZTotalUs=$raw['66'];visibilityZMaxUs=$raw['67'];rawQueries=$raw;limitations='重置发生在启动采集器前，统计包含初始设定和采集尾段；布局、重绘、预览等耗时有嵌套，不能相加。'} }else{$profile=@{supported=$false;reason='此版本未实现测试计时重置接口，仅保留 WGC 结果'}};$result|Add-Member -NotePropertyName profile -NotePropertyValue $profile}
  Message $main 273 17|Out-Null;if(!$process.WaitForExit(5000)-or$process.ExitCode-ne0){throw '相框未正常退出'}
  return $result
 }finally{if(!$process.HasExited){if($main-ne[IntPtr]::Zero){[ResizePerformanceProbe]::PostMessage($main,273,[IntPtr]17,[IntPtr]::Zero)|Out-Null};if(!$process.WaitForExit(3000)){Stop-Process -Id $process.Id}}}
}
$before=@(Get-FrameTestTrayHistory);$results=[Collections.Generic.List[object]]::new();$failure=$null;$added=0
try{
 for($pair=1;$pair-le$Pairs;$pair++){
  $sequence=if(!$baseline){@('candidate')}elseif($pair%2){@('baseline','candidate')}else{@('candidate','baseline')}
  foreach($label in $sequence){Write-Output "第 $pair 轮：$label，$Page 页面";$path=if($label-eq'baseline'){$baseline}else{$candidate};$results.Add((Invoke-Measurement $path $label $pair))}
 }
}catch{$failure=$_.Exception.Message}finally{
 $after=@(Get-FrameTestTrayHistory);$added=@($after|Where-Object{$_.path-in$paths-and$_.key-notin@($before.key)}).Count
 $baselineRuns=@($results|Where-Object label -eq 'baseline');$candidateRuns=@($results|Where-Object label -eq 'candidate')
 $comparison=$null;if($baselineRuns.Count-and$candidateRuns.Count){$b=Percentile @($baselineRuns.sizeChangeGapMedianMs) .5;$n=Percentile @($candidateRuns.sizeChangeGapMedianMs) .5;$comparison=@{baselineMedianOfMediansMs=$b;candidateMedianOfMediansMs=$n;medianGapChangePercent=if($b-gt0){[Math]::Round(100*($n-$b)/$b,2)}else{$null};baselineMedianOfP95Ms=(Percentile @($baselineRuns.sizeChangeGapP95Ms) .5);candidateMedianOfP95Ms=(Percentile @($candidateRuns.sizeChangeGapP95Ms) .5);baselineSupersededPercent=(Percentile @($baselineRuns.supersededPercent) .5);candidateSupersededPercent=(Percentile @($candidateRuns.supersededPercent) .5)}}
 $report=@{passed=(!$failure-and$added-eq0);failure=$failure;page=$Page;theme=$Theme;photoCount=$PhotoCount;requestedRateHz=$RateHz;warmupSeconds=3;appTestHooks=[bool]$ProfileLayout;simulatedResizeLifecycle=(!$NoResizeLifecycle);realSystemDpiChanged=$false;source='Windows Graphics Capture 窗口帧元数据';limitations=@('WGC 是窗口合成采样，不是物理显示器呈现统计；同尺寸的重复帧不计入几何变化间隔。','固定频率轨迹只保留一个待应答目标，过时目标会合并；合并率不是显示掉帧率。','默认只模拟进入/退出缩放消息，SetWindowPos 不能完整模拟系统模态鼠标拖拽；WM_NULL 可能重入，窗口矩形抵达和应答不等于照片完成绘制。','固定三秒暖机不保证缩略图缓存全部完成；同一机器同条件成对比较。','只采集元数据，不验证像素闪烁；请另运行 resize-paint-smoke.ps1。','WGC 与硬件/桌面负载可能影响样本，比较仅适用于相同设备与测试条件。','ProfileLayout 开关会给支持该接口的版本添加计时开销；正式性能对比请关闭开关，分别启用它定位热点。');results=$results;comparison=$comparison;newTestTrayRecords=$added}
 $report|ConvertTo-Json -Depth 8|Set-Content (Join-Path $output 'summary.json') -Encoding utf8
 Write-Output ($report|ConvertTo-Json -Depth 5)
 Write-Output "性能报告：$output"
}
if($failure){throw $failure};if($added-ne0){throw '出现测试托盘记录'}

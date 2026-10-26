param([string]$Executable='native/out/2.0.0-preview.15-delivery/DesktopPhotoFrameNative.exe')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot)
$artifactRoot=Join-Path $root 'native/artifacts'
$cases=@()
foreach($ratio in @('auto','1:1','free','16:9','9:16')){foreach($handle in @('top-left','top-right','bottom-left','bottom-right')){$cases+=@{ratio=$ratio;handle=$handle;queued=$false}}}
foreach($handle in @('left','right','top','bottom')){$cases+=@{ratio='free';handle=$handle;queued=$false}}
foreach($handle in @('top-left','top-right','bottom-left','bottom-right')){$cases+=@{ratio='free';handle=$handle;queued=$true}}
$results=@()
# 鼠标是桌面共享资源，所有交互用例顺序执行。
foreach($case in $cases){
 $arguments=@('-NoProfile','-File',(Join-Path $PSScriptRoot 'resize-continuity-smoke.ps1'),'-Executable',$Executable,'-Ratio',$case.ratio,'-Handle',$case.handle)
 if($case.queued){$arguments+='-ReplayQueuedMove'}
 & pwsh @arguments
 $code=$LASTEXITCODE
 $latest=Get-ChildItem -LiteralPath $artifactRoot -Directory -Filter 'resize-continuity-*'|Sort-Object LastWriteTime -Descending|Select-Object -First 1
 $record=Get-Content (Join-Path $latest.FullName 'samples.json') -Raw|ConvertFrom-Json
 $results+=@{ratio=$case.ratio;handle=$case.handle;queued=$case.queued;passed=($code -eq 0);maxJump=$record.maxJump;queuedError=$record.queuedError;anchorErrorX=$record.anchorErrorX;anchorErrorY=$record.anchorErrorY;record=$latest.FullName}
}
$output=Join-Path $artifactRoot ('resize-matrix-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'.json')
@{passed=@($results|Where-Object passed).Count;total=$cases.Count;executable=$Executable;cases=$results}|ConvertTo-Json -Depth 6|Set-Content $output -Encoding utf8
Write-Output $output
if(@($results|Where-Object {!$_.passed}).Count){throw '缩放组合检查存在失败项'}

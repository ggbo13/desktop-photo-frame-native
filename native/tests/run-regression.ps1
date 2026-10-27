param([switch]$IncludeInstaller,[switch]$AllowCapture,[string]$Executable='native/out/2.0.0-preview.19-delivery/DesktopPhotoFrameNative.exe')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot)
$workspacePrefix=[IO.Path]::GetFullPath($root).TrimEnd('\')+'\'
. (Join-Path $PSScriptRoot 'frame-tray-history.ps1')
function Get-TestTrayHistory {
 return @(Get-FrameTestTrayHistory | Where-Object {$_.path.StartsWith($workspacePrefix,[StringComparison]::OrdinalIgnoreCase) -and $_.path -match 'DesktopPhotoFrameNative\.exe$|桌面相框\.exe$'})
}
$before=@(Get-TestTrayHistory)
$scripts=@('test-runtime-smoke.ps1','tray-isolation-smoke.ps1','composition-idle-smoke.ps1','settings-resize-smoke.ps1','viewer-canvas-smoke.ps1','viewer-doubleclick-smoke.ps1','settings-theme-smoke.ps1','settings-review-smoke.ps1','preview8-smoke.ps1','tray-controls-smoke.ps1','device-recovery-smoke.ps1')
if($AllowCapture){$scripts+='resize-paint-smoke.ps1'}else{Write-Output '已跳过高负载窗口采集；本次回归不验证采集画面闪烁。'}
if($IncludeInstaller){$scripts+='installer-smoke.ps1'}
$results=@()
foreach($script in $scripts){$arguments=@('-NoProfile','-File',(Join-Path $PSScriptRoot $script));if($script -ne 'installer-smoke.ps1'){$arguments+=@('-Executable',$Executable)};if($script -eq 'resize-paint-smoke.ps1'){$arguments+='-AllowCapture'};& pwsh @arguments;$results+=@{script=$script;exitCode=$LASTEXITCODE;passed=($LASTEXITCODE -eq 0)}}
$after=@(Get-TestTrayHistory)
$output=Join-Path $root ('native/artifacts/regression-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'.json')
@{results=$results;testHistoryBefore=$before;testHistoryAfter=$after;testHistoryCountBefore=$before.Count;testHistoryCountAfter=$after.Count}|ConvertTo-Json -Depth 6|Set-Content $output -Encoding utf8
Write-Output $output
if(@($results|Where-Object {!$_.passed}).Count -or $after.Count -ne 0){throw '回归检查失败，或测试留下了托盘历史'}
Write-Output "回归通过：$($results.Count)组脚本，测试托盘历史保持 $($before.Count) -> $($after.Count)"

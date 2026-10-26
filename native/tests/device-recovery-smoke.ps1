param([string]$Executable='native/out/2.0.0-preview.14-delivery/DesktopPhotoFrameNative.exe')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
$binary=[IO.Path]::GetFullPath((Join-Path (Get-Location) $Executable))
$root=Join-Path (Get-Location) ('native/artifacts/device-recovery-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $root -Force | Out-Null

function Check([bool]$condition,[string]$name){if(!$condition){throw "失败：$name"};Write-Output "通过：$name"}
function New-TestData([string]$name){
 $path=Join-Path $root $name
 New-Item -ItemType Directory -Path $path -Force | Out-Null
 @{unknownFixture='preserve-me';global=@{paused=$true}} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $path 'settings.json') -Encoding utf8
 return $path
}

$hardware=New-TestData 'hardware'
$first=Start-FrameTest -FilePath $binary -ArgumentList @('--data-dir',('"'+$hardware+'"'),'--simulate-device-loss','--test-seconds','4') -WindowStyle Hidden -PassThru
Check ($first.WaitForExit(10000)) '首次设备失效后进程及时退出'
Check ($first.ExitCode -eq 1) '设备失效返回非零状态'
$hardwareLog=Join-Path $hardware 'graphics-recovery.log'
for($i=0;$i -lt 100 -and (!(Test-Path $hardwareLog) -or (Get-Content $hardwareLog -Raw) -notmatch 'software renderer started');$i++){Start-Sleep -Milliseconds 100}
$lines=@(Get-Content $hardwareLog)
Check (@($lines | Where-Object {$_ -match 'failure hr=0x887a0005'}).Count -eq 1) '错误仅处理一次'
Check (@($lines | Where-Object {$_ -eq 'software renderer started'}).Count -eq 1) '软件渲染仅重启一次'
$state=Get-Content (Join-Path $hardware 'settings.json') -Raw | ConvertFrom-Json
Check ($state.unknownFixture -eq 'preserve-me' -and $state.global.paused) '旧配置和未知字段保留'

$software=New-TestData 'software'
$second=Start-FrameTest -FilePath $binary -ArgumentList @('--data-dir',('"'+$software+'"'),'--software-renderer','--simulate-device-loss','--test-seconds','4') -WindowStyle Hidden -PassThru
Check ($second.WaitForExit(10000)) '软件渲染再次失败后及时退出'
Check ($second.ExitCode -eq 1) '软件渲染故障返回非零状态'
$lines=@(Get-Content (Join-Path $software 'graphics-recovery.log'))
Check (@($lines | Where-Object {$_ -match 'failure hr=0x887a0005'}).Count -eq 1) '软件渲染故障不循环重启'
Write-Output "证据目录：$root"

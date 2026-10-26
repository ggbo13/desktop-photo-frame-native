param([string]$Executable='native/out/2.0.0-preview.16-delivery/DesktopPhotoFrameNative.exe')
$ErrorActionPreference='Stop'
$helper=Join-Path $PSScriptRoot 'frame-test-runtime.ps1'
if(!(Test-Path -LiteralPath $helper)){throw '测试启动器尚未提供无托盘保护'}
. $helper
$blocked=$false
# 验证同一版本门槛，无需分发或运行会登记托盘的旧版程序。
try{Assert-FrameTestVersion -Version ([Version]'2.0.0.12')}catch{if($_.Exception.Message -notmatch '无托盘隔离'){throw};$blocked=$true}
if(!$blocked){throw '旧版本号没有被测试启动器阻止'}
$output=Join-Path (Split-Path (Split-Path $PSScriptRoot)) ('native/artifacts/test-runtime-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $output -Force|Out-Null
$absoluteBinary=[IO.Path]::GetFullPath((Join-Path (Split-Path (Split-Path $PSScriptRoot)) $Executable))
$process=Start-FrameTest -FilePath $absoluteBinary -ArgumentList @('--data-dir',('"'+$output+'"'),'--test-seconds','1') -PassThru -WindowStyle Hidden
if(!$process.WaitForExit(10000) -or $process.ExitCode -ne 0){throw '隔离启动器运行新版失败'}
@{oldBinaryBlocked=$blocked;newBinaryExitCode=$process.ExitCode;executable=$Executable}|ConvertTo-Json|Set-Content (Join-Path $output 'summary.json') -Encoding utf8
Write-Output "测试启动器验证通过：旧版被拦截，新版隔离运行；$output"

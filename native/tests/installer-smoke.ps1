param([string]$Installer='native/dist/2.0.0-preview.20/桌面相框 原生版 2.0.0-preview.20 Setup.exe',[string]$ExpectedVersion='2.0.0-preview.20')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
$root=Split-Path (Split-Path $PSScriptRoot)
$output=Join-Path $root ('native/artifacts/install-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$destination=Join-Path $output 'installed-app'
New-Item -ItemType Directory -Path $output -Force | Out-Null
# 测试开关跳过桌面快捷方式及系统卸载项，仅在独立目录验证安装内容。
$setup=Start-Process -FilePath $Installer -ArgumentList @('/S','/TEST',('/D='+$destination)) -WindowStyle Hidden -PassThru
if(!$setup.WaitForExit(30000)){throw '安装测试超时'}
if($setup.ExitCode -ne 0){throw "安装退出码 $($setup.ExitCode)"}
$exe=Join-Path $destination 'DesktopPhotoFrameNative.exe'
if(!(Test-Path $exe)){throw '安装后缺少主程序'}
if((Get-Item $exe).VersionInfo.ProductVersion -ne $ExpectedVersion){throw '安装后的版本资源不匹配'}
$source=Join-Path (Split-Path (Resolve-Path $Installer)) 'app/DesktopPhotoFrameNative.exe'
if((Get-FileHash $exe).Hash -ne (Get-FileHash $source).Hash){throw '安装前后主程序校验不一致'}
$licenseCount=@(Get-ChildItem (Join-Path $destination 'licenses') -File).Count
if($licenseCount -lt 7){throw '第三方许可证不完整'}
$app=Start-FrameTest -FilePath $exe -ArgumentList @('--data-dir',('"'+$output+'\data"'),'--test-seconds','8') -WindowStyle Hidden -PassThru
if(!$app.WaitForExit(20000)){throw '安装后启动验证超时'}
if($app.ExitCode -ne 0){throw "安装后的程序退出码 $($app.ExitCode)"}
@{installer=(Resolve-Path $Installer).Path;installedExe=$exe;exitCode=$app.ExitCode;licenseFiles=$licenseCount;fileVersion=(Get-Item $exe).VersionInfo.ProductVersion;sha256=(Get-FileHash $exe).Hash;checks=@('静默测试安装','主程序内容一致','第三方许可证齐全','安装后启动及正常退出','测试数据独立保存')} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $output 'summary.json')
Write-Output "安装与启动验证通过；$output"

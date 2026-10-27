param([string]$ApplicationDirectory='native/out/2.0.0-preview.17-delivery',[string]$OutputDirectory='native/dist/2.0.0-preview.17',[string]$MakensisExecutable='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$source=[IO.Path]::GetFullPath((Join-Path $root $ApplicationDirectory))
$output=[IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))
$stage=Join-Path $output 'app'
$licenses=Join-Path $stage 'licenses'
New-Item -ItemType Directory -Path $stage,$licenses -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'DesktopPhotoFrameNative.exe') -Destination $stage
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'package/使用说明.txt') -Destination $stage
$thirdParty=Join-Path $PSScriptRoot 'vendor/codecs'
Copy-Item -LiteralPath (Join-Path $thirdParty 'libwebp-1.6.0/COPYING') -Destination (Join-Path $licenses 'webp-COPYING.txt')
Copy-Item -LiteralPath (Join-Path $thirdParty 'libwebp-1.6.0/PATENTS') -Destination (Join-Path $licenses 'webp-PATENTS.txt')
Copy-Item -LiteralPath (Join-Path $thirdParty 'libavif-1.4.2/LICENSE') -Destination (Join-Path $licenses 'avif-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'vendor/libyuv-LICENSE') -Destination (Join-Path $licenses 'libyuv-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $thirdParty 'dav1d-1.5.4/COPYING') -Destination (Join-Path $licenses 'dav1d-COPYING.txt')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'vendor/json-LICENSE.MIT') -Destination (Join-Path $licenses 'json-MIT.txt')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'toolchain/llvm-mingw-20260908-ucrt-x86_64/LICENSE.TXT') -Destination (Join-Path $licenses 'llvm-mingw-LICENSE.txt')
$compiler=if($MakensisExecutable){(Get-Command $MakensisExecutable -ErrorAction Stop).Source}else{Join-Path $PSScriptRoot 'toolchain/nsis-3.0.4.1-portable/Bin/makensis.exe'}
$package=Join-Path $output '桌面相框 原生版 2.0.0-preview.17 Setup.exe'
Push-Location (Join-Path $PSScriptRoot 'package')
try { & $compiler /V2 /INPUTCHARSET UTF8 "/DAPP_DIR=$stage" "/DPACKAGE_OUTPUT=$package" installer.nsi; if($LASTEXITCODE -ne 0){throw '安装包构建失败'} } finally {Pop-Location}
Get-Item -LiteralPath $package | Select-Object FullName,Length
Get-FileHash -LiteralPath $package -Algorithm SHA256 | Format-List

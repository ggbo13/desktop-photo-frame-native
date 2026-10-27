param([string]$SourceDirectory='native/src')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot)
$output=Join-Path $root 'native/out/scroll-math-smoke'
New-Item -ItemType Directory -Path $output -Force|Out-Null
$compiler=Join-Path $root 'native/toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/clang++.exe'
$program=Join-Path $output 'ScrollMathSmoke.exe'
& $compiler -std=c++20 -O2 -static '-I' (Join-Path $root $SourceDirectory) (Join-Path $PSScriptRoot 'scroll-math-smoke.cpp') '-o' $program
if($LASTEXITCODE-ne0){throw '滚动端点测试构建失败'}
& $program
if($LASTEXITCODE-ne0){throw '滚动端点计算不正确'}
Write-Output '滚动条端点、中心及负坐标检查通过'

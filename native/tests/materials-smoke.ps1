param()
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot)
$output=Join-Path $root ('native/artifacts/materials-'+(Get-Date -Format yyyyMMdd-HHmmss))
New-Item -ItemType Directory -Path $output -Force|Out-Null
$compiler=Join-Path $root 'native/toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/clang++.exe'
$executable=Join-Path $output 'materials-smoke.exe'
& $compiler -std=c++20 -O2 -static (Join-Path $PSScriptRoot 'materials-smoke.cpp') -o $executable
if($LASTEXITCODE-ne0){throw '材质测试构建失败'}
& $executable
if($LASTEXITCODE-ne0){throw '材质测试失败'}
@{passed=$true;checks=@('外沿修饰','中性纸白','五种材质像素格式与不透明度')}|ConvertTo-Json|Set-Content (Join-Path $output 'summary.json') -Encoding utf8
Write-Output $output

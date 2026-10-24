param([string]$OutputDirectory='native/out/native-preview',[switch]$Release)
$ErrorActionPreference='Stop'
$compiler=Join-Path $PSScriptRoot 'toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/clang++.exe'
$codecs=Join-Path $PSScriptRoot 'out/codecs/install'
if(!(Test-Path (Join-Path $codecs 'lib/libavif.a'))){throw '请先运行 native/build-codecs.ps1 构建随附解码库'}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$resources=Join-Path $OutputDirectory 'application-resource.o'
& (Join-Path (Split-Path $compiler) 'llvm-windres.exe') -c 65001 '-I' (Join-Path $PSScriptRoot 'package') (Join-Path $PSScriptRoot 'package/application.rc') '-O' 'coff' '-o' $resources
if($LASTEXITCODE -ne 0){throw '窗口资源构建失败'}
# 公开安装包不携带调试符号及本机源码路径，日常开发构建仍保留调试信息。
$debugFlags=if($Release){@('-g0','-s',('-ffile-prefix-map='+(Split-Path $PSScriptRoot)+'=.'))}else{@('-g')}
& $compiler -std=c++20 -O2 @debugFlags -municode -mwindows -static -DUNICODE -D_UNICODE -DPHOTO_FRAME_CODECS -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 "-I$codecs/include" (Join-Path $PSScriptRoot 'src/application.cpp') $resources -o (Join-Path $OutputDirectory 'DesktopPhotoFrameNative.exe') "-L$codecs/lib" -lwebpdemux -lwebp -lsharpyuv -lavif -ldav1d -lpthread -ld3d11 -ld2d1 -ldcomp -ldwmapi -lwindowscodecs -lole32 -lshell32 -luuid -lcomdlg32 -lpsapi -lgdi32 -luser32 -lcomctl32 -luxtheme -ladvapi32 -lwtsapi32 -lmsimg32
if($LASTEXITCODE -ne 0){throw '原生应用构建失败'}
Write-Output (Join-Path $OutputDirectory 'DesktopPhotoFrameNative.exe')

param([string]$OutputDirectory='')
$ErrorActionPreference = 'Stop'
$nativeRoot = $PSScriptRoot
$compiler = Join-Path $nativeRoot 'toolchain\llvm-mingw-20260908-ucrt-x86_64\bin\clang++.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw '请先准备 native/toolchain 内的 LLVM-MinGW 20260908 工具链。' }
if (!$OutputDirectory) { $OutputDirectory=Join-Path $nativeRoot 'out' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
& $compiler -std=c++20 -O2 -g -municode -mwindows -static -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 (Join-Path $nativeRoot 'src\prototype.cpp') -o (Join-Path $OutputDirectory 'PhotoFramePrototype.exe') -ld3d11 -ld2d1 -ldcomp -ldwmapi -lwindowscodecs -lole32 -lshell32 -luuid -lcomdlg32 -lpsapi -lgdi32 -luser32
if ($LASTEXITCODE -ne 0) { throw '原生样机构建失败' }
& $compiler -std=c++20 -O2 -municode -mwindows -static -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 (Join-Path $nativeRoot 'tests\capture.cpp') -o (Join-Path $OutputDirectory 'CaptureWitness.exe') -ld3d11 -lwindowscodecs -lole32 -lshell32 -luuid -luser32 -lgdi32 -lruntimeobject -lmfplat -lmfreadwrite -lmfuuid
if ($LASTEXITCODE -ne 0) { throw '呈现采样工具构建失败' }
& $compiler -std=c++20 -O2 -municode -static (Join-Path $nativeRoot 'tests\video-inspect.cpp') -o (Join-Path $OutputDirectory 'VideoInspect.exe') -lmfplat -lmfreadwrite -lmfuuid -lole32 -luuid
if ($LASTEXITCODE -ne 0) { throw '录像检查工具构建失败' }

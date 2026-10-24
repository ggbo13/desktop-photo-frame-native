param([string]$OutputDirectory='native/out/codecs',[string]$PythonExecutable='python')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot
$output=[IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))
$tools=Join-Path $PSScriptRoot 'toolchain/build-python-20260923'
$compiler=Join-Path $PSScriptRoot 'toolchain/llvm-mingw-20260908-ucrt-x86_64/bin'
$python=(Get-Command $PythonExecutable -ErrorAction Stop).Source
$cmake=Join-Path $tools 'cmake/data/bin/cmake.exe'
$ninja=Join-Path $tools 'bin/ninja.exe'
if(!(Test-Path $cmake) -or !(Test-Path $ninja)){throw '请先按 README 安装固定版本的 Python 构建依赖'}
$archives=@(
 @{name='dav1d-1.5.4';sha256='A1D5B63D2D38EC9BD03ACF643CAA51FA22EDD1E89C5A109C4807717216BBEC07'},
 @{name='libavif-1.4.2';sha256='2B645287340BA5A631D268B551DC2D72BD73AC33335962DD36DCDB6D8366921D'},
 @{name='libwebp-1.6.0';sha256='93A852C2B3EFAFEE3723EFD4636DE855B46F9FE1EFDDD607E1F42F60FC8F2136'}
)
foreach($archive in $archives){
 $file=Join-Path $PSScriptRoot ('vendor/codecs/'+$archive.name+'.tar.gz')
 if((Get-FileHash -LiteralPath $file).Hash -ne $archive.sha256){throw "依赖源码校验失败：$($archive.name)"}
 if(!(Test-Path (Join-Path $PSScriptRoot ('vendor/codecs/'+$archive.name)))){
  & tar -xzf $file -C (Join-Path $PSScriptRoot 'vendor/codecs')
  if($LASTEXITCODE -ne 0){throw "依赖源码解压失败：$($archive.name)"}
 }
}
$env:PYTHONPATH=$tools
$env:PATH=$compiler+';'+(Split-Path $ninja)+';'+$env:PATH
$env:CC=Join-Path $compiler 'clang.exe'
$env:CXX=Join-Path $compiler 'clang++.exe'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$prefix=Join-Path $output 'install'
& $python -m mesonbuild.mesonmain setup (Join-Path $output 'dav1d') (Join-Path $PSScriptRoot 'vendor/codecs/dav1d-1.5.4') --buildtype=release --default-library=static "--prefix=$prefix" -Denable_asm=false -Denable_tools=false -Denable_tests=false
if($LASTEXITCODE -ne 0){throw 'dav1d 配置失败'}
& $ninja -C (Join-Path $output 'dav1d') -j 4 install
if($LASTEXITCODE -ne 0){throw 'dav1d 构建失败'}
$common=@('-G','Ninja',"-DCMAKE_MAKE_PROGRAM=$ninja", "-DCMAKE_C_COMPILER=$env:CC", "-DCMAKE_CXX_COMPILER=$env:CXX",'-DCMAKE_BUILD_TYPE=Release','-DBUILD_SHARED_LIBS=OFF',"-DCMAKE_INSTALL_PREFIX=$prefix")
& $cmake -S (Join-Path $PSScriptRoot 'vendor/codecs/libwebp-1.6.0') -B (Join-Path $output 'webp') @common -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF
if($LASTEXITCODE -ne 0){throw 'WebP 配置失败'}
& $cmake --build (Join-Path $output 'webp') --parallel 4
if($LASTEXITCODE -ne 0){throw 'WebP 构建失败'}
& $cmake --install (Join-Path $output 'webp')
& $cmake -S (Join-Path $PSScriptRoot 'vendor/codecs/libavif-1.4.2') -B (Join-Path $output 'avif') @common -DAVIF_CODEC_DAV1D=SYSTEM -DAVIF_LIBYUV=OFF -DAVIF_BUILD_APPS=OFF -DAVIF_BUILD_TESTS=OFF "-DDAV1D_INCLUDE_DIR=$prefix/include" "-DDAV1D_LIBRARY=$prefix/lib/libdav1d.a"
if($LASTEXITCODE -ne 0){throw 'AVIF 配置失败'}
& $cmake --build (Join-Path $output 'avif') --parallel 4
if($LASTEXITCODE -ne 0){throw 'AVIF 构建失败'}
& $cmake --install (Join-Path $output 'avif')
if($LASTEXITCODE -ne 0){throw 'AVIF 安装至项目目录失败'}
Get-ChildItem (Join-Path $prefix 'lib') -Filter '*.a' | Select-Object Name,Length

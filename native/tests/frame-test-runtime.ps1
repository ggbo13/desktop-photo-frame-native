# 所有原生应用测试从这里启动，旧二进制不能保证托盘隔离，必须在运行前拦截。
function Assert-FrameTestVersion {
 param([Parameter(Mandatory)][Version]$Version)
 if($Version -lt [Version]'2.0.0.13'){throw '该程序不支持无托盘隔离，请使用 preview.13 或更新的构建；旧版对照需在独立 Windows 测试账户执行'}
}
function Start-FrameTest {
 param([Parameter(Mandatory,Position=0)][string]$FilePath,[string[]]$ArgumentList=@(),[switch]$PassThru,[ValidateSet('Normal','Hidden','Minimized','Maximized')][string]$WindowStyle)
 $root=Split-Path (Split-Path $PSScriptRoot)
 $binary=if([IO.Path]::IsPathRooted($FilePath)){[IO.Path]::GetFullPath($FilePath)}else{[IO.Path]::GetFullPath((Join-Path $root $FilePath))}
 $nativeRoot=[IO.Path]::GetFullPath((Join-Path $root 'native')).TrimEnd('\')+'\'
 if(!$binary.StartsWith($nativeRoot,[StringComparison]::OrdinalIgnoreCase)){throw '测试只允许运行工作区 native 目录内的副本'}
 $info=[Diagnostics.FileVersionInfo]::GetVersionInfo($binary)
 $version=[Version]::new($info.FileMajorPart,$info.FileMinorPart,$info.FileBuildPart,$info.FilePrivatePart)
 Assert-FrameTestVersion -Version $version
 $launch=@{FilePath=$binary;ArgumentList=@('--no-tray')+$ArgumentList;PassThru=$true}
 if($PSBoundParameters.ContainsKey('WindowStyle')){$launch.WindowStyle=$WindowStyle}
 $process=Start-Process @launch
 if($PassThru){return $process}
}

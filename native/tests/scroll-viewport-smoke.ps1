param([string]$Executable='native/out/2.0.0-preview.20-delivery/DesktopPhotoFrameNative.exe')
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'manual-resize-profile.ps1') -Executable $Executable -WithPhotos -AutomatedScroll

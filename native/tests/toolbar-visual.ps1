param([string]$Executable='native/out/2.0.0-preview.19-delivery/DesktopPhotoFrameNative.exe')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
$root=Split-Path (Split-Path $PSScriptRoot)
$output=Join-Path $root ('native/artifacts/toolbar-visual-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $output -Force|Out-Null
$fixtures=Join-Path $root 'test-artifacts/v3-1.25-1790082981600/photos'
$state=@{global=@{paused=$true;alwaysOnTop=$true};bounds=@{x=120;y=120;width=480;height=360};playlists=@(@{id='toolbar';name='工具栏预览';sources=@(@{type='file';path=(Join-Path $fixtures 'a-landscape.bmp')});config=@{ratio='auto';anchor='bottom-right';frame='minimal';color='#202729';thickness=18;radius=10;shadow=0;interval=10}})}
$state|ConvertTo-Json -Depth 12|Set-Content (Join-Path $output 'settings.json') -Encoding utf8
Add-Type -AssemblyName System.Drawing
Add-Type 'using System; using System.Runtime.InteropServices; public static class ToolbarVisualTest { [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left,top,right,bottom; } [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y); [DllImport("user32.dll")] public static extern void mouse_event(uint flags,uint x,uint y,uint data,UIntPtr extra); [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="FindWindowW")] public static extern IntPtr FindWindow(string cls,string title); [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr parent,int id); [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd,out Rect rect); }'
$process=Start-FrameTest $Executable -ArgumentList @('--data-dir',('"'+$output+'"'),'--test-seconds','8') -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 2
[ToolbarVisualTest]::SetCursorPos(360,298)|Out-Null
Start-Sleep -Milliseconds 500
$image=[Drawing.Bitmap]::new(700,650);$graphics=[Drawing.Graphics]::FromImage($image)
try{$graphics.CopyFromScreen(0,0,0,0,$image.Size);$image.Save((Join-Path $output 'toolbar.png'))}finally{$graphics.Dispose();$image.Dispose()}
$image=[Drawing.Bitmap]::new(700,650);$graphics=[Drawing.Graphics]::FromImage($image)
try{$toolbar=[ToolbarVisualTest]::FindWindow('NativePhotoFrameSettings','相框工具栏');$button=[ToolbarVisualTest]::GetDlgItem($toolbar,120);$rect=[ToolbarVisualTest+Rect]::new();[ToolbarVisualTest]::GetWindowRect($button,[ref]$rect)|Out-Null;[ToolbarVisualTest]::SetCursorPos(($rect.left+$rect.right)/2,($rect.top+$rect.bottom)/2)|Out-Null;Start-Sleep -Milliseconds 100;[ToolbarVisualTest]::mouse_event(2,0,0,0,[UIntPtr]::Zero);[ToolbarVisualTest]::mouse_event(4,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 300;$graphics.CopyFromScreen(0,0,0,0,$image.Size);$image.Save((Join-Path $output 'toolbar-menu.png'))}finally{$graphics.Dispose();$image.Dispose()}
$process.WaitForExit(12000)|Out-Null
Write-Output "工具栏截图=$output\toolbar.png；退出码=$($process.ExitCode)"

param([string]$Executable='native/out/2.0.0-preview.20-delivery/DesktopPhotoFrameNative.exe',[int]$Seconds=12)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
$root=Split-Path (Split-Path $PSScriptRoot)
$output=Join-Path $root ('native/artifacts/app-flicker-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$photos=Join-Path $output 'photos'
New-Item -ItemType Directory -Path $photos -Force | Out-Null
function New-SolidBmp([string]$Path,[int]$Width,[int]$Height,[byte]$Red,[byte]$Green,[byte]$Blue){
 $stride=([int][Math]::Floor(($Width*3+3)/4))*4;$pixels=New-Object byte[] ($stride*$Height)
 for($y=0;$y -lt $Height;$y++){for($x=0;$x -lt $Width;$x++){$i=$y*$stride+$x*3;$pixels[$i]=$Blue;$pixels[$i+1]=$Green;$pixels[$i+2]=$Red}}
 $header=New-Object byte[] 54;[Array]::Copy([byte[]](0x42,0x4d),0,$header,0,2)
 [Array]::Copy([BitConverter]::GetBytes([int](54+$pixels.Length)),0,$header,2,4);[Array]::Copy([BitConverter]::GetBytes([int]54),0,$header,10,4)
 [Array]::Copy([BitConverter]::GetBytes([int]40),0,$header,14,4);[Array]::Copy([BitConverter]::GetBytes([int]$Width),0,$header,18,4)
 [Array]::Copy([BitConverter]::GetBytes([int]$Height),0,$header,22,4);[Array]::Copy([BitConverter]::GetBytes([short]1),0,$header,26,2)
 [Array]::Copy([BitConverter]::GetBytes([short]24),0,$header,28,2);[Array]::Copy([BitConverter]::GetBytes([int]($pixels.Length)),0,$header,34,4)
 [System.IO.File]::WriteAllBytes($Path,[byte[]]($header+$pixels))
}
$landscape=Join-Path $photos '01-teal.bmp';$portrait=Join-Path $photos '02-orange.bmp'
New-SolidBmp $landscape 640 480 20 190 155;New-SolidBmp $portrait 480 640 230 95 24
$state=@{global=@{paused=$false;alwaysOnTop=$true};bounds=@{x=120;y=120;width=480;height=360};playlists=@(@{id='flicker';name='闪烁像素验证';sources=@(@{type='file';path=$landscape},@{type='file';path=$portrait});config=@{ratio='auto';anchor='bottom-right';frame='minimal';color='#202729';thickness=18;radius=10;shadow=0;interval=0.5}})}
$state|ConvertTo-Json -Depth 12|Set-Content (Join-Path $output 'settings.json') -Encoding utf8
$process=Start-FrameTest $Executable -ArgumentList @('--data-dir',('"'+$output+'"'),'--stress','--test-seconds',"$Seconds") -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds 500
$witness=Start-Process 'native/out/candidate-19/CaptureWitness.exe' -ArgumentList @(('"'+(Join-Path $output 'capture')+'"'),"$Seconds",'--video') -WindowStyle Hidden -PassThru
while(!$process.HasExited){Start-Sleep -Seconds 1;$process.Refresh()}
$witness.WaitForExit(15000)|Out-Null
Write-Output "闪烁像素录像=$output；应用退出码=$($process.ExitCode)；采集退出码=$($witness.ExitCode)"

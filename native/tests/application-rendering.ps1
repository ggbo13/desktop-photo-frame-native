param([string]$Executable='native/out/2.0.0-preview.16-delivery/DesktopPhotoFrameNative.exe',[int]$Seconds=130,[double]$Scale=1,[switch]$Video)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'frame-test-runtime.ps1')
$root=Split-Path (Split-Path $PSScriptRoot)
$output=Join-Path $root ('native/artifacts/app-render-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $output -Force | Out-Null
$fixtures=Join-Path $root 'test-artifacts/v3-1.25-1790082981600/photos'
$state=@{global=@{paused=$false;alwaysOnTop=$true};bounds=@{x=120;y=120;width=(480*$Scale);height=(360*$Scale)};playlists=@(@{id='render';name='动画验证';sources=@(@{type='file';path=(Join-Path $fixtures 'a-landscape.bmp')},@{type='file';path=(Join-Path $fixtures 'b-portrait.bmp')});config=@{ratio='auto';anchor='bottom-right';frame='minimal';color='#202729';thickness=[Math]::Round(18*$Scale);radius=[Math]::Round(10*$Scale);shadow=0;interval=3}})}
$state | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $output 'settings.json') -Encoding utf8
$process=Start-FrameTest $Executable -ArgumentList @('--data-dir',('"'+$output+'"'),'--stress','--test-seconds',"$Seconds",'--metrics',('"'+$output+'\metrics.csv"')) -WindowStyle Hidden -PassThru
Write-Output "原生应用动画验证 PID=$($process.Id)；报告目录=$output"
$arguments=@(('"'+$output+'\capture"'),"$Seconds")
if($Video){$arguments+='--video'}else{$arguments+='--timing-only'}
$witness=Start-Process 'native/out/candidate-19/CaptureWitness.exe' -ArgumentList $arguments -WindowStyle Hidden -PassThru
$dwm=Get-Process dwm | Select-Object -First 1
$present=Start-Process 'native/toolchain/PresentMon-2.5.1-x64.exe' -ArgumentList @('--process_id',"$($dwm.Id)",'--session_name',('NativeApp-'+$process.Id),'--output_file',('"'+$output+'\present.csv"'),'--timed',"$Seconds",'--terminate_after_timed','--no_console_stats') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $output 'present-stdout.txt') -RedirectStandardError (Join-Path $output 'present-stderr.txt')
while(!$process.HasExited){Start-Sleep -Seconds 2;$process.Refresh()}
$witness.WaitForExit(15000)|Out-Null;$present.WaitForExit(15000)|Out-Null
@{qpcFrequency=[Diagnostics.Stopwatch]::Frequency;exitCode=$process.ExitCode;witnessExit=$witness.ExitCode;renderingScale=$Scale;realSystemDpiTest=$false;video=[bool]$Video} | ConvertTo-Json | Set-Content (Join-Path $output 'run.json')
if(!$Video){& 'C:/APP/Node.js/node.exe' native/tests/analyze-presentation.mjs $output;& 'C:/APP/Node.js/node.exe' native/tests/analyze-app-geometry.mjs $output}
Write-Output "动画验证结束；$output"

param([Parameter(Mandatory)][string]$Run,[double]$Scale=1)
$ErrorActionPreference='Stop'
$frames=@(Import-Csv (Join-Path $Run 'capture/frames.csv'))
$events=@(Import-Csv (Join-Path $Run 'metrics.csv.events.csv'))
$checked=0;$failures=@();$coverage=@{};$plateaus=@();$sampleGaps=@()
foreach($event in $events){
    $begin=[double]$event.qpc_begin;$duration=[double]$event.qpc_duration
    if($duration -le 0){continue}
    $samples=@($frames | Where-Object { [double]$_.qpc -ge $begin -and [double]$_.qpc -le ($begin+$duration) -and [int]$_.border_pixels -gt 2000 })
    if($samples.Count -lt 3){continue}
    $anchor=[int]$event.anchor
    $xs=@($samples | ForEach-Object { [int]$_.x + $(if($anchor%2){[int]$_.width}else{0}) })
    $ys=@($samples | ForEach-Object { [int]$_.y + $(if($anchor -ge 2){[int]$_.height}else{0}) })
    $xm=$xs | Measure-Object -Minimum -Maximum;$ym=$ys | Measure-Object -Minimum -Maximum
    $dx=$xm.Maximum-$xm.Minimum;$dy=$ym.Maximum-$ym.Minimum
    $checked++;$key="$anchor/$($event.effect)";$coverage[$key]++
    if($dx -gt 1 -or $dy -gt 1){$failures+=@{begin=$begin;anchor=$anchor;effect=$event.effect;dx=$dx;dy=$dy}}
    # 仅检查动画中段；端点缓动与像素取整会自然产生相同尺寸。
    $middle=@($samples | Where-Object {[double]$_.qpc -gt $begin+.15*$duration -and [double]$_.qpc -lt $begin+.85*$duration})
    $previous=$null;$sameStart=0
    foreach($sample in $middle){
        $time=[double]$sample.qpc
        if($previous){
            $gap=($time-[double]$previous.qpc)/($duration/.42)*1000
            if($gap -gt 50){$sampleGaps+=@{begin=$begin;gapMs=$gap}}
            if($sample.width -eq $previous.width -and $sample.height -eq $previous.height){
                if(!$sameStart){$sameStart=[double]$previous.qpc}
                $still=($time-$sameStart)/($duration/.42)*1000
                if($still -gt 50){$plateaus+=@{begin=$begin;stillMs=$still}}
            }else{$sameStart=0}
        }
        $previous=$sample
    }
}
Add-Type -AssemblyName System.Drawing
$edges=@()
function Test-Border($color){return [Math]::Abs([int]$color.R-32) -le 2 -and [Math]::Abs([int]$color.G-39) -le 2 -and [Math]::Abs([int]$color.B-41) -le 2}
foreach($file in Get-ChildItem (Join-Path $Run 'capture') -Filter 'frame-*.png'){
    $bmp=[System.Drawing.Bitmap]::FromFile($file.FullName)
    try{
        $cx=[int][Math]::Floor($bmp.Width/2);$cy=[int][Math]::Floor($bmp.Height/2)
        $left=0;while($left -lt $cx -and (Test-Border $bmp.GetPixel($left,$cy))){$left++}
        $right=0;while($right -lt $cx -and (Test-Border $bmp.GetPixel(($bmp.Width-1-$right),$cy))){$right++}
        $top=0;while($top -lt $cy -and (Test-Border $bmp.GetPixel($cx,$top))){$top++}
        $bottom=0;while($bottom -lt $cy -and (Test-Border $bmp.GetPixel($cx,($bmp.Height-1-$bottom)))){$bottom++}
        $edges+=@{file=$file.Name;left=$left;right=$right;top=$top;bottom=$bottom;pass=(@($left,$right,$top,$bottom | Where-Object {$_ -lt (18*$Scale-1.5) -or $_ -gt (18*$Scale+1.5)}).Count -eq 0)}
    }finally{$bmp.Dispose()}
}
# DWM 统计按输出交换链分开；它只能证明系统呈现节奏，不能单独证明相框逐帧更新。
$chains=@()
foreach($group in (Import-Csv (Join-Path $Run 'present.csv') | Group-Object SwapChainAddress)){
    $times=@($group.Group | ForEach-Object {if($_.MsBetweenDisplayChange -ne 'NA'){[double]$_.MsBetweenDisplayChange}} | Where-Object {$_ -gt 0} | Sort-Object)
    if(!$times.Count){continue}
    $median=$times[[int][Math]::Floor($times.Count/2)]
    $chains+=@{chain=$group.Name;samples=$times.Count;medianMs=$median;maxMs=$times[-1];over50ms=@($times | Where-Object {$_ -gt 50}).Count;longIntervalPercent=100*@($times | Where-Object {$_ -gt 1.5*$median}).Count/$times.Count}
}
$report=@{scale=$Scale;checkedTransitions=$checked;cornerFailures=$failures;coverage=$coverage;edgeFrames=$edges.Count;edgeFailures=@($edges | Where-Object {!$_.pass});dwmOutputs=$chains;captureSource=(Get-Content (Join-Path $Run 'capture/capture-source.txt') -Raw);limitations='渲染缩放模拟非系统 DPI；GDI 采样非逐次硬件呈现记录，不能据此宣称应用掉帧率达标。'}
$report.sampledMidAnimationPlateausOver50ms=$plateaus
$report.captureGapsOver50ms=$sampleGaps
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Run 'visual-summary.json') -Encoding utf8
$report | ConvertTo-Json -Depth 8

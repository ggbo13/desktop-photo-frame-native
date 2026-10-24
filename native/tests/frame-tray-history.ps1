function Get-FrameTestTrayHistory {
    if (-not ('FrameTestTrayHistoryReader' -as [type])) {
        Add-Type -Path (Join-Path $PSScriptRoot 'tray-history-reader.cs')
    }
    $session = (Get-Process -Id $PID).SessionId
    $key = $null
    foreach ($shell in @(Get-Process explorer -ErrorAction SilentlyContinue | Where-Object SessionId -EQ $session)) {
        $key = [FrameTestTrayHistoryReader]::OpenExplorerRoot($shell.Id)
        if ($key) { break }
    }
    # 无法核验真实视图时明确失败，不能把另一视图中的零记录当成验证通过。
    if (-not $key) { throw '无法读取资源管理器实际使用的托盘历史，请在有桌面会话的测试账户执行。' }
    try {
        @($key.GetSubKeyNames() | ForEach-Object {
            $child = $key.OpenSubKey($_)
            if ($child) {
                try { [pscustomobject]@{ key = $_; path = [string]$child.GetValue('ExecutablePath') } }
                finally { $child.Dispose() }
            }
        })
    } finally { $key.Dispose() }
}

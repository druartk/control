param(
    [Parameter(Mandatory = $true)][string]$Port,
    [switch]$DiscoveryOnly,
    [switch]$NoRescan,
    [int]$DiscoveryWaitSeconds = 60,
    [int]$RefreshCount = 10,
    [int]$RescanCount = 10
)

$serial = New-Object System.IO.Ports.SerialPort $Port, 115200, None, 8, One
$serial.ReadTimeout = 100
$serial.WriteTimeout = 1000
$script:buffer = ''
$script:nextId = 1

function Read-Lines {
    $chunk = $serial.ReadExisting()
    if (-not $chunk) { return @() }
    $script:buffer += $chunk
    $lines = @()
    while (($newline = $script:buffer.IndexOf("`n")) -ge 0) {
        $line = $script:buffer.Substring(0, $newline).TrimEnd("`r")
        $script:buffer = $script:buffer.Substring($newline + 1)
        if ($line.StartsWith('@@CFG:')) { Write-Host $line }
        elseif ($line -match 'CONTROL_MATTER|CONTROL_CONFIG|CONTROL_MATTER_DISCOVERY|rst:|Guru Meditation|watchdog|assert|abort') { Write-Host "SERIAL_LOG: $line" }
        if ($line.StartsWith('@@CFG:')) {
            try { $lines += ($line.Substring(6) | ConvertFrom-Json) } catch {}
        }
    }
    return $lines
}

function Request {
    param([string]$Command, [hashtable]$Params = @{}, [int]$TimeoutMilliseconds = 8000)
    $id = $script:nextId++
    $body = @{ cmd = $Command; protocol = 1; request_id = $id }
    foreach ($key in $Params.Keys) { $body[$key] = $Params[$key] }
    $serial.Write(('@@CFG:' + ($body | ConvertTo-Json -Compress -Depth 8) + "`n"))
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        foreach ($message in (Read-Lines)) {
            if ($null -ne $message.request_id -and [int]$message.request_id -eq $id) { return $message }
        }
        Start-Sleep -Milliseconds 50
    }
    throw "Timeout waiting for $Command request $id"
}

function Wait-Scan {
    param([int]$ScanId, [int]$TimeoutSeconds = 300)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        try { $status = Request 'get_discovery_status' @{} 15000 }
        catch {
            if ([DateTime]::UtcNow -ge $deadline -or $_.Exception.Message -notmatch 'Timeout') { throw }
            Start-Sleep -Milliseconds 250
            continue
        }
        if ([int]$status.scan_id -ne $ScanId) { throw "Stale discovery result: expected $ScanId, got $($status.scan_id)" }
        if (-not $status.scanning) {
            if ($status.phase -eq 'failed') { throw "Matter discovery failed: error $($status.error)" }
            return $status
        }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Timeout waiting for Matter scan $ScanId"
}

function Get-AllDevices {
    $first = Request 'get_devices' @{ offset = 0; limit = 16 } 60000
    $devices = @($first.devices)
    for ($offset = $devices.Count; $offset -lt [int]$first.count; $offset += 16) {
        $page = Request 'get_devices' @{ offset = $offset; limit = 16 } 60000
        $devices += @($page.devices)
    }
    return [pscustomobject]@{ count = [int]$first.count; devices = $devices }
}

try {
    $serial.Open()
    Start-Sleep -Seconds 2
    $hello = Request 'hello'
    $status = Request 'get_status'
    if ($DiscoveryOnly) {
        for ($attempt = 1; $attempt -le 15 -and -not $status.controller_ready; $attempt++) {
            Start-Sleep -Seconds 2
            $status = Request 'get_status'
        }
        if (-not $status.controller_ready) { throw 'Matter controller did not become ready' }
        $rescan = Request 'rescan_devices' @{} 15000
        if (-not $rescan.accepted) { throw "Rescan rejected: $($rescan.error)" }
        $scan = Wait-Scan ([int]$rescan.scan_id) $DiscoveryWaitSeconds
        $after = Get-AllDevices
        Write-Host ("ACCEPT: status={0} rescan_accepted={1} scan_id={2} duration_ms={3} devices_after={4}" -f $status.type, $rescan.accepted, $scan.scan_id, $scan.duration_ms, $after.count)
        if ($after.devices) { $after.devices | ConvertTo-Json -Compress -Depth 8 | Write-Host }
        return
    }
    $configs = Request 'get_configs' @{} 60000
    $config = Request 'get_config' @{ config_id = 1 }
    $before = Get-AllDevices
    $expectedNames = @($before.devices | ForEach-Object { "$($_.node_id):$($_.endpoint)=$($_.name)" } | Sort-Object)
    $expectedCount = [int]$before.count
    $refreshCounts = @()
    for ($i = 1; $i -le $RefreshCount; $i++) {
        $refreshStarted = [DateTime]::UtcNow
        $refresh = Get-AllDevices
        $refreshCounts += [int]$refresh.count
        Write-Host ("ACCEPT: refresh={0} duration_ms={1} devices={2}" -f $i, ([DateTime]::UtcNow - $refreshStarted).TotalMilliseconds, $refresh.count)
    }
    $rescan = [pscustomobject]@{ accepted = $null }
    $rescanDurations = @()
    if (-not $NoRescan) {
        for ($i = 1; $i -le $RescanCount; $i++) {
            $rescanStarted = [DateTime]::UtcNow
            $rescan = Request 'rescan_devices' @{} 15000
            if (-not $rescan.accepted) { throw "Rescan $i rejected: $($rescan.error)" }
            $scan = Wait-Scan ([int]$rescan.scan_id) $DiscoveryWaitSeconds
            $afterScan = Get-AllDevices
            $duration = ([DateTime]::UtcNow - $rescanStarted).TotalMilliseconds
            $rescanDurations += $duration
            Write-Host ("ACCEPT: rescan={0} scan_id={1} duration_ms={2} end_to_end_ms={3} devices={4}" -f $i, $scan.scan_id, $scan.duration_ms, $duration, $afterScan.count)
            $actualNames = @($afterScan.devices | ForEach-Object { "$($_.node_id):$($_.endpoint)=$($_.name)" } | Sort-Object)
            if ($i -eq 1 -and [int]$afterScan.count -gt $expectedCount) {
                Write-Host ("ACCEPT: legacy_cache_expanded={0}->{1}" -f $expectedCount, $afterScan.count)
                $expectedCount = [int]$afterScan.count
                $expectedNames = $actualNames
            }
            if ([int]$afterScan.count -ne $expectedCount) { throw "Device count changed on rescan $($i): $expectedCount -> $($afterScan.count)" }
            if (($actualNames -join "`n") -ne ($expectedNames -join "`n")) { throw "Device names changed on rescan $i" }
            $after = $afterScan
        }
    } else { $after = $before }
    Write-Host ("ACCEPT: hello={0} controller_ready={1} commissioned={2} node={3}" -f $hello.type, $hello.controller_ready, $hello.matter_commissioned, $hello.remote_node_id)
    Write-Host ("ACCEPT: status={0} rescan_accepted={1}" -f $status.type, $rescan.accepted)
    Write-Host ("ACCEPT: configs={0} active={1} config={2}" -f $configs.configs.Count, $configs.active_config_id, $config.type)
    Write-Host ("ACCEPT: devices_before={0} devices_after={1} serialized_count={2}" -f $before.count, $after.count, $after.count)
    Write-Host ("ACCEPT: refresh_counts={0} rescan_durations_ms={1}" -f (($refreshCounts -join ',')), (($rescanDurations -join ',')))
    if ($after.devices) { $after.devices | ConvertTo-Json -Compress -Depth 8 | Write-Host }
} catch {
    Write-Host "ACCEPT: FAIL $($_.Exception.Message)"
    exit 1
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}

param(
    [Parameter(Mandatory = $true)]
    [string]$Port
)

$serial = New-Object System.IO.Ports.SerialPort
$serial.PortName = $Port
$serial.BaudRate = 115200
$serial.DataBits = 8
$serial.Parity = [System.IO.Ports.Parity]::None
$serial.StopBits = [System.IO.Ports.StopBits]::One
$serial.Handshake = [System.IO.Ports.Handshake]::None
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.ReadTimeout = 200
$serial.WriteTimeout = 1000

$script:rxBuffer = ''
$helloPass = $false
$statusPass = $false
$configsPass = $false

function Send-ProtocolFrame {
    param(
        [Parameter(Mandatory = $true)][string]$Command,
        [Parameter(Mandatory = $true)][int]$RequestId
    )

    $line = '@@CFG:{"cmd":"' + $Command + '","protocol":1,"request_id":' + $RequestId + '}' + "`n"
    Write-Host $line.TrimEnd("`n")
    $serial.Write($line)
}

function Receive-Response {
    param(
        [Parameter(Mandatory = $true)][int]$RequestId,
        [int]$TimeoutMilliseconds = 5000
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $chunk = $serial.ReadExisting()
        if ($chunk) {
            $script:rxBuffer += $chunk
            while (($newline = $script:rxBuffer.IndexOf("`n")) -ge 0) {
                $line = $script:rxBuffer.Substring(0, $newline).TrimEnd("`r")
                $script:rxBuffer = $script:rxBuffer.Substring($newline + 1)
                if (-not $line.StartsWith('@@CFG:')) {
                    continue
                }
                Write-Host $line
                try {
                    $message = $line.Substring(6) | ConvertFrom-Json
                    if ($null -ne $message.request_id -and [int]$message.request_id -eq $RequestId) {
                        return $message
                    }
                } catch {
                    continue
                }
            }
        }
        Start-Sleep -Milliseconds 50
    }
    return $null
}

try {
    $serial.Open()
    Start-Sleep -Seconds 10

    for ($attempt = 1; $attempt -le 5 -and -not $helloPass; $attempt++) {
        Send-ProtocolFrame -Command 'hello' -RequestId 1
        $hello = Receive-Response -RequestId 1
        if ($null -ne $hello -and $hello.type -eq 'hello' -and [int]$hello.protocol -eq 1) {
            $helloPass = $true
        }
    }

    if ($helloPass) {
        Send-ProtocolFrame -Command 'get_status' -RequestId 2
        $status = Receive-Response -RequestId 2
        $statusPass = $null -ne $status -and $status.type -eq 'status'
    }

    if ($statusPass) {
        Send-ProtocolFrame -Command 'get_configs' -RequestId 3
        $configs = Receive-Response -RequestId 3
        $configsPass = $null -ne $configs -and $configs.type -eq 'configs'
    }
} catch {
    Write-Host "SERIAL: FAIL ($($_.Exception.Message))"
} finally {
    if ($null -ne $serial) {
        if ($serial.IsOpen) {
            $serial.Close()
        }
        $serial.Dispose()
    }
}

Write-Host ("HELLO: {0}" -f $(if ($helloPass) { 'PASS' } else { 'FAIL' }))
Write-Host ("GET_STATUS: {0}" -f $(if ($statusPass) { 'PASS' } else { 'FAIL' }))
Write-Host ("GET_CONFIGS: {0}" -f $(if ($configsPass) { 'PASS' } else { 'FAIL' }))
$verdict = $helloPass -and $statusPass -and $configsPass
Write-Host ("VERDICT: {0}" -f $(if ($verdict) { 'PASS' } else { 'FAIL' }))
if (-not $verdict) { exit 1 }

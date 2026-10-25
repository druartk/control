[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BusId,
    [Parameter(Mandatory = $true)]
    [string]$StdoutLog,
    [Parameter(Mandatory = $true)]
    [string]$StderrLog,
[switch]$UseUnplugged
)

# StdoutLog/StderrLog are retained for the Bash interface. Do not redirect
# child handles here: inherited redirected handles can keep WSL attached to the
# long-running watcher. The watcher is intentionally hidden and detached.
$arguments = @('attach', '--wsl', '--auto-attach', '--busid', $BusId)
if ($UseUnplugged) {
    $arguments += '--unplugged'
}
$process = Start-Process -FilePath 'usbipd.exe' -ArgumentList ($arguments -join ' ') -WindowStyle Hidden -PassThru
if ($null -eq $process) {
    throw 'Process.Start returned null'
}

[Console]::Out.WriteLine($process.Id)

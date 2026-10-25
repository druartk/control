[CmdletBinding()]
param(
    [string]$Port = '',
    [Parameter(Mandatory = $true)] [string]$Bootloader,
    [Parameter(Mandatory = $true)] [string]$PartitionTable,
    [Parameter(Mandatory = $true)] [string]$Application,
    [string]$ControlWindowsEsptool = '',
    [int]$TimeoutSeconds = 180,
    [switch]$TestOnly,
    [switch]$VersionOnly
)

if (-not ('Control.NativeCommandLine' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;

namespace Control {
    public static class NativeCommandLine {
        [DllImport("shell32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CommandLineToArgvW(string commandLine, out int argc);

        [DllImport("kernel32.dll")]
        private static extern IntPtr LocalFree(IntPtr memory);

        public static string[] Parse(string commandLine) {
            int argc;
            IntPtr argv = CommandLineToArgvW(commandLine, out argc);
            if (argv == IntPtr.Zero) {
                throw new InvalidOperationException("CommandLineToArgvW failed");
            }
            try {
                string[] result = new string[argc];
                for (int i = 0; i < argc; i++) {
                    IntPtr item = Marshal.ReadIntPtr(argv, i * IntPtr.Size);
                    result[i] = Marshal.PtrToStringUni(item);
                }
                return result;
            }
            finally {
                LocalFree(argv);
            }
        }
    }
}
'@
}

if (-not ('Control.ProcessOutput' -as [type])) {
    Add-Type @'
using System;
using System.Diagnostics;

namespace Control {
    public static class ProcessOutput {
        public static volatile bool HashVerified;
        public static volatile bool StdoutClosed;
        public static volatile bool StderrClosed;
        public static readonly DataReceivedEventHandler StdoutHandler = ForwardStdout;
        public static readonly DataReceivedEventHandler StderrHandler = ForwardStderr;

        public static void Reset() {
            HashVerified = false;
            StdoutClosed = false;
            StderrClosed = false;
        }

        private static void ForwardStdout(object sender, DataReceivedEventArgs e) {
            if (e.Data == null) {
                StdoutClosed = true;
                return;
            }
            Console.Out.WriteLine(e.Data);
            Console.Out.Flush();
            if (e.Data.IndexOf("Hash of data verified", StringComparison.OrdinalIgnoreCase) >= 0) {
                HashVerified = true;
            }
        }

        private static void ForwardStderr(object sender, DataReceivedEventArgs e) {
            if (e.Data == null) {
                StderrClosed = true;
                return;
            }
            Console.Error.WriteLine(e.Data);
            Console.Error.Flush();
        }
    }
}
'@
}

function Parse-CommandLine([string]$RawCommand) {
    if ([string]::IsNullOrWhiteSpace($RawCommand)) {
        throw 'empty_command'
    }
    @([Control.NativeCommandLine]::Parse($RawCommand))
}

function Escape-Diagnostic([string]$Value) {
    if ($null -eq $Value) { return '' }
    $Value.Replace('\', '\\').Replace("`r", '\r').Replace("`n", '\n').Replace('"', '\"')
}

function Resolve-Executable([string]$Name) {
    $script:ResolveReason = ''
    if ($Name -match '(?i)WindowsApps') {
        $script:ResolveReason = 'windows_store_alias'
        return $null
    }
    if (Test-Path -LiteralPath $Name -PathType Leaf) {
        return (Get-Item -LiteralPath $Name).FullName
    }
    $command = Get-Command -Name $Name -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($command) {
        $resolved = if ($command.Path) { $command.Path } else { $command.Source }
        if ($resolved -match '(?i)WindowsApps') {
            $script:ResolveReason = 'windows_store_alias'
            return $null
        }
        return $resolved
    }
    $script:ResolveReason = 'executable_not_found'
    $null
}

function New-CommandModel([string]$RawCommand, [string]$Source) {
    try {
        $tokens = @(Parse-CommandLine $RawCommand)
    }
    catch {
        $script:ResolveReason = $_.Exception.Message
        return $null
    }
    if ($tokens.Count -eq 0 -or [string]::IsNullOrWhiteSpace($tokens[0])) {
        $script:ResolveReason = 'missing_executable'
        return $null
    }
    $executable = Resolve-Executable $tokens[0]
    if (-not $executable) { return $null }
    [pscustomobject]@{
        Source = $Source
        Executable = $executable
        PrefixArguments = @($tokens | Select-Object -Skip 1)
    }
}

function Quote-WindowsArgument([string]$Value) {
    if ($Value -notmatch '[\s"]') { return $Value }
    $escaped = $Value.Replace('\', '\\').Replace('"', '\"')
    '"' + $escaped + '"'
}

function Join-WindowsArguments([string[]]$Arguments) {
    (($Arguments | ForEach-Object { Quote-WindowsArgument $_ }) -join ' ')
}

function Invoke-VersionCheck($Model) {
    $fileName = $Model.Executable
    $arguments = @($Model.PrefixArguments) + @('version')
    if ($fileName -match '(?i)\.(cmd|bat)$') {
        $commandText = '"' + $fileName + '" ' + (Join-WindowsArguments $arguments)
        $fileName = $env:ComSpec
        $arguments = @('/d', '/s', '/c', '"' + $commandText + '"')
    }
    try {
        $combined = (& $fileName @arguments 2>&1 | Out-String)
        $exitCode = $LASTEXITCODE
        $forbidden = $combined -match '(?i)Python est introuvable|Python was not found|Microsoft Store|WindowsApps'
        $hasEsptool = $combined -match '(?i)esptool'
        $accepted = ($exitCode -eq 0) -and $hasEsptool -and (-not $forbidden)
        $reason = if ($forbidden) { 'windows_store_alias' }
            elseif ($exitCode -ne 0) { 'version_exit_code' }
            elseif (-not $hasEsptool) { 'version_output_missing_esptool' }
            else { 'version_ok' }
        [pscustomobject]@{
            Accepted = $accepted
            ExitCode = $exitCode
            Reason = $reason
            Stdout = $combined
            Stderr = ''
        }
    }
    catch {
        [pscustomobject]@{
            Accepted = $false
            ExitCode = -1
            Reason = 'process_start_failed'
            Stdout = ''
            Stderr = $_.Exception.Message
        }
    }
}

function Wait-ProcessGone([int]$ProcessId, [int]$WaitSeconds = 10) {
    $deadline = [DateTime]::UtcNow.AddSeconds($WaitSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $process = Get-Process -Id $ProcessId -ErrorAction Stop
            if ($process.HasExited) { return $true }
        }
        catch { return $true }
        Start-Sleep -Milliseconds 100
    }
    try {
        $process = Get-Process -Id $ProcessId -ErrorAction Stop
        return $process.HasExited
    }
    catch { return $true }
}

function Stop-ProcessTree([int]$ProcessId) {
    try { & taskkill.exe /PID $ProcessId /T /F *> $null } catch { }
    try {
        $process = Get-Process -Id $ProcessId -ErrorAction Stop
        if (-not $process.HasExited) { $process.Kill() }
    }
    catch { }
    $null = Wait-ProcessGone $ProcessId
}

function Invoke-EspTool {
    param(
        $Model,
        [string[]]$Arguments,
        [int]$ProcessTimeoutSeconds = 180
    )

    if ($ProcessTimeoutSeconds -le 0) { throw 'timeout_must_be_positive' }

    $allArguments = @($Model.PrefixArguments) + @($Arguments)
    $displayCommand = Join-WindowsArguments (@($Model.Executable) + $allArguments)
    $fileName = $Model.Executable
    $processArguments = Join-WindowsArguments $allArguments
    if ($fileName -match '(?i)\.(cmd|bat)$') {
        $commandText = '"' + $fileName + '" ' + $processArguments
        $fileName = $env:ComSpec
        $processArguments = Join-WindowsArguments @('/d', '/s', '/c', '"' + $commandText + '"')
    }

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $fileName
    $psi.Arguments = $processArguments
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $psi
    $process.EnableRaisingEvents = $true
    [Control.ProcessOutput]::Reset()
    $started = $false
    $timedOut = $false
    $process.add_OutputDataReceived([Control.ProcessOutput]::StdoutHandler)
    $process.add_ErrorDataReceived([Control.ProcessOutput]::StderrHandler)

    try {
        [Console]::Out.WriteLine('CONTROL_DEV: windows_esptool_launch=starting')
        [Console]::Out.WriteLine("CONTROL_DEV: windows_esptool_command=$(Escape-Diagnostic $displayCommand)")
        [Console]::Out.Flush()
        if (-not $process.Start()) { throw 'process_start_returned_false' }
        $started = $true
        [Console]::Out.WriteLine("CONTROL_DEV: windows_esptool_pid=$($process.Id)")
        [Console]::Out.Flush()
        $process.BeginOutputReadLine()
        $process.BeginErrorReadLine()

        $deadline = [DateTime]::UtcNow.AddSeconds($ProcessTimeoutSeconds)
        while (-not $process.HasExited) {
            if ([DateTime]::UtcNow -ge $deadline) {
                $timedOut = $true
                [Console]::Error.WriteLine('CONTROL_DEV: ERROR: Windows esptool timed out')
                [Console]::Error.WriteLine("CONTROL_DEV: windows_esptool_pid=$($process.Id)")
                [Console]::Error.Flush()
                Stop-ProcessTree $process.Id
                if (-not (Wait-ProcessGone $process.Id)) {
                    [Console]::Error.WriteLine("CONTROL_DEV: ERROR: Windows esptool process still exists after termination: $($process.Id)")
                    [Console]::Error.Flush()
                }
                break
            }
            Start-Sleep -Milliseconds 100
        }
        if ($timedOut) { return 124 }

        $process.WaitForExit(5000) | Out-Null
        $drainDeadline = [DateTime]::UtcNow.AddSeconds(5)
        while ((!([Control.ProcessOutput]::StdoutClosed) -or !([Control.ProcessOutput]::StderrClosed)) -and [DateTime]::UtcNow -lt $drainDeadline) {
            Start-Sleep -Milliseconds 50
        }
        if (!$([Control.ProcessOutput]::StdoutClosed) -or !$([Control.ProcessOutput]::StderrClosed)) {
            [Console]::Error.WriteLine('CONTROL_DEV: ERROR: Windows esptool output drain timed out')
            [Console]::Error.Flush()
            return 1
        }
        $exitCode = $process.ExitCode
        [Console]::Out.WriteLine("CONTROL_DEV: windows_esptool_exit_code=$exitCode")
        [Console]::Out.Flush()
        if ($exitCode -ne 0) { return 1 }
        if (-not [Control.ProcessOutput]::HashVerified) {
            [Console]::Error.WriteLine('CONTROL_DEV: ERROR: Windows esptool did not report Hash of data verified')
            [Console]::Error.Flush()
            return 1
        }
        return 0
    }
    catch {
        if ($started -and -not $process.HasExited) { Stop-ProcessTree $process.Id }
        [Console]::Error.WriteLine("CONTROL_DEV: ERROR: Windows esptool process failed: $($_.Exception.Message)")
        [Console]::Error.Flush()
        return 1
    }
    finally {
        $process.remove_OutputDataReceived([Control.ProcessOutput]::StdoutHandler)
        $process.remove_ErrorDataReceived([Control.ProcessOutput]::StderrHandler)
        $process.Dispose()
    }
}

function Write-Model($Model) {
    Write-Output "CONTROL_DEV: windows_esptool_executable=$($Model.Executable)"
    for ($i = 0; $i -lt $Model.PrefixArguments.Count; $i++) {
        Write-Output "CONTROL_DEV: windows_esptool_prefix_arg[$i]=$($Model.PrefixArguments[$i])"
    }
}

function Assert-Test([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Run-ProcessTests {
    $childPath = [IO.Path]::ChangeExtension([IO.Path]::GetTempFileName(), '.ps1')
    $child = @'
param([string]$Mode)
$ErrorActionPreference = 'Stop'
trap { [Console]::Error.WriteLine($_.Exception.Message); exit 33 }
[Console]::Out.WriteLine("CHILD_MODE=$Mode")

if ($Mode -eq 'large') {
    Add-Type -TypeDefinition (@(
        'using System;',
        'using System.Threading;',
        'public static class ProcessTestBurst {',
        '  public static void Run() {',
        '    var stdout = new Thread(() => {',
        '      Console.Out.WriteLine("OUT_BEGIN");',
        '      for (var i = 0; i < 200; i++) Console.Out.WriteLine(new string(''O'', 512));',
        '    });',
        '    var stderr = new Thread(() => {',
        '      Console.Error.WriteLine("ERR_BEGIN");',
        '      for (var i = 0; i < 200; i++) Console.Error.WriteLine(new string(''E'', 512));',
        '    });',
        '    stdout.Start(); stderr.Start(); stdout.Join(); stderr.Join();',
        '  }',
        '}'
    ) -join [Environment]::NewLine)
    [ProcessTestBurst]::Run()
    [Console]::Out.WriteLine('Hash of data verified.')
    exit 0
}
if ($Mode -eq 'sleep') {
    Start-Sleep -Seconds 20
    exit 0
}
if ($Mode -eq 'exit7') {
    [Console]::Out.WriteLine('Hash of data verified.')
    exit 7
}
if ($Mode -eq 'external_kill') {
    $killerCommand = "Start-Sleep -Milliseconds 500; Stop-Process -Id $PID -Force"
    Start-Process -FilePath powershell.exe -WindowStyle Hidden -ArgumentList @('-NoProfile', '-NonInteractive', '-Command', $killerCommand) | Out-Null
    while ($true) { Start-Sleep -Seconds 1 }
}
'@
    Set-Content -LiteralPath $childPath -Value $child -Encoding UTF8
    try {
        $psExe = (Get-Command powershell.exe -ErrorAction Stop).Source
        $model = [pscustomobject]@{
            Executable = $psExe
            PrefixArguments = @('-NoProfile', '-NonInteractive', '-File', $childPath)
        }

        $status = Invoke-EspTool $model @('-Mode', 'large') 10
        Assert-Test ($status -eq 0) 'CASE_N process did not stream concurrent stdout/stderr successfully'
        Write-Output 'CASE_N: PASS'

        $status = Invoke-EspTool $model @('-Mode', 'sleep') 1
        Assert-Test ($status -ne 0) 'CASE_O timeout unexpectedly passed'
        Write-Output 'CASE_O: PASS'

        $status = Invoke-EspTool $model @('-Mode', 'external_kill') 10
        Assert-Test ($status -ne 0) 'CASE_Q externally terminated child unexpectedly passed'
        Write-Output 'CASE_Q: PASS'
    }
    finally {
        Remove-Item -LiteralPath $childPath -Force -ErrorAction SilentlyContinue
    }
}

function Run-ParserTests {
    $h = @(Parse-CommandLine 'C:\Users\K\AppData\Local\Programs\Python\Python314\python.exe -m esptool')
    if ($h[0] -ne 'C:\Users\K\AppData\Local\Programs\Python\Python314\python.exe' -or $h[1] -ne '-m' -or $h[2] -ne 'esptool') { throw 'CASE_H parser mismatch' }
    Write-Output 'CASE_H: PASS'

    $i = @(Parse-CommandLine '"C:\Program Files\Python314\python.exe" -m esptool')
    if ($i[0] -ne 'C:\Program Files\Python314\python.exe' -or $i.Count -ne 3) { throw 'CASE_I parser mismatch' }
    Write-Output 'CASE_I: PASS'

    $j = @(Parse-CommandLine 'C:\Tools\esptool.exe')
    if ($j.Count -ne 1 -or $j[0] -ne 'C:\Tools\esptool.exe') { throw 'CASE_J parser mismatch' }
    Write-Output 'CASE_J: PASS'

    $null = New-CommandModel '' 'CONTROL_WINDOWS_ESPTOOL'
    if ($script:ResolveReason -ne 'empty_command') { throw 'CASE_K rejection mismatch' }
    Write-Output 'CASE_K: PASS'

    $null = New-CommandModel 'C:\Users\K\AppData\Local\Microsoft\WindowsApps\python.exe -m esptool' 'CONTROL_WINDOWS_ESPTOOL'
    if ($script:ResolveReason -ne 'windows_store_alias') { throw 'CASE_L rejection mismatch' }
    Write-Output 'CASE_L: PASS'

    $m = @(Parse-CommandLine 'py.exe -m esptool')
    if ($m[0] -ne 'py.exe' -or $m[1] -ne '-m' -or $m[2] -ne 'esptool') { throw 'CASE_M candidate mismatch' }
    Write-Output 'CASE_M: PASS'

    Run-ProcessTests
}

$scriptPath = (Get-Item -LiteralPath $MyInvocation.MyCommand.Path).FullName
Write-Output "CONTROL_DEV: windows_flash_script=$scriptPath"
if ($ControlWindowsEsptool) {
    Write-Output 'CONTROL_DEV: control_windows_esptool_present=yes'
    Write-Output "CONTROL_DEV: control_windows_esptool_raw=$(Escape-Diagnostic $ControlWindowsEsptool)"
}
else {
    Write-Output 'CONTROL_DEV: control_windows_esptool_present=no'
    Write-Output 'CONTROL_DEV: control_windows_esptool_raw='
}

if ($TestOnly) {
    Run-ParserTests
    exit 0
}

$model = $null
if ($ControlWindowsEsptool) {
    Write-Output 'CONTROL_DEV: windows_esptool_candidate=CONTROL_WINDOWS_ESPTOOL'
    $model = New-CommandModel $ControlWindowsEsptool 'CONTROL_WINDOWS_ESPTOOL'
    if (-not $model) {
        Write-Output 'CONTROL_DEV: windows_esptool_candidate_exit_code=-1'
        Write-Output 'CONTROL_DEV: windows_esptool_candidate_result=REJECTED'
        Write-Output "CONTROL_DEV: windows_esptool_candidate_reason=$script:ResolveReason"
        Write-Error "CONTROL_DEV: ERROR: explicit Windows esptool override rejected: $script:ResolveReason"
        exit 1
    }
    $check = Invoke-VersionCheck $model
    Write-Output "CONTROL_DEV: windows_esptool_candidate_exit_code=$($check.ExitCode)"
    $candidateResult = if ($check.Accepted) { 'ACCEPTED' } else { 'REJECTED' }
    Write-Output "CONTROL_DEV: windows_esptool_candidate_result=$candidateResult"
    Write-Output "CONTROL_DEV: windows_esptool_candidate_reason=$($check.Reason)"
    if (-not $check.Accepted) {
        if ($check.Stdout) { Write-Error "CONTROL_DEV: esptool_stdout=$($check.Stdout.Trim())" }
        if ($check.Stderr) { Write-Error "CONTROL_DEV: esptool_stderr=$($check.Stderr.Trim())" }
        exit 1
    }
}
else {
    foreach ($candidate in @('py.exe -m esptool', 'python.exe -m esptool', 'esptool.exe', 'esptool.py')) {
        $model = New-CommandModel $candidate 'auto'
        if ($model) {
            $check = Invoke-VersionCheck $model
            if ($check.Accepted) { break }
            $model = $null
        }
    }
    if (-not $model) {
        Write-Error 'CONTROL_DEV: ERROR: Windows esptool is not installed'
        Write-Output 'CONTROL_DEV: install_hint=py -m pip install esptool'
        exit 1
    }
}

if ($VersionOnly) {
    Write-Output 'CONTROL_DEV: windows_esptool_version_check=PASS'
    Write-Model $model
    exit 0
}

if ([string]::IsNullOrWhiteSpace($Port)) {
    Write-Error 'CONTROL_DEV: ERROR: Windows flash requires a detected serial port'
    exit 1
}

Write-Output 'CONTROL_DEV: windows_esptool_source=' + $model.Source
Write-Model $model
Write-Output "CONTROL_DEV: windows_port=$Port"
Write-Output "CONTROL_DEV: windows_bootloader=$Bootloader"
Write-Output "CONTROL_DEV: windows_partition_table=$PartitionTable"
Write-Output "CONTROL_DEV: windows_application=$Application"

$flashArgs = @(
    '--chip', 'esp32s3', '--port', $Port, '--baud', '460800',
    '--before', 'default_reset', '--after', 'hard_reset', 'write_flash',
    '--flash_mode', 'dio', '--flash_freq', '80m', '--flash_size', '16MB',
    '0x0', $Bootloader, '0x8000', $PartitionTable, '0x20000', $Application
)

if ($TimeoutSeconds -le 0) {
    Write-Error 'CONTROL_DEV: ERROR: Windows flash timeout must be a positive integer'
    exit 1
}

exit (Invoke-EspTool $model $flashArgs $TimeoutSeconds)

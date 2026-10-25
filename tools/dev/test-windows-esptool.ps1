[CmdletBinding()]
param()

$script = Join-Path $PSScriptRoot 'windows-esptool-flash.ps1'
& $script -TestOnly -Port COM3 -Bootloader bootloader.bin -PartitionTable partition-table.bin -Application control.bin
exit $LASTEXITCODE

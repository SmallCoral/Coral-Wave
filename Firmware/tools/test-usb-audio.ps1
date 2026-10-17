param(
    [ValidateRange(1, 120)][int]$Seconds = 1,
    [ValidateRange(0, 4000)][int]$Amplitude = 2000,
    [string]$CaptureFile = '',
    [string]$PlaybackFile = ''
)
$ErrorActionPreference = 'Stop'
if (-not ('CoralWaveUsbTest' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'usb-audio-test.cs')
}
[CoralWaveUsbTest]::Run($Seconds, $Amplitude, $CaptureFile, $PlaybackFile) | ConvertTo-Json

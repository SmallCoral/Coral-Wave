$ErrorActionPreference = 'Stop'
if (-not ('CoralWaveUsbTest' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'usb-audio-test.cs')
}
[CoralWaveUsbTest]::Run() | ConvertTo-Json

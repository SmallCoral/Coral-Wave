$ErrorActionPreference = 'Stop'
if (-not ('CoralWaveEndpoint' -as [type])) { Add-Type -Path (Join-Path $PSScriptRoot 'endpoint-controls.cs') }
$endpoints = Get-PnpDevice -PresentOnly -Class AudioEndpoint
$playDevice = @($endpoints | Where-Object { $_.FriendlyName -like 'Coral-Wave Playback*' })
$micDevice = @($endpoints | Where-Object { $_.FriendlyName -like 'Coral-Wave Microphone*' })
if ($playDevice.Count -ne 1 -or $micDevice.Count -ne 1) { throw 'Connect exactly one Coral-Wave board.' }
$play = [CoralWaveEndpoint]::Open($playDevice[0].InstanceId.Substring('SWD\MMDEVAPI\'.Length))
$mic = [CoralWaveEndpoint]::Open($micDevice[0].InstanceId.Substring('SWD\MMDEVAPI\'.Length))
$savedPlayDb = $play.Decibels; $savedPlayMute = $play.Muted
$savedMicDb = $mic.Decibels; $savedMicMute = $mic.Muted
$result = [ordered]@{}
function Get-BoardStatus {
    Start-Sleep -Milliseconds 100
    & (Join-Path $PSScriptRoot 'read-status.ps1') | ConvertFrom-Json
}
try {
    $play.Muted = $false
    $play.Decibels = -12
    $at12 = Get-BoardStatus
    $play.Decibels = -24
    $at24 = Get-BoardStatus
    if ([Math]::Abs($at12.PlaybackGainLeftQ15 - 8231) -gt 50 -or
        [Math]::Abs($at24.PlaybackGainLeftQ15 - 2068) -gt 20) {
        throw 'Windows playback volume did not reach firmware.'
    }
    $result.PlaybackGainAtMinus12Db = $at12.PlaybackGainLeftQ15
    $result.PlaybackGainAtMinus24Db = $at24.PlaybackGainLeftQ15
    $play.Muted = $true
    $muted = Get-BoardStatus
    if ($muted.PlaybackGainLeftQ15 -ne 0 -or $muted.PlaybackGainRightQ15 -ne 0) { throw 'Playback mute failed.' }
    $result.PlaybackMute = 'passed'
    $play.Decibels = $savedPlayDb; $play.Muted = $savedPlayMute
    $mic.Muted = $true
    $muted = Get-BoardStatus
    if ($muted.CaptureGainQ15 -ne 0) { throw 'Capture mute did not reach firmware.' }
    $silence = & (Join-Path $PSScriptRoot 'test-usb-audio.ps1') -Seconds 2 -Amplitude 0 | ConvertFrom-Json
    if ($silence.CapturePeak -gt 2) { throw 'Capture mute did not produce silence.' }
    $result.CaptureMute = 'passed'; $result.MutedCapturePeak = $silence.CapturePeak
    $mic.Muted = $false; $mic.Decibels = -12
    $reduced = Get-BoardStatus
    $mic.Decibels = 0
    $full = Get-BoardStatus
    if ([Math]::Abs($reduced.CaptureGainQ15 - 8231) -gt 50 -or $full.CaptureGainQ15 -ne 32768) { throw 'Capture volume failed.' }
    $result.CaptureGainAtMinus12Db = $reduced.CaptureGainQ15
    $result.CaptureGainAt0Db = $full.CaptureGainQ15
} finally {
    try { $play.Decibels = $savedPlayDb; $play.Muted = $savedPlayMute }
    finally {
        try { $mic.Decibels = $savedMicDb; $mic.Muted = $savedMicMute }
        finally { $play.Dispose(); $mic.Dispose() }
    }
}
$result | ConvertTo-Json

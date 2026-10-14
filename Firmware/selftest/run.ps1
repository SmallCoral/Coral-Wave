param(
    [ValidateSet('Control', 'Audio')][string]$Mode = 'Control',
    [ValidateSet('Software', 'Hardware')][string]$Transport = 'Software',
    [ValidateSet('Direct12288', 'Pll12')][string]$CodecClock = 'Direct12288',
    [ValidateSet(0, 1, 2)][int]$AudioCommand = 0,
    [string]$JLink = 'C:\Program Files\SEGGER\JLink\JLink.exe',
    [int]$Speed = 1000
)
$ErrorActionPreference = 'Stop'
$out = Join-Path $PSScriptRoot 'build'
$name = $Mode.ToLowerInvariant()
if ($Transport -eq 'Hardware') { $name += '-hardware' }
if ($CodecClock -eq 'Pll12') { $name += '-pll12' }
$manifest = Join-Path $out "$name.json"
if (-not (Test-Path $manifest)) { throw "Build $Mode first with build.ps1." }
$artifact = Get-Content -Raw $manifest | ConvertFrom-Json
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$script = Join-Path $out "$name-$stamp.jlink"
$log = Join-Path $out "$name-$stamp.log"
$raw = Join-Path $out "$name-$stamp-results.bin"
$commands = @('r', 'h', "loadbin `"$($artifact.Binary)`", 0x20000000", 'wreg MSP, 0x20008000',
    "SetPC $($artifact.Entry)", 'g', 'sleep 2500', 'h')
if ($Mode -eq 'Audio' -and $AudioCommand -ne 0) {
    # Audible output remains limited in firmware and auto-mutes after 10 s.
    $commands += @("w4 0x2000705C, $AudioCommand", 'g', 'sleep 12000', 'h')
}
$commands += @('mem32 0x20007000, 0x20')
if ($Transport -eq 'Hardware') { $commands += 'mem32 0x42001400, 0xC' }
if ($CodecClock -eq 'Pll12') { $commands += 'mem32 0x20007100, 0x4' }
$commands += @("savebin `"$raw`", 0x20007000, 0x80", 'q')
$commands | Set-Content -Encoding ascii $script
& $JLink -device ATSAMD21E18 -if SWD -speed $Speed -autoconnect 1 -ExitOnError 1 -CommanderScript $script | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "J-Link failed. See $log" }
if (-not (Test-Path $raw)) { throw 'Result mailbox was not saved.' }
$bytes = [IO.File]::ReadAllBytes($raw)
if ($bytes.Length -ne 128) { throw 'Unexpected mailbox size.' }
$words = for ($i = 0; $i -lt 32; $i++) { [BitConverter]::ToUInt32($bytes, $i * 4) }
if ($words[0] -ne 0x574D3837 -or $words[1] -ne 2) { throw 'Firmware did not initialize its result mailbox.' }
$errors = @('none', 'clock startup', 'I2C bus stuck', 'codec NACK', 'audio clock missing/out of range',
    'I2S receive/initialization', 'DMA transfer', 'HardFault', 'SERCOM error/timeout')
$addresses = @()
for ($a = 8; $a -lt 0x78; $a++) {
    if ($words[27 + [int][Math]::Floor($a / 32)] -band ([uint32]1 -shl ($a % 32))) {
        $addresses += ('0x{0:X2}' -f $a)
    }
}
$decoded = [ordered]@{
    Mode = $Mode; Transport = $Transport; CodecClock = $CodecClock; Stage = $words[2]; ErrorCode = $words[3]; Error = $errors[$words[3]];
    DeviceId = ('0x{0:X8}' -f $words[4]); BusIdle = $words[6]; LastAckMask = $words[7];
    SuccessfulWrites = $words[8]; FailedRegister = ('0x{0:X}' -f $words[9]);
    ApproximateLrclkHz = $words[10]; BclkTransitionsObserved = $words[11]; RxWords = $words[12];
    AdcMin = @([BitConverter]::ToInt32($bytes, 52), [BitConverter]::ToInt32($bytes, 56));
    AdcMax = @([BitConverter]::ToInt32($bytes, 60), [BitConverter]::ToInt32($bytes, 64));
    TxUnderruns = $words[19]; RxOverruns = $words[20]; DmaBlocks = $words[21];
    ElapsedMs = $words[22]; Done = $words[25]; AddressAckCount = $words[26];
    RespondingAddresses = $addresses; GpioDriveCheck = $words[31]; Log = $log
}
$json = $decoded | ConvertTo-Json -Depth 3
$json | Set-Content -Encoding utf8 (Join-Path $out "$name-$stamp-results.json")
Write-Output $json
if ($words[3] -ne 0) { throw "Self-test stopped: $($errors[$words[3]])." }
if ($Mode -eq 'Control' -and ($words[2] -ne 7 -or $words[25] -ne 1)) {
    throw 'Control self-test did not complete.'
}
if ($Mode -eq 'Audio' -and ($words[2] -ne 6 -or $words[12] -eq 0 -or $words[21] -eq 0)) {
    throw 'Audio self-test did not establish receive and transmit activity.'
}

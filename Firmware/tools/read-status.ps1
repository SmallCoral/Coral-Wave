param(
  [string]$JLink = 'C:\Program Files\SEGGER\JLink\JLink.exe',
  [string]$OutputFile = ''
)
$ErrorActionPreference = 'Stop'
$out = Join-Path (Split-Path $PSScriptRoot) 'usb-audio/build'
New-Item -ItemType Directory -Force $out | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$script = Join-Path $out "status-$stamp.jlink"
$raw = Join-Path $out "status-$stamp.bin"
$log = Join-Path $out "status-$stamp.log"
@('connect', "savebin `"$raw`", 0x20007000, 0xAC", 'q') | Set-Content -Encoding ascii $script
& $JLink -device ATSAMD21E18 -if SWD -speed 1000 -autoconnect 1 -ExitOnError 1 -CommanderScript $script *> $log
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $raw)) { throw "Status read failed. See $log" }
$names = @('Magic','Version','Stage','Error','Millis','Mounted','MountCount',
  'PlaybackActive','CaptureActive','PlaybackBytes','CaptureBytes','ControlRequests','CodecAck','SuspendCount',
  'LrclkHz','DmaTxBlocks','DmaRxBlocks','TxFrames','RxFrames',
  'PlaybackUnderflows','CaptureOverflows','DmaErrors','I2sTxUnderruns','I2sRxOverruns','PlayFifoFrames',
  'CapturePeak','CaptureNonzero','CodecWrites','FailedRegister','DacPeak','Feedback16_16',
  'LeftAdcPeak','RightAdcPeak','PlaybackOverflows','CaptureDrops',
  'PlaybackGainLeftQ15','PlaybackGainRightQ15','CaptureGainQ15','CurrentDacPeak',
  'FaultPc','FaultLr','FaultXpsr','FaultExceptionReturn')
$bytes = [IO.File]::ReadAllBytes($raw)
$decoded = [ordered]@{}
for ($i = 0; $i -lt $names.Count; $i++) { $decoded[$names[$i]] = [BitConverter]::ToUInt32($bytes, 4*$i) }
if ($decoded.Magic -ne 0x434F524C) { throw 'Coral-Wave firmware mailbox not found.' }
$json = $decoded | ConvertTo-Json
if ($OutputFile) { $json | Set-Content -Encoding utf8 $OutputFile }
$json

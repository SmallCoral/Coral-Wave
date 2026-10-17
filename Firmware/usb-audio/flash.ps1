param(
  [string]$JLink = 'C:\Program Files\SEGGER\JLink\JLink.exe',
  [string]$Binary = ''
)
$ErrorActionPreference = 'Stop'
$out = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force $out | Out-Null
if (-not $Binary) {
  $Binary = Join-Path $out 'Coral-Wave.bin'
  if (-not (Test-Path -LiteralPath $Binary)) {
    $Binary = Join-Path (Split-Path $PSScriptRoot) 'releases/Coral-Wave-USB.bin'
  }
}
if (-not (Test-Path -LiteralPath $Binary)) { throw 'Firmware binary not found. Run build.ps1 first.' }
$Binary = (Resolve-Path -LiteralPath $Binary).Path
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $out "before-usb-$stamp.bin"
$script = Join-Path $out "flash-$stamp.jlink"
$log = Join-Path $out "flash-$stamp.log"
@('r', 'h', "savebin `"$backup`", 0x00000000, 0x40000",
  "loadbin `"$Binary`", 0x00000000", "verifybin `"$Binary`", 0x00000000", 'r', 'g', 'sleep 5000',
  'mem32 0x20007000, 0x2B', 'q') | Set-Content -Encoding ascii $script
& $JLink -device ATSAMD21E18 -if SWD -speed 1000 -autoconnect 1 -ExitOnError 1 -CommanderScript $script | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "Flash/verification failed. See $log" }
Write-Output "Flash verified. Previous flash saved locally at $backup"

param([string]$Toolchain='', [string]$Python='python')
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'bootloader/build.ps1') -Toolchain $Toolchain
& (Join-Path $PSScriptRoot 'usb-audio/build.ps1') -Toolchain $Toolchain
& (Join-Path $PSScriptRoot 'host/build.ps1')
foreach ($name in @('Coral-Wave-Control.exe','Coral-Wave-CLI.exe')) {
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot "host/build/$name") -Destination (Join-Path $PSScriptRoot "releases/$name") -Force
}
& $Python (Join-Path $PSScriptRoot 'tools/package.py')
if ($LASTEXITCODE) { throw 'Firmware packaging failed' }

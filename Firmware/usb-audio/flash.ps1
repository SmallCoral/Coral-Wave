param([Alias('Binary')][string]$Firmware='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
if (-not $Firmware) { $Firmware=Join-Path $root 'releases/Coral-Wave.cwfw' }
if ([IO.Path]::GetExtension($Firmware) -ne '.cwfw') { throw 'Select a .cwfw USB update package. Raw .bin files are not USB update packages.' }
$cli=Join-Path $root 'host/build/Coral-Wave-CLI.exe'
if (-not (Test-Path $cli)) { $cli=Join-Path $root 'releases/Coral-Wave-CLI.exe' }
if (-not (Test-Path $cli)) { & (Join-Path $root 'host/build.ps1'); $cli=Join-Path $root 'host/build/Coral-Wave-CLI.exe' }
& $cli --flash (Resolve-Path -LiteralPath $Firmware).Path
if ($LASTEXITCODE) { throw 'USB update failed. The recovery interface can accept the package again.' }

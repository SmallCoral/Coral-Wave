param(
    [ValidateSet('Control', 'Audio')][string]$Mode = 'Control',
    [ValidateSet('Software', 'Hardware')][string]$Transport = 'Software',
    [ValidateSet('Direct12288', 'Pll12')][string]$CodecClock = 'Direct12288',
    [int]$I2cHalfUs = 25,
    [string]$Toolchain = ''
)
$ErrorActionPreference = 'Stop'
if (-not $Toolchain) {
    $gcc = Get-Command arm-none-eabi-gcc.exe -ErrorAction SilentlyContinue
    $Toolchain = if ($gcc) { Split-Path $gcc.Source } else {
        Join-Path $env:LOCALAPPDATA 'Programs/STM32SensorTools/bin'
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $Toolchain 'arm-none-eabi-gcc.exe'))) {
    throw 'Arm GNU Toolchain not found. Pass -Toolchain with its bin directory.'
}
if ($I2cHalfUs -lt 5 -or $I2cHalfUs -gt 100) { throw 'I2cHalfUs must be 5..100.' }
$selftestRoot = $PSScriptRoot
$deps = Join-Path $selftestRoot 'deps'
$out = Join-Path $selftestRoot 'build'
New-Item -ItemType Directory -Force $deps, $out | Out-Null
$dfpInclude = Join-Path $deps 'samd21\samd21a\include'
if (-not (Test-Path (Join-Path $dfpInclude 'samd21e18a.h'))) {
    $pack = Join-Path $deps 'samd21.zip'
    Invoke-WebRequest 'https://packs.download.microchip.com/Microchip.SAMD21_DFP.3.8.270.atpack' -OutFile $pack
    Expand-Archive -LiteralPath $pack -DestinationPath (Join-Path $deps 'samd21') -Force
}
$cmsis = Join-Path $deps 'cmsis'
New-Item -ItemType Directory -Force $cmsis | Out-Null
foreach ($f in @('core_cm0plus.h', 'cmsis_gcc.h', 'cmsis_version.h', 'cmsis_compiler.h', 'mpu_armv7.h')) {
    $target = Join-Path $cmsis $f
    if (-not (Test-Path -LiteralPath $target)) {
        Invoke-WebRequest ('https://raw.githubusercontent.com/ARM-software/CMSIS_5/5.9.0/CMSIS/Core/Include/' + $f) -OutFile $target
    }
}
$name = $Mode.ToLowerInvariant()
if ($Transport -eq 'Hardware') { $name += '-hardware' }
if ($CodecClock -eq 'Pll12') { $name += '-pll12' }
$elf = Join-Path $out "$name.elf"
$bin = Join-Path $out "$name.bin"
$control = if ($Mode -eq 'Control') { 1 } else { 0 }
$hardware = if ($Transport -eq 'Hardware') { 1 } else { 0 }
$pll12 = if ($CodecClock -eq 'Pll12') { 1 } else { 0 }
$compilerArgs = @('-mcpu=cortex-m0plus', '-mthumb', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
    '-ffreestanding', '-fno-builtin', '-ffunction-sections', '-fdata-sections', '-nostdlib',
    "-DWM8978_CONTROL_ONLY=$control", "-DWM8978_HARDWARE_I2C=$hardware", "-DWM8978_PLL_FROM_12M=$pll12", "-DI2C_HALF_US=$I2cHalfUs", '-I', $cmsis, '-I', $dfpInclude,
    '-Wl,--gc-sections', "-Wl,-Map=$out\$name.map", '-T', (Join-Path $selftestRoot 'ram.ld'),
    (Join-Path $selftestRoot 'main.c'), '-lgcc', '-o', $elf)
& (Join-Path $Toolchain 'arm-none-eabi-gcc.exe') @compilerArgs
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
& (Join-Path $Toolchain 'arm-none-eabi-objcopy.exe') -O binary $elf $bin
if ($LASTEXITCODE -ne 0) { throw 'objcopy failed.' }
$entry = & (Join-Path $Toolchain 'arm-none-eabi-nm.exe') $elf | Where-Object { $_ -match ' T Reset_Handler$' }
if (-not $entry) { throw 'Reset_Handler was not found.' }
$entryAddress = ($entry -split '\s+')[0]
$artifact = [ordered]@{
    Mode = $Mode; Transport = $Transport; CodecClock = $CodecClock; Elf = $elf; Binary = $bin; Entry = "0x$entryAddress";
    Mailbox = '0x20007000'; MailboxBytes = 128; I2cHalfUs = $I2cHalfUs
}
$artifact | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $out "$name.json")
& (Join-Path $Toolchain 'arm-none-eabi-size.exe') $elf
Write-Output "Built $Mode RAM self-test: $bin"

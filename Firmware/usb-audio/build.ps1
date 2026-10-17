param(
  [string]$Toolchain = '',
  [ValidateSet(0, 1, 2)][int]$UsbDebug = 0
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
$firmwareRoot = Split-Path $PSScriptRoot
$tinyusb = Join-Path $firmwareRoot 'third_party\tinyusb\src'
$out = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force $out | Out-Null
$sourceFiles = @((Join-Path $PSScriptRoot 'main.c'), (Join-Path $PSScriptRoot 'usb_descriptors.c'),
  (Join-Path $PSScriptRoot 'audio.c'),
  (Join-Path $tinyusb 'tusb.c'), (Join-Path $tinyusb 'common\tusb_fifo.c'),
  (Join-Path $tinyusb 'device\usbd.c'), (Join-Path $tinyusb 'class\audio\audio_device.c'),
  (Join-Path $tinyusb 'portable\microchip\samd\dcd_samd.c'))
$objects = @()
$common = @('-mcpu=cortex-m0plus', '-mthumb', '-std=c11', '-Os', '-g3', '-Wall', '-Wextra',
  '-ffunction-sections', '-fdata-sections', '-D__SAMD21E18A__', "-DCORAL_USB_DEBUG=$UsbDebug", '-I', $PSScriptRoot,
  '-I', $tinyusb, '-I', (Join-Path $firmwareRoot 'third_party\samd21\include'),
  '-I', (Join-Path $firmwareRoot 'third_party\cmsis'))
foreach ($source in $sourceFiles) {
  $object = Join-Path $out (([IO.Path]::GetFileNameWithoutExtension($source)) + '.o')
  & (Join-Path $Toolchain 'arm-none-eabi-gcc.exe') @common -c $source -o $object
  if ($LASTEXITCODE -ne 0) { throw "Compile failed: $source" }
  $objects += $object
}
$elf = Join-Path $out 'Coral-Wave.elf'
$linkArgs = @('-mcpu=cortex-m0plus', '-mthumb', '-nostartfiles', '--specs=nano.specs', '--specs=nosys.specs',
  '-Wl,--gc-sections', "-Wl,-Map=$out\Coral-Wave.map", '-T', (Join-Path $PSScriptRoot 'flash.ld')) + $objects + @('-o', $elf)
& (Join-Path $Toolchain 'arm-none-eabi-gcc.exe') @linkArgs
if ($LASTEXITCODE -ne 0) { throw 'Link failed.' }
foreach ($format in @('binary', 'ihex')) {
  $ext = if ($format -eq 'binary') { 'bin' } else { 'hex' }
  & (Join-Path $Toolchain 'arm-none-eabi-objcopy.exe') -O $format $elf (Join-Path $out "Coral-Wave.$ext")
  if ($LASTEXITCODE -ne 0) { throw 'objcopy failed.' }
}
& (Join-Path $Toolchain 'arm-none-eabi-size.exe') $elf

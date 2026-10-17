param([string]$Toolchain = '')
$ErrorActionPreference = 'Stop'
if (-not $Toolchain) {
  $gcc = Get-Command arm-none-eabi-gcc.exe -ErrorAction SilentlyContinue
  $Toolchain = if ($gcc) { Split-Path $gcc.Source } else { Join-Path $env:LOCALAPPDATA 'Programs/STM32SensorTools/bin' }
}
$root = Split-Path $PSScriptRoot
$tiny = Join-Path $root 'third_party/tinyusb/src'
$out = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force $out | Out-Null
$sources = @("$PSScriptRoot/main.c", "$PSScriptRoot/usb_descriptors.c",
  "$root/common/board.c", "$root/common/protocol.c", "$root/common/flash.c",
  "$tiny/tusb.c", "$tiny/common/tusb_fifo.c", "$tiny/device/usbd.c",
  "$tiny/class/hid/hid_device.c", "$tiny/portable/microchip/samd/dcd_samd.c")
$flags = @('-mcpu=cortex-m0plus', '-mthumb', '-std=c11', '-Os', '-g3', '-Wall', '-Wextra',
  '-ffunction-sections', '-fdata-sections', '-D__SAMD21E18A__', '-I', $PSScriptRoot,
  '-I', "$root/common", '-I', $tiny, '-I', "$root/third_party/samd21/include", '-I', "$root/third_party/cmsis")
$objects = @()
foreach ($source in $sources) {
  $obj = Join-Path $out (([IO.Path]::GetFileNameWithoutExtension($source)) + '.o')
  & "$Toolchain/arm-none-eabi-gcc.exe" @flags -c $source -o $obj
  if ($LASTEXITCODE) { throw "Compile failed $source" }
  $objects += $obj
}
$elf = "$out/Coral-Wave-Bootloader.elf"
& "$Toolchain/arm-none-eabi-gcc.exe" -mcpu=cortex-m0plus -mthumb -nostartfiles --specs=nano.specs --specs=nosys.specs '-Wl,--gc-sections' "-Wl,-Map=$out/Coral-Wave-Bootloader.map" -T "$PSScriptRoot/flash.ld" @objects -o $elf
if ($LASTEXITCODE) { throw 'Bootloader link failed' }
& "$Toolchain/arm-none-eabi-objcopy.exe" -O binary $elf "$out/Coral-Wave-Bootloader.bin"
if ($LASTEXITCODE) { throw 'objcopy failed' }
& "$Toolchain/arm-none-eabi-size.exe" $elf

param([string]$JLink='C:\Program Files\SEGGER\JLink\JLink.exe', [string]$Toolchain='', [switch]$ReplaceBootloader)
$ErrorActionPreference='Stop'
$firmware=Split-Path $PSScriptRoot
$image=Join-Path $firmware 'releases/Coral-Wave-First-Install.bin'
if (-not (Test-Path -LiteralPath $image)) { throw 'Build and package the firmware first.' }
if (-not $Toolchain) { $Toolchain=Join-Path $env:LOCALAPPDATA 'Programs/STM32SensorTools/bin' }
$out=Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force $out | Out-Null
& "$Toolchain/arm-none-eabi-gcc.exe" -mcpu=cortex-m0plus -mthumb -std=c11 -Os -nostdlib -D__SAMD21E18A__ -I "$firmware/third_party/samd21/include" -I "$firmware/third_party/cmsis" -Ttext=0x20000000 '-Wl,-e,Reset_Handler' "$PSScriptRoot/protect.c" -o "$out/protect.elf"
if ($LASTEXITCODE) { throw 'Protect helper build failed' }
& "$Toolchain/arm-none-eabi-objcopy.exe" -O binary "$out/protect.elf" "$out/protect.bin"
if ($LASTEXITCODE) { throw 'Protect helper conversion failed' }
$entry=(& "$Toolchain/arm-none-eabi-nm.exe" "$out/protect.elf" | Select-String ' T Reset_Handler$').ToString().Split(' ')[0]
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$backup=Join-Path $out "before-bootloader-$stamp.bin"
$fuses=Join-Path $out "before-bootloader-$stamp-user.bin"
$script=Join-Path $out "provision-$stamp.jlink"
$log=Join-Path $out "provision-$stamp.log"
@('r','h',"savebin `"$backup`", 0, 0x40000", "savebin `"$fuses`", 0x00804000, 0x40",'q') | Set-Content -Encoding ascii $script
& $JLink -device ATSAMD21E18 -if SWD -speed 1000 -autoconnect 1 -ExitOnError 1 -CommanderScript $script *> $log
if ($LASTEXITCODE -or -not (Test-Path $fuses) -or (Get-Item $backup).Length -ne 262144) { throw "Backup failed. See $log" }
$user=[IO.File]::ReadAllBytes($fuses)
if (($user[0] -band 7) -ne 7) {
  if (-not $ReplaceBootloader) { throw 'BOOTPROT is already enabled. Use the USB updater instead of provisioning again.' }
  # Explicit SWD maintenance only; the USB protocol cannot touch these fuses.
  & "$Toolchain/arm-none-eabi-gcc.exe" -mcpu=cortex-m0plus -mthumb -std=c11 -Os -nostdlib -D__SAMD21E18A__ -DCW_BOOTPROTECTION=7 -I "$firmware/third_party/samd21/include" -I "$firmware/third_party/cmsis" -Ttext=0x20000000 '-Wl,-e,Reset_Handler' "$PSScriptRoot/protect.c" -o "$out/unprotect.elf"
  if ($LASTEXITCODE) { throw 'Unprotect helper build failed' }
  & "$Toolchain/arm-none-eabi-objcopy.exe" -O binary "$out/unprotect.elf" "$out/unprotect.bin"
  if ($LASTEXITCODE) { throw 'Unprotect helper conversion failed' }
  $unlockEntry=(& "$Toolchain/arm-none-eabi-nm.exe" "$out/unprotect.elf" | Select-String ' T Reset_Handler$').ToString().Split(' ')[0]
  $unlocked=Join-Path $out "unprotected-$stamp-user.bin"
  @('r','h',"loadbin `"$out/unprotect.bin`", 0x20000000",'wreg MSP, 0x20008000',"SetPC 0x$unlockEntry",'g','sleep 300','h',"savebin `"$unlocked`", 0x00804000, 0x40",'r','h','q') | Set-Content -Encoding ascii $script
  & $JLink -device ATSAMD21E18 -if SWD -speed 1000 -autoconnect 1 -ExitOnError 1 -CommanderScript $script *> "$out/unprotect-$stamp.log"
  if ($LASTEXITCODE) { throw 'BOOTPROT unlock failed' }
  $expected=[byte[]]$user.Clone();$expected[0]=($expected[0] -band 248) -bor 7
  if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($unlocked)) -ne [Convert]::ToBase64String($expected)) { throw 'BOOTPROT unlock verification failed' }
}
@('r','h',"loadbin `"$image`", 0", "verifybin `"$image`", 0",'r','g','sleep 7000','mem32 0x20007000, 0x2B','q') | Set-Content -Encoding ascii $script
& $JLink -device ATSAMD21E18 -if SWD -speed 1000 -autoconnect 1 -ExitOnError 1 -CommanderScript $script | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE) { throw "First-install verification failed. Backup $backup" }
# Verify the relocated application through USB before protecting the bootloader.
$cli=Join-Path $firmware 'host/build/Coral-Wave-CLI.exe'
$state=& $cli --status
if ($LASTEXITCODE) { throw 'Application did not reconnect over USB. BOOTPROT was not changed.' }
$s=$state | ConvertFrom-Json
if ($s.info.Mode -ne 0 -or $s.status[1] -ne 3 -or $s.status[2] -ne 0 -or $s.status[3] -ne 1) { throw "Board is not healthy: $state" }
$verify=Join-Path $out "after-bootloader-$stamp-user.bin"
@('r','h',"loadbin `"$out/protect.bin`", 0x20000000",'wreg MSP, 0x20008000',"SetPC 0x$entry",'g','sleep 300','h','mem32 0x20007000, 3',"savebin `"$verify`", 0x00804000, 0x40",'r','g','sleep 7000','q') | Set-Content -Encoding ascii $script
& $JLink -device ATSAMD21E18 -if SWD -speed 1000 -autoconnect 1 -ExitOnError 1 -CommanderScript $script | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE) { throw "BOOTPROT helper failed. See $log" }
$after=[IO.File]::ReadAllBytes($verify)
$user[0]=($user[0] -band 248) -bor 1
if ([Convert]::ToBase64String($after) -ne [Convert]::ToBase64String($user)) { throw 'Fuse verification failed. Keep the saved user-row backup.' }
Write-Output "Provisioned USB bootloader, verified BOOTPROT=1 (16 KiB). Backups $backup and $fuses"

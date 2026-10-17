# 固件

`usb-audio` 是写入 Flash 的录放音固件。Y1 为主控提供 12 MHz 参考，Y2 给 WM8978 提供 12.288 MHz MCLK。USB 与 I²S 已接通，格式固定为 48 kHz、16 位，播放双声道，采集单声道。

耳机接板上 3.5 mm 耳机口，录音使用板载麦克风。Windows 输出选择 **Coral-Wave Playback**，输入选择 **Coral-Wave Microphone**。耳机固定模拟增益为 -18 dB，初次播放从较低系统音量开始。

在项目根目录运行下面两条命令。需要 Arm GNU Toolchain 和 SEGGER J-Link，工具不在默认位置时可传入 `-Toolchain`、`-JLink`。

```powershell
./Firmware/usb-audio/build.ps1 -Toolchain 'C:/path/to/arm-gnu/bin'
./Firmware/usb-audio/flash.ps1
```

编译产物在 `usb-audio/build`。也可直接烧录仓库附带的固件，烧录前会在本地备份旧 Flash，并在写入后回读校验。

```powershell
./Firmware/usb-audio/flash.ps1 -Binary ./Firmware/releases/Coral-Wave-USB.bin
./Firmware/tools/test-usb-audio.ps1
```

测试脚本默认播放一秒低音量正弦波，同时采集麦克风。下面几条命令可录音、回放和检查系统音量控制。控制测试结束后会恢复原设置。

```powershell
./Firmware/tools/test-usb-audio.ps1 -Seconds 12 -Amplitude 0 -CaptureFile ./mic-test.wav
./Firmware/tools/test-usb-audio.ps1 -Seconds 12 -PlaybackFile ./mic-test.wav
./Firmware/tools/test-controls.ps1
./Firmware/tools/read-status.ps1
```

录音文件默认只保存在本地。当前开发 VID 为 `0xCAFE`，PID 为 `0xC0A3`，USB 设备版本为 `0x0201`。正式产品需要获授权的 VID/PID。

`selftest` 保留 WM8978 的 [RAM 自检](selftest/README.md)，运行它会暂时停掉 USB，复位后恢复 Flash 固件。依赖版本和本地改动见 [third_party](third_party/README.md)。

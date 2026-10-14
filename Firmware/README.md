# 固件

`usb-audio` 是写入 Flash 的 USB 声卡程序，使用 Y1 的 12 MHz 有源时钟。Windows 会显示 Coral-Wave Playback 和 Coral-Wave Microphone，格式固定为 48 kHz、16 位，播放双声道，采集单声道。

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

最后一条命令在 Windows 上测试一秒双向 USB 传输，只打开 Coral-Wave。当前程序丢弃播放 PCM、发送零样本，尚未连接 WM8978 的 I²S 音频。静音和音量控制会保存设置，尚未作用到模拟输出。VID `0xCAFE`、PID `0xC0A1` 用于开发，产品发行前需使用获授权的 VID/PID。

`selftest` 保留 WM8978 的 [RAM 自检](selftest/README.md)，运行它会暂时停掉 USB，复位后恢复 Flash 固件。依赖版本和本地改动见 [third_party](third_party/README.md)。

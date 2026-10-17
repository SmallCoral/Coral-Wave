# 固件与上位机

日常使用直接打开 [Coral-Wave-Control.exe](releases/Coral-Wave-Control.exe)。板子接 USB-C 后可调音、导入导出预设，或选择 [Coral-Wave.cwfw](releases/Coral-Wave.cwfw) 更新固件。首次安装引导程序之后，烧录不需要 J-Link。

当前格式为 **48 kHz、16 位，双声道播放和单声道采集**。Windows 输出选择 Coral-Wave Playback，输入选择 Coral-Wave Microphone。耳机默认模拟增益为 -18 dB，首次播放从较低系统音量开始。

| 目录 | 内容 |
| --- | --- |
| [host](host/) | Windows 上位机、CLI、MCP 与接口测试 |
| [usb-audio](usb-audio/) | 录放音和音频参数控制固件 |
| [bootloader](bootloader/) | USB 恢复引导程序、首次安装工具 |
| [common](common/) | USB 控制协议、时钟、Flash 写入 |
| [releases](releases/) | EXE、USB 升级包、首次安装镜像和校验清单 |
| [tools](tools/) | 打包、录放音测试、SWD 诊断工具 |

上位机操作见 [使用说明](host/README.md)，AI 接入见 [MCP](../Documentation/MCP.md)。设备开发 VID 为 `0xCAFE`，应用 PID 为 `0xC0A4`，引导 PID 为 `0xC0B0`。正式产品需要获授权的标识。

从源码编译需要 Arm GNU Toolchain、.NET Framework 的 C# 编译器和 Python 3。关闭已打开的发布版上位机后运行下面命令。工具路径不在默认位置时可传 `-Toolchain` 和 `-Python`。

```powershell
./Firmware/build.ps1 -Toolchain 'C:/path/to/arm-gnu/bin'
```

新焊的空白主控先接 USB 供电和 J-Link，运行一次下面的脚本。它会备份 Flash 和用户配置区，安装引导及应用，验证 USB 通信后保护前 16 KB。已经装过的板子使用上位机更新即可。

```powershell
./Firmware/bootloader/provision.ps1
./Firmware/usb-audio/flash.ps1 -Firmware ./Firmware/releases/Coral-Wave.cwfw
```

`Coral-Wave-USB.bin` 和 HEX 均从 `0x4000` 开始，不能按旧固件写到地址零。`Coral-Wave-First-Install.bin` 仅供首次 SWD 安装。USB 升级会保留引导区和音频设置，失败后可重试或使用恢复烧录。

Y1 为主控提供 12 MHz 参考，Y2 给 WM8978 提供 12.288 MHz MCLK。[selftest](selftest/README.md) 保留早期 RAM 自检，运行时会暂停 USB。依赖授权及本地补丁见 [third_party](third_party/README.md)。

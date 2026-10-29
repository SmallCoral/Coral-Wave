# 固件编译与烧录

以下 PowerShell 命令均在仓库根目录运行。
只使用预编译上位机时，直接阅读[使用方法](usage.md)，不需要安装编译工具。

## 准备开发环境

| 工具 | 用途与要求 |
| --- | --- |
| Windows、PowerShell | 运行仓库的构建、烧录脚本与上位机 |
| Arm GNU Toolchain | 需要 `arm-none-eabi-gcc`、`objcopy`、`nm`、`size`，以及链接脚本使用的 newlib-nano 支持 |
| .NET Framework | 上位机按 4.8 运行环境说明；构建脚本默认使用 `Framework64/v4.0.30319/csc.exe` |
| Python 3 | 固件打包与测试；基础构建只使用标准库 |
| SEGGER J-Link | 仅首次 SWD 安装及 SWD 诊断需要；默认程序位置为 `C:/Program Files/SEGGER/JLink/JLink.exe` |

本次复核实际使用 Arm GNU 14.3.Rel1 / GCC 14.3.1 完成编译，见[核验记录](documentation-review.md)。
没有验证其他工具链版本或 Linux/macOS 构建流程。

## 编译完整工程

关闭占用待覆盖 EXE 的发布版上位机。
已将 Arm GNU 和 Python 加入 PATH 时运行：

```powershell
python --version
arm-none-eabi-gcc --version
./Firmware/build.ps1 -Python python
```

如果工具没有加入 PATH，用实际安装路径替换下例的两个路径：

```powershell
./Firmware/build.ps1 -Toolchain 'C:/Tools/arm-gnu/bin' -Python 'C:/Tools/Python/python.exe'
```

顶层脚本依次编译引导程序、USB 音频应用和 Windows 上位机，再更新 `Firmware/releases/`。
各模块的中间输出放在自身 `build/` 下。
构建的最后一步输出 `Packaged 3.0.0` 以及应用、引导程序的字节数；发布内容和哈希以生成的 `manifest.json` 为准。
这套流程未承诺不同工具链生成逐字节相同的文件。

只修改上位机时可单独运行：

```powershell
./Firmware/host/build.ps1
```

该命令只更新 `Firmware/host/build/`，不会自动替换 `Firmware/releases/` 的 EXE。
需要打包一致的完整发布内容时，运行顶层脚本。

## 识别烧录文件

| 文件，均位于 `Firmware/releases/` | 用途 |
| --- | --- |
| `Coral-Wave.cwfw` | 日常 USB 更新包，包含型号、地址、长度、版本和 CRC 校验 |
| `Coral-Wave-First-Install.bin` | 空白主控首次 SWD 安装的组合镜像，由安装脚本从地址零写入 |
| `Coral-Wave-USB.bin` / `.hex` | 链接到 `0x4000` 的应用；原始 BIN 不能按地址零烧录 |
| `Coral-Wave-Bootloader.bin` | 独立引导程序镜像；日常 USB 更新不使用此文件 |
| `Coral-Wave-Control.exe` / `Coral-Wave-CLI.exe` | 图形上位机与命令行/MCP 服务 |
| `manifest.json` | 发布版本、每份文件大小与 SHA-256 |

先运行无需设备的包检查：

```powershell
./Firmware/releases/Coral-Wave-CLI.exe --validate ./Firmware/releases/Coral-Wave.cwfw
```

有效包返回 JSON，包含 `valid: true`、版本、应用字节数和 CRC32。
这只证明包结构及校验通过，不执行烧录，也不验证固件来源。

## 首次通过 SWD 安装

此步骤会写入主控 Flash 和引导保护配置，只用于新焊的空白主控。
已有引导程序的板子使用下一节的 USB 更新。

1. 用 USB-C 给板子供电。
2. 按 [J2 针序](hardware.md#j2-swd)连接 J-Link 的 VTref、SWDIO、SWCLK 和 GND。
3. 完成构建，确认组合镜像存在。
4. 运行安装脚本；工具链目录替换成实际位置。

```powershell
./Firmware/bootloader/provision.ps1 -Toolchain 'C:/Tools/arm-gnu/bin'
```

J-Link 安装位置不同时，另传 `-JLink 'C:/实际位置/JLink.exe'`。
脚本先备份 Flash 和用户配置区，再写入并回读校验组合镜像，检查应用 USB 状态，最后设置前 16 KB 的引导保护。
备份和日志位于 `Firmware/bootloader/build/`，保留这些文件。
成功时输出 `Provisioned USB bootloader, verified BOOTPROT=1 (16 KiB)`。
已有 BOOTPROT 的设备会被默认拒绝；不要将脚本的 `-ReplaceBootloader` 维护选项当作常规更新步骤。

## 通过 USB 更新与恢复

正常更新前停止电脑上的播放和录音。
在上位机选择 `.cwfw` 后执行烧录，或运行：

```powershell
./Firmware/usb-audio/flash.ps1 -Firmware ./Firmware/releases/Coral-Wave.cwfw
```

脚本优先使用 `Firmware/host/build/` 中的 CLI，找不到时使用 `Firmware/releases/`。
更新会暂时断开音频设备，结束后应用重新枚举；已保存的音频设置保留，尚未保存的设置在重启后丢失。
CLI 成功输出包含 `flashed: true` 的 JSON。

应用无响应但引导程序仍可运行时，先启动恢复等待，再拔插板子的 USB-C：

```powershell
./Firmware/releases/Coral-Wave-CLI.exe --recover ./Firmware/releases/Coral-Wave.cwfw
```

也可在 GUI 使用“等待拔插并恢复烧录”。
主机最多等待 45 秒捕获上电恢复接口；多块板子同时连接时按[CLI 说明](usage.md#使用-cli)指定序列号。
恢复流程会更新应用，不能修复损坏的引导程序或未建立的硬件时钟。
协议细节与恢复窗口见 [USB 控制协议](../Documentation/USB-Protocol.md)。

早期 RAM 自检单独见 [selftest](../Firmware/selftest/README.md)。
它会暂停正常 USB 应用，不是日常安装或 USB 更新步骤。

# 固件与上位机

本目录包含 Coral-Wave 固件、USB 引导程序、Windows 上位机和测试工具。
预编译文件位于 [releases](releases/)，版本及文件校验见 [manifest.json](releases/manifest.json)。

## 开始使用

- 已有板子：[播放、录音与调音](../docs/usage.md)。
- 开发或安装：[编译、首次 SWD 安装与 USB 更新](../docs/firmware.md)。
- AI 客户端：[MCP 接入](../Documentation/MCP.md)。
- 复测与诊断：[测试说明](../docs/testing.md)、[早期 RAM 自检](selftest/README.md)。

## 目录

| 目录 | 内容 |
| --- | --- |
| [host](host/README.md) | GUI、CLI、MCP 与接口测试 |
| [usb-audio](usb-audio/) | USB 录放音、I²S/DMA 与 codec 参数控制 |
| [bootloader](bootloader/) | USB 恢复引导程序、首次安装与保护工具 |
| [common](common/) | 共享协议、时钟和 Flash 写入 |
| [releases](releases/) | EXE、USB 升级包、首次安装镜像与校验清单 |
| [tools](tools/) | 打包、录放音测试与 SWD 诊断工具 |
| [third_party](third_party/README.md) | 已纳入仓库的依赖、授权和本地补丁说明 |

发布文件与烧录地址的区别由[烧录文件表](../docs/firmware.md#识别烧录文件)维护。
不要将原始应用 BIN 当作从地址零写入的首次安装镜像。

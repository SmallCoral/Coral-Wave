# Coral-Wave 上位机

GUI、CLI 和 MCP 共享 [Device.cs](Device.cs) 中的 USB 访问及固件包校验实现。
可执行文件位于 [releases](../releases/)，源码编译后也会输出到本目录的 `build/`。

| 任务 | 说明 |
| --- | --- |
| 播放、录音、调音、预设和 CLI | [使用方法](../../docs/usage.md) |
| 构建 GUI 与 CLI | [编译说明](../../docs/firmware.md#编译完整工程) |
| USB 更新与恢复 | [烧录说明](../../docs/firmware.md#通过-usb-更新与恢复) |
| AI 客户端连接 | [MCP](../../Documentation/MCP.md) |
| 固件包与 MCP 测试 | [无需实板的检查](../../docs/testing.md#无需实板的检查) |

测试脚本加 `--hardware` 会实际调参，加 `--hardware --flash` 会经 USB 更新板子；日常文档核验使用不带这些参数的测试。

# 文档导航

本目录说明当前 Coral-Wave 工程。
按日期保存的原始证据继续放在 `Documentation/Reports/`，芯片手册继续放在 `Documentation/Datasheets/`。

## 按任务查阅

| 任务 | 入口 |
| --- | --- |
| 了解数据、控制和时钟怎样连接 | [系统架构](architecture.md) |
| 查器件、接口、SWD 针序和 MCU 引脚 | [硬件设计与引脚](hardware.md) |
| 编译源码、安装引导程序或更新固件 | [固件编译与烧录](firmware.md) |
| 播放、录音、调音或管理预设 | [使用方法](usage.md) |
| 运行检查、追溯实板证据或查看限制 | [测试记录与限制](testing.md) |
| 查看此次文档核验范围与待确认项 | [2026-10-29 文档审查](documentation-review.md) |

## 参考资料

- [MCP 接入](../Documentation/MCP.md)维护 AI 客户端配置和工具用法。
- [USB 控制协议](../Documentation/USB-Protocol.md)维护 HID 报告、命令和 Flash 分区格式。
- [芯片资料](../Documentation/README.md)保留数据手册及校验文件。
- [专用 3D 模型](../Hardware/WM8978.3dshapes/README.md)说明模型来源与耳机插座五脚适配范围。
- [图片说明](../Images/README.md)记录照片、预览和图标来源。
- [第三方依赖](../Firmware/third_party/README.md)记录版本、许可证和本地补丁。

## 维护边界

README 保留项目概况和入口；详细参数、引脚和操作步骤由上表中的主题文档维护。
`Firmware/README.md` 与 `Firmware/host/README.md` 提供目录内入口，不再重复整套操作说明。
历史报告中的“当前”指报告当时的版本；旧路径、旧固件状态和当时的测量结果保留为历史信息。
重新判断现状时，应使用本次文件、对应源码及[可复现检查](testing.md#无需实板的检查)。

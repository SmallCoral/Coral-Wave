# USB 控制协议 v1

推荐直接使用 [MCP 或命令行](MCP.md)。需要自行接 USB 时可参考 [Device.cs](../Firmware/host/Device.cs)，它也负责固件校验和恢复。

设备 VID 为 `0xCAFE`，声卡 PID 为 `0xC0A4`，恢复模式 PID 为 `0xC0B0`。这是开发用标识。HID Usage Page 为 `0xFF00`，Usage 为 `1`，Feature Report ID 为 `1`，含 ID 共 64 字节。Windows 用 `HidD_SetFeature` 发请求，再用 `HidD_GetFeature` 轮询应答。

| 报告字节 | 含义 |
| --- | --- |
| 0 | Report ID，固定 1 |
| 1、2 | ASCII `CW` |
| 3 | 协议版本 1 |
| 4 | 命令 |
| 5 | 请求序号，应答须相同 |
| 6 | 应答码，0 成功，1 参数，2 状态，3 Flash，4 CRC，5 Codec，6 忙 |
| 7 | 有效数据长度，最多 55 |
| 8 | 保留为 0 |
| 9～63 | 数据，多字节整数为小端 |

| 命令 | 请求与应答 |
| --- | --- |
| 1 INFO | 无参数，回 11 个 uint32，见 `cw_info` |
| 2 STATUS | 应用模式，回 13 个 uint32，见 `control.c` |
| 3 GET_PARAMS | 回 32 字节 `cw_params_t` |
| 4 SET_PARAMS | 发完整 `cw_params_t`，成功后回应用结果 |
| 5 SAVE_PARAMS | 空闲时保存，随后重启 |
| 6 BOOT | 应用模式重启到恢复接口 |
| 7 REGISTERS | 发起始寄存器号，回十个 uint16 软件镜像 |
| 16 HOLD | 恢复模式停止自动启动 |
| 17 BEGIN | 四个 uint32，board、length、CRC32、version |
| 18 WRITE | uint32 连续偏移，随后 1～48 字节 |
| 19 FINISH | 校验 Flash，提交并返回 28 字节元数据 |
| 20 RUN | CRC 和向量有效时启动应用 |
| 21 ABORT | 停止写入，保留恢复接口 |

参数布局和范围以 [protocol.h](../Firmware/common/protocol.h) 与 [control.c](../Firmware/usb-audio/control.c) 为准。EQ 和 3D 固定走 DAC；ALC 仅使用板载麦克风所在的左通道。调参每轮主循环最多写一个寄存器，避免长时间阻塞录放音。

引导区为 `0x0000～0x3FFF`，设置 BOOTPROT=1。应用从 `0x4000` 开始，最大 `0x3BC00` 字节。`0x3FC00` 放有效性元数据，`0x3FD00` 和 `0x3FE00` 放两份带代数和 CRC 的参数，最后一行预留。

`.cwfw` 由 [package.py](../Firmware/tools/package.py) 生成。64 字节包头后是应用数据，包头含型号、地址、长度、版本和两处 CRC。引导程序先清除有效标记，再按顺序写应用，最终校验后才提交元数据。中断会留在恢复模式，可重新烧录；不提供旧版本自动回滚。上电还有约 2.2 秒的恢复窗口，可用上位机的「等待拔插并恢复烧录」捕获。

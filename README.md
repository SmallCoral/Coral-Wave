![Coral-Wave](Images/coral-wave.svg)

# Coral-Wave

一块用 ATSAMD21E18A 和 WM8978 做的 USB 声卡。硬件有耳机、麦克风和扬声器接口，工程用 KiCad 10 打开。

2026-10-14 实板已在 Windows 上识别为 **Coral-Wave**，播放和麦克风端点都能打开。48 kHz、16 位双向 USB 传输已通过测试。

当前固件用于打通 USB。播放数据接收后丢弃，采集端发送零样本，USB 与 I²S 的连接还没接入。新换的 WM8978 已通过 I²C 写入测试，但实板 Y2 仍空缺，恢复 12.288 MHz 有源时钟后才能继续验证录放音。

| 目录 | 内容 |
| --- | --- |
| [Documentation](Documentation/) | 主控与 WM8978 手册、实测记录 |
| [Firmware](Firmware/) | USB 固件、预编译文件、芯片自检与测试工具 |
| [Hardware](Hardware/Coral-Wave.kicad_pro) | KiCad 工程、器件库和 3D 模型 |
| [Images](Images/) | 项目标识与 PCB 预览 |

供电接板上 USB-C，下载接 J2 的 SWD。首次使用见 [固件说明](Firmware/README.md)，本次结果见 [USB 测试记录](Documentation/Reports/usb-audio/2026-10-14/review.md)。

![PCB 预览](Images/pcb-3d-preview.png)

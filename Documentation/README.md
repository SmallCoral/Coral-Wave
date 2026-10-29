# 芯片资料与历史证据

当前工程的设计、开发和使用说明见 [docs 文档导航](../docs/README.md)。
本目录保留原厂手册、协议参考及按日期归档的测试证据。

## 数据手册

| 手册 | 文件与来源 |
| --- | --- |
| SAMD21 / DA1 | [DS40001882](Datasheets/SAMD21-DA1-DS40001882.pdf)，[Microchip 原厂下载](https://ww1.microchip.com/downloads/aemDocuments/documents/MCU32/ProductDocuments/DataSheets/SAM-D21-DA1-Family-Data-Sheet-DS40001882.pdf) |
| WM8978 | [Rev 4.5](Datasheets/WM8978-Rev4.5.pdf)，[原厂手册的 LibDriver 镜像](https://github.com/libdriver/wm8978/blob/main/datasheet/wm8978_datasheet.pdf)，另有 [Mouser 下载页](https://www.mouser.com/datasheet/2/76/WM8978_v4_5-1141768.pdf) |

PDF 保留原厂内容和版权信息，文件校验值见 [SHA256SUMS.txt](Datasheets/SHA256SUMS.txt)。

## 协议与测试

- [MCP 接入](MCP.md)与 [USB 控制协议](USB-Protocol.md)。
- [测试、已知问题与历史报告索引](../docs/testing.md)。

2026-10-17 的[录放音](Reports/audio/2026-10-17/review.md)和[控制与升级](Reports/control/2026-10-17/review.md)记录覆盖已实现通路。
此前的 USB 测试使用零样本固件，不能作为当时模拟采集已经工作的证据。
旧报告中的路径、项目名和“当前”描述对应其记录日期；原始 JSON、图纸和日志保持原样。

# 双时钟及精简版复核（2026-09-23）

正式原理图现有 **54 个实体器件**。Y1 12.000 MHz 只送 SAMD21 PA14/XIN；Y2 12.288 MHz 经 R73=33 Ω 只送 WM8978 MCLK，两个时钟输出在网表中相互隔离。C103、C104 分别旁路 Y1、Y2。Y1/Y2 的使能共用 PA18，R74 使上电默认关闭。原 U18 缓冲器及其输入下拉 R79 已删除。

WM8978 配置为 I²S 主机：12.288 MHz MCLK 直驱，PLL 关闭，`R6.CLKSEL=0`、`MCLKDIV=000`、`BCLKDIV=010`、`MS=1`，输出 48 kHz LRCLK 和 3.072 MHz BCLK（双声道 32-bit 时隙）。SAMD21 I²S clock unit 0 作为从机，用串行器及 DMA 收发。12 MHz 是 MCU 的外部参考；USB 所需 48 MHz 时钟由 MCU 时钟系统在固件中建立。两个时钟域异步，USB Audio 必须实现反馈及缓冲管理。

上轮精简删除 12 个测试点、R62/R72/R77/R80、SW1；本轮又删除 U18/R79，增加 Y2，C104 改为 Y2 旁路。耳机左右声道已对应 Tip=左、Ring=右；四针 SWD 接口保留。J2 符号收入项目本地库，消除对缺失的 SparkFun 符号库引用。

KiCad 10.0.6 原生检查结果：

- 原理图 ERC：**0 错误、0 警告**；[报告](erc.json)。
- 关键网表拓扑：**35 项通过**，包含两个时钟输出隔离、I²S、USB、电源和耳机声道；[检查结果](topology-check.json)、[网表](netlist.xml)。
- PCB：54 个封装，原理图一致性 **0 项问题**；[同步记录](pcb-sync.json)。现有 PCB **0 条走线、0 个铺铜区**。
- 整板 DRC：**4 处 USB2 封装内部孔距违规**（焊盘 A1/B12、A12/B1 与两个定位孔的实际距离 0.1944 mm，小于项目设置的 0.25 mm）；另有 **135 个未连接项目**。这些与布线/连接器封装规则有关，尚未放行制板；[DRC](drc.json)。

布局时把 Y2 和 R73 放在 WM8978 MCLK 附近，C104 靠近 Y2 VDD，并让 12 MHz 与 12.288 MHz 走线分开。USB2 的定位孔/焊盘几何应对照 GCT 原厂推荐图及板厂能力核实；不要单靠调低全板 DRC 规则消除四处违规。原理图 [SVG](WM8978.svg) 可直接查看。

资料：[Abracon ASE](https://abracon.com/Oscillators/ASEseries.pdf)、[WM8978](https://www.mouser.com/datasheet/2/76/WM8978_v4_5-1141768.pdf)、[SAMD21](https://ww1.microchip.com/downloads/aemDocuments/documents/MCU32/ProductDocuments/DataSheets/SAM-D21-DA1-Family-Data-Sheet-DS40001882.pdf)、[SAMD21 I²S 模式说明](https://developerhelp.microchip.com/xwiki/bin/view/products/mcu-mpu/32bit-mcu/sam/samd21-mcu-overview/peripherals/samd21-i2s-overview/)。

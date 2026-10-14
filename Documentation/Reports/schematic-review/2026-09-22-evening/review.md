# 当前原理图复查 · 2026-09-22 晚

本轮审查当前 SAMD21E18A + WM8978 单页设计。原理图、PCB、项目设置和器件库均未修改，检查对象的 SHA-256 见 `baseline.json`。

结论：主要电气连接已贯通，没有再次发现 MCLK 断网；仍应修正耳机左右声道，并在继续相关布线前同步 USB2/CN2 的 PCB 封装。旧报告中的 MCLK 断线、PCB 全部为 STM32、缺少限流开关和时钟缓冲器等描述已经不适用于本版。

## 明确需要修改

### 1. 耳机左右声道互换

当前网表实际连接：

| 芯片输出 | 电容 | 插座端子 | 标准 TRS 声道 |
| --- | --- | --- | --- |
| U16.29 / ROUT1，右声道 | C86 | CN2.4 / Tip | 左 |
| U16.30 / LOUT1，左声道 | C87 | CN2.3 / Ring | 右 |
| GND | — | CN2.2 / Sleeve | 公共地 |

按 CN2 所附 XKB 图纸的 SCHEMATIC 和插座接触片位置，端子 2 为 Sleeve、3 为 Ring、4 为 Tip。应交换两个耳机输出支路，使 LOUT1 经隔直电容接 CN2.4，ROUT1 经隔直电容接 CN2.3；电容正极继续朝 codec。否则标准立体声播放的左右声道会颠倒。可以用固件交换样本补偿，但当前工程无固件，不应依赖未实现的补偿。

来源：[XKB PJ-3136 原厂图纸](https://atta.szlcsc.com/upload/public/pdf/source/20231101/FB45AABFD04C0979FDEE8A028D0B6336.pdf)。

### 2. PCB 尚未同步两处连接器封装

- USB2：原理图指定 `WM8978-import-fps:USB_C_GCT_USB4105_GF_A`，MPN 为 `USB4105-GF-A`；PCB 实际仍为 `WM8978-import-fps:USB_TYPE-C-16P`。两者的焊盘尺寸、相对位置和机械孔不同，并非只有名字不同。PCB 旧封装也没有新封装中的两个 0.65 mm 定位孔。应按最终 GCT 型号同步封装，再检查接口已有走线、板边及外壳位置。
- CN2：项目库已经改为五个电气通孔加两个 1.60 mm 非金属化定位孔；PCB 内嵌的同名封装仍是六个电气通孔、零定位孔。仅“从原理图更新 PCB”未必刷新同名封装几何，需要执行“从库更新封装”并检查孔位。

本轮核对了两端的实际焊盘数据，不是根据 README 推断。PCB 已包含本版 72 个器件，不能继续称为“全部旧 STM32 PCB”。除 USB2 封装字段外，器件值和封装标识对应；电气焊盘网名也一致（两个未连接网络的 `/` 与 `{slash}` 是导出转义差异）。这不代表 PCB 已完成布局布线或通过 DRC。

来源：[GCT USB4105 产品页](https://gct.co/connector/usb4105)、原理图和 PCB 当前文件。

### 3. 库引用及自动检查需要更新

KiCad 10.0.6 原生 ERC：**0 错误、4 警告**，没有新增排除项。绝对路径和项目目录下重跑结果一致。

- J2 的 `PCM_SparkFun-Connector` 符号库在当前配置中不存在。建议换成标准连接器符号，或将实际使用的符号纳入项目本地库。
- USB2、CN2、MIC1 的 `WM8978-import-fps` 封装库在当前 CLI 环境中未被 ERC 解析。项目 `fp-lib-table` 和对应文件实际存在、语法可解析，因此这里只能确认库解析警告，不能据此断言三个封装丢失或电路错误。应检查项目库加载，再验证清零；不要关闭检查项目掩盖警告。
- `scripts/check_schematic.py` 仍假设 `/3V3`、旧 J1、振荡器直接接 R73、没有串联时钟电阻和测试点。对本次新网表运行时，首先在 `U1.9 -> 3V3` 失败，原因是当前电源网名为 `+3V3`。旧“200 项通过”不适用于本版；修改检查器时应同时校正耳机声道预期，不能把当前反接固化为测试要求。

## 本版已解决或核对通过

- MCLK：`R73.2 / U1.15 / U16.11 / TP7.1` 同网。
- Y1 经 U18 缓冲后驱动 MCU XIN 和 codec MCLK；U18 的低有效 OE 接地、输入有 R79 下拉、VCC 有 C104 去耦。此前振荡器直接承受双负载的结构已调整。
- TPS2553：IN/EN 接 VBUS_RAW，OUT 给 LDO 和 SPKVDD，ILIM 经 R78 到地；FAULT 是开漏信号，经 R80 上拉到 3V3。C105=1 µF 是直接位于 USB 入口的电容，原有较大电容在开关之后。
- R78=61.9 kΩ、1% 时，按 TI §9.5.1 公式估算限流范围约 **378–480 mA**，典型 **425 mA**。因此图中连续总负载不超过 350 mA 的约束有余量；仍需实测扬声器峰值和 USB 插入时的电流波形。此限流值不等于主机已允许设备取用相同电流。
- SAMD21 电源与地脚、VDDCORE 独立 1 µF 去耦、USB D±、两路 CC 下拉、SWD 和 RESET 测试点连接正确。
- MCU 的 PA07/PA08 对应 I2S SD0/SD1，PA10/PA11 对应 SCK0/FS0，PA14 为 XIN；主控作为音频从机的接线成立。
- MICBIAS 已接到麦克风偏置支路；MIC1 正负极、输入耦合和 VMID 去耦连接正确。
- U16 的中心焊盘已选 3.45 mm，PCB 实际铜焊盘也是 3.45 mm，EP 接地。
- C86/C87 已选 Panasonic EEEFK1C471P、470 µF/16 V、8 mm 直径封装。470 µF 配 16 Ω 负载的理想一阶截止约 21.2 Hz；无需再次建议从 220 µF 增大容量。
- BTL 扬声器两端分别接 ROUT2、LOUT2，没有接地。

## 可选优化与后续验证

1. **优先验证供电与扬声器峰值。** 350 mA 是整板约束，扬声器峰值叠加 MCU/codec 电流可能先触发限流，导致供电下跌、失真或复位。先测量，再决定是否需要调整限幅、供电支路或受控电源后的储能容量。
2. **FAULT 可接到空闲 MCU 输入。** 当前只引到 TP12，适合测量；接入 MCU 可记录过流/过温事件并做静音处理。不是基本功能的必需条件。
3. **MCU 版本和固件必须兑现图中约束。** 按 DSU.DID 排除不支持本 I2S 从机结构的早期硅版本；32-bit 时隙承载 16-bit 音频，48 kHz、BCLK 3.072 MHz。应用需实现异步 USB 音频反馈与缓冲管理、挂起/恢复时钟顺序和枚举前限功耗。
4. **时钟与保护按板级测试优化。** R73/R75/R76 靠近各自驱动端；保持连续参考地。检查时钟两端波形、USB 插拔/ESD、挂起电流和 LDO 温升后再决定是否增添保护或调整阻值。

依据：[TPS2553 数据手册](https://www.ti.com/lit/ds/symlink/tps2553.pdf)、[SN74LVC1G125 数据手册](https://www.ti.com/lit/ds/symlink/sn74lvc1g125.pdf)、[Abracon ASE 数据手册](https://abracon.com/Oscillators/ASEseries.pdf)、[Microchip SAM D21 数据手册](https://ww1.microchip.com/downloads/en/DeviceDoc/SAM-D21DA1-Family-Data-Sheet-DS40001882G.pdf)、[SAM D21 勘误 §1.10](https://ww1.microchip.com/downloads/aemDocuments/documents/MCU32/ProductDocuments/Errata/SAM-D21DA1-Family-Silicon-Errata-and-Data-Sheet-Clarification-DS80000760.pdf)。

## 检查范围和证据

本目录 `erc.json`、`netlist.xml` 为本次 KiCad 原生输出，`baseline.json` 记录审查对象。另进行了当前 12 项关键拓扑断言（全部通过）、连接器机械孔检查、72 个原理图器件与 PCB 器件及电气焊盘网络比较，并查看完整原理图渲染及连接器图纸。

本轮没有运行整板 DRC，没有运行固件或实板验证。没有把 ERC 通过当作整机可投产证明。

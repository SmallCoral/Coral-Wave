# 当前原理图复查 · 2026-09-22

**结论：当前版本不能直接进入正式 layout。** 新发现 WM8978 的 MCLK 断网；CN2 耳机座的机械定位孔问题仍未解决。本轮为检查与记录，未修改原理图、PCB、封装库或项目设置。

## 本次重新执行的检查

- KiCad 10.0.5 重新导出当前网表，并执行原生 ERC：**1 个错误、1 个警告**，均由 codec MCLK 断网引起。
- 对比上次网表：排除旧 J1 / 新 J2 后，178 个共同引脚中，175 个引脚的连接组不变；另外 3 个引脚的差异全部属于同一处 MCLK 网络分裂。
- J2 的 4 个引脚单独检查通过：1=3V3/VTref、2=SWDIO、3=SWCLK、4=GND。
- 48 个器件的封装均存在，符号引脚与电气焊盘编号集合对应；SW1 新封装的两个焊盘也对应。该检查不覆盖机械定位孔及实际采购器件的尺寸。
- 已查看本次原生导出的整页图面，确认时钟源侧缺少网络标签。
- 旧 `scripts/check_schematic.py` 在 `U1.15 -> I2S_MCLK` 检查失败；它还使用旧 J1 接口约束，不能再把此前的“200 项通过”当作本版结论。

证据：`recheck-current-erc.json`、`recheck-current-netlist.xml`、`recheck-current.json`、`layout-pad-check.json`。

## 正式 layout 前的明确修正项

### 1. 恢复 R73 输出到 WM8978 MCLK 的网络

当前网表是：

| 网络 | 实际成员 |
| --- | --- |
| `Net-(U1-PA14)` | R73.2、U1.15 |
| `/I2S_MCLK` | 只有 U16.11 |

因此 Y1 经 R73 只给 MCU 提供时钟，codec 没有 MCLK；按本设计的 PLL / I²S 主机架构，音频时钟不能正常建立。ERC 对应 `pin_not_driven` 和 `isolated_pin_label`。

修正方式：在 **R73.2 至 U1.15 的导线上**恢复 `I2S_MCLK` 标签，使 `R73.2 / U1.15 / U16.11` 成为同一网络；不要用 PWR_FLAG 或忽略 ERC 消除提示。修正后重新导出网表、验证这三个引脚同网。

### 2. CN2 机械定位孔仍缺失

当前指定的 `AUDIO-TH_PJ-3136-B` 封装只有六个电气通孔焊盘，没有机械定位孔。CN2 所附图纸的 PJ-3136 视图要求两个 Φ1.60 mm 定位孔。按该图纸采购的带定位柱器件存在无法贴板安装的风险。

应按最终采购型号核对机械图并修正封装，核对插口与板边位置。[XKB 原厂图纸](https://atta.szlcsc.com/upload/public/pdf/source/20231101/FB45AABFD04C0979FDEE8A028D0B6336.pdf)

### 3. 落实关键实物料号与封装

USB2 仍只有通用 16P Type-C 封装，C86/C87 仍只有 220 µF / 16 V 参数而无有效制造商型号。应先确认实际接口机械尺寸、电解电容直径/高度/焊盘。

U16 仍写 `WM8978GEFL/RV`，使用中心焊盘 3.1 × 3.1 mm 的通用 QFN；引用的 Rev 4.5 手册包含更新的 `WM8978CGEFL/...` 订货码及封装图，应按实际后缀核对。封装裸露金属尺寸与 PCB 铜皮尺寸不能直接等同，不能仅凭名称判断兼容性。[WM8978 手册](https://www.mouser.com/datasheet/2/76/WM8978_v4_5-1141768.pdf)

## 设计改进建议

| 项目 | 建议及优先级 |
| --- | --- |
| 下载接口 | 当前 J2 四针信号接线正确，但没有 RESET。建议增加 RESET 引脚或紧邻接口的复位测试点，以便调试器控制复位；四针 SWD 本身并非接线错误。J2 实际封装是 JST XH 2.50 mm，自制转接线应按上述针序，不能只凭“JLINK”名称接标准排线。 |
| USB 上电浪涌 | VBUS 直接电容仍合计约 9.6 µF，3V3 端直接去耦约 39.91 µF。建议确认启动电流与主机端口要求，必要时增加限流/受控上升时间的负载开关。下游电容不能简单与输入电容相加判定违规。 |
| 时钟驱动 | 修复 MCLK 后，Y1 将恢复同时驱动两个芯片。ASE 规格负载为 15 pF，WM8978 数字输入电容列为 10 pF；需计入 MCU XIN 和走线寄生。若余量不足，应选择驱动能力更高的振荡器或缓冲器。串阻可改善振铃，不能代替负载能力核算。 |
| MCU 硅版本 | 当前 I²S 从机架构应排除 SAMD21 硅版本 A/B；型号末尾 A 不是硅版本。通过 DSU.DID 验收，按勘误处理数据槽对齐；32-bit 时隙承载 16-bit 音频是可采用的配置。 |
| 音频供电与调试 | AVDD 支路预留 0 Ω / 磁珠位置，保持连续地平面并控制扬声器回流；BCLK/LRCLK 源端预留可调串阻，增加电源及关键时钟测试点。 |
| 耳机低频 | C86/C87=220 µF 配 16 Ω 耳机的理想一阶截止约 45.2 Hz，配 32 Ω 约 22.6 Hz。若重视 16 Ω 耳机低频，可考虑 470 µF，同时核对体积和上电爆音控制。 |

供电、时钟和硅版本依据：[TI USB 热插拔负载说明](https://www.ti.com/lit/an/slva049/slva049.pdf)、[Abracon ASE 规格](https://abracon.com/Oscillators/ASEseries.pdf)、[WM8978 数字输入规格](https://www.mouser.com/datasheet/2/76/WM8978_v4_5-1141768.pdf)、[Microchip 勘误 §1.10](https://ww1.microchip.com/downloads/aemDocuments/documents/MCU32/ProductDocuments/Errata/SAM-D21DA1-Family-Silicon-Errata-and-Data-Sheet-Clarification-DS80000760.pdf)。

## 本轮文件基线

当前文件已经与上次审查版本不同，本轮保留用户的后续修改，没有回退 PCB。

| 文件 | 本轮开始及结束时 SHA-256 |
| --- | --- |
| 原理图 | `da9090cd18255e0a34c1a41e4236d26b039e8611c06d62f0447b395383ad4108` |
| PCB | `3dbe479590fec1b7c73a3aa50047f6be0c3d2642301092d8fa0e42d8cbf08127` |
| 项目设置 | `a735b4348fb94e4a48c6c9c1843c64e70f60052514bbe274a237ced6aaafd8e3` |

本轮未验证 PCB 布局布线、固件或实板。此前报告保留为历史记录；当前放行结论以本文件及对应 SHA-256 为准。

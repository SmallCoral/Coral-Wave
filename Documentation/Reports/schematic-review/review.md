# 原理图复核记录 · 2026-09-22

后续 layout 审查发现了机械封装和器件版本等待解决事项，详见 [layout-readiness.md](layout-readiness.md)。本记录的 ERC/连接通过不代表已满足完整 layout 放行条件。

## 设计基线

工作开始时，用户已经删除旧 STM32 主控电路并放入未接线的 ATSAMD21E18A-A。
本次沿用该选择和现有 USB/WM8978 电路，未从 Git 恢复旧原理图。
保留所有原有器件 UUID，仅调整部分文字位置并补充主控外围。

原生 ERC 初检 45 项；最终 0 错误、0 警告。检查器仍使用原项目规则，没有新增排除或降低严重度。
原项目忽略的类别仍是 single_global_label、four_way_junction、simulation_model_issue、footprint_filter，详见 ERC JSON。

## 硬件修正

1. 完成 U1 的供电、所有地脚、USB FS、双向音频数据、I²C、复位和 SWD。
2. 新增 C97–C103、R70–R74、Y1、SW1、J1，共 15 个器件。
3. 使用独立 12 MHz 参考驱动 MCU XIN 和 codec MCLK，避免直接把 MCU 48 MHz 整数分频误当成 12.288 MHz。
4. 时钟 EN 接 PA18，100 kΩ 默认下拉；支持在切回内部时钟后关断振荡器。
5. 将 U16 的 MICBIAS 输出与 R68 偏置支路连通。原图标签仅存在于电阻一侧，没有给麦克风供电。
6. 更新主控名称、模块分区、说明及部分密集文字。原有 USB/音频器件位置和所有导线保持。

## 电气复核

| 项目 | 结论 |
| --- | --- |
| VDDIN、VDDANA | 同为 3V3，每组 100 nF + 10 µF |
| VDDCORE | 与 3V3 隔离，仅接 1 µF 对地；两个 GND 脚均连接 |
| 复位和调试 | RC 复位、330 Ω 按键串阻、SWCLK 1 kΩ 上拉；标准 Cortex SWD 针序 |
| USB | PA24=D−、PA25=D+；两侧触点并接正确，两路 CC 独立下拉，TVS 对地并联 |
| Codec 控制 | PA22/23 使用 SERCOM3 PAD0/1；4.7 kΩ 上拉；MODE=0，地址 0x1A |
| I²S | PA07=SD0 TX、PA08=SD1 RX；PA10/11 分别接 codec BCLK/LRC；同时收发 |
| 麦克风 | MICBIAS→R68→MIC1 正极；C79 接 LIP，C80 将 LIN 交流接地 |
| 耳机 | 两个 220 µF 电容正极朝向 codec，输出与直流偏置隔离 |
| 扬声器 | ROUT2 与 LOUT2 分别接负载两端，不接地；需固件配置 BTL 信号及反相 |
| 不使用的 codec 引脚 | 保留现有 NC，固件关闭不用的输入/输出通道 |

## 固件约束

- 启动顺序：OSC8M 启动 → PA18 置高 → 等待至少 2 ms → XOSC 外部时钟模式 → 配置 MCU/USB 时钟。
- USB 必须得到符合规格的 48 MHz 时钟并加载芯片 USB pad 校准值。
- WM8978 以 12 MHz 为 PLL 输入，参考参数 N=8、K=0x3126E9、PLLPRESCALE=0、MCLKDIV=/2；再选择 BCLK 分频、字长和 48 kHz 帧率。
- codec 为音频主机，SAMD21 的 serializer 0 发送、serializer 1 接收，共用 clock unit 0 的外部 BCLK/FS。不能把 PA07、PA08 都配置成输出。
- USB 主机 SOF 和板上音频参考并不锁相，需要异步反馈及持续的 DMA 缓冲管理；录音端也须正确管理每帧样本数。
- CSB 接地，不能把 GPIO1 配置为推挽高电平输出。
- 挂起时先使 codec 进入低功耗、MCU 切回内部时钟，再拉低 PA18。唤醒按相反方向建立时钟，并等待稳定。
- 未枚举和挂起时限制功耗，扬声器输出需按可用 USB 电流预算限幅。不能把 1 W 额定输出当作所有主机端口都允许的工作点。

## 检查证据

- `erc.json`：KiCad 10.0.5，所有错误、警告、排除项均纳入输出，违规数为零。
- `netlist.xml`：KiCad 从最终原理图直接导出。
- `connectivity-check.json`：200 项检查通过，包括关键引脚/参数、未使用 GPIO、完整旧电路连接对比。
- 旧连接对比允许的唯一网络合并：`R68.1 + U16.32 + C96.1`，对应 MICBIAS 修复。
- 原生 SVG 导出后另用原生 PDF/Poppler 做图面检查，检查了新增主控区和密集 codec 标注。
- PCB SHA-256：`fe49a5879a41e8960b2d1fa32fe4746d4188ac50d7024b465eacbf6cd71d87dd`，修改前后一致。
- 项目设置 SHA-256：`fc729af468ccccd9d76b9b11aee179896fd4961858aca257d9ce4f9314589901`，修改前后一致。

## 保留的实际验证事项

尚未运行固件或实板测试。USB 信号质量、ESD、挂起电流、LDO 温升、振荡器双负载与走线电容、
音频噪声和最大输出功率须在后续实板确认。现有 USB 外壳直连 GND，VBUS 无独立 TVS；
是否增加屏蔽 RC 或 VBUS 保护应结合外壳接地及产品 EMC 要求确定。
本次不更新 PCB；旧 STM32 PCB 和四份旧参考原理图均不代表当前正式电路。

器件与引脚依据见仓库 README 的厂商数据手册链接。

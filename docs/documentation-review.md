# 文档审查记录（2026-10-29）

本次按本地 docs Skill 先检查工程、汇报修改计划，再更新 Markdown。
源代码、原理图、PCB、工程配置、图片、许可证文件与预编译程序均不在修改范围。
检查以磁盘上工作区为准，不代表检查了编辑器中未保存的内容。
基础源码提交为 `4afef10ba65532cff3dbdbe074827ef3542a245a`。
工作区原有原理图编辑仅涉及 R75/R76 图面位置及相连线段，本次未提交该编辑；重新导出的工作区和 Git 版本网表，其全部引脚连接集合一致。

## 检查依据

- 逐项查阅自有固件、上位机、构建与测试脚本，以及现有 Markdown。
- 从当前原理图重新导出 KiCad XML 网表，读取 PCB 焊盘及网络，交叉核对接口和引脚。
- 查看整板实物、芯片局部照及 PCB 设计预览，标明调试阶段与工程选型的差异。
- 对照 2026-10-17 原始结果，保留测试时长、固件阶段和未测项目的边界。
- 核查第三方及 Logo 授权；没有为项目自有内容代选许可证。

## 修改范围

| 文件或范围 | 修改 |
| --- | --- |
| [README](../README.md) | 项目概况、已实现功能、实物图、文档入口、目录与许可证状态 |
| [docs 导航](README.md) | 当前设计、开发、使用和证据的归属 |
| [架构](architecture.md) | 音频、控制、时钟、保存和更新机制 |
| [硬件](hardware.md) | 工程器件、供电、GPIO/封装引脚、连接器与实物差异 |
| [编译烧录](firmware.md) | 工具链、输出文件、首次 SWD 安装、日常 USB 更新与恢复 |
| [使用方法](usage.md) | 录放音、调音范围、预设编码、CLI 操作 |
| [测试与限制](testing.md) | 可复现命令、历史报告索引、现有证据和待验证项 |
| [Firmware 入口](../Firmware/README.md)、[host 入口](../Firmware/host/README.md) | 引导到主题文档，减少重复维护 |
| [Documentation 索引](../Documentation/README.md)、[MCP](../Documentation/MCP.md) | 补充导航，修正 CLI 示例的运行目录及路径 |
| [图片来源](../Images/README.md)、[第三方来源](../Firmware/third_party/README.md) | 补充阶段说明和许可证链接 |
| 14 份历史报告 | 增加历史版本提示；模型报告中 1 处失效链接改为当前真实路径，原始测量未改写 |

## 实际执行的验证

| 检查 | 结果及限制 |
| --- | --- |
| 当前网表和 PCB | 原理图、PCB 各 54 个器件，已连接多引脚网络集合一致；35 项拓扑检查通过 |
| KiCad 原生检查 | 10.0.6；ERC 无违规，DRC 仍有制造及丝印事项，详见[当前结果](testing.md#2026-10-29-静态与离线复核) |
| 完整源码构建 | 临时副本，Arm GNU 14.3.Rel1 / GCC 14.3.1、Python 3.12.14 与系统 .NET C# 编译器；引导、应用、GUI、CLI、打包均成功 |
| RAM 自检构建 | 复用已有依赖，Control/Audio、Hardware I²C、Direct12288 两种模式均构建成功；未运行 SWD 加载 |
| 固件包用例 | 原有 10 项测试全部通过 |
| MCP 用例 | 使用现有官方 SDK 1.30.0，初始化、ping、10 个工具的 schema/操作标记、包校验和错误返回通过；未加硬件或烧录选项 |
| 发布文件 | 仓库内 7 份发布文件的大小及 SHA-256 与 manifest 一致；CLI 对 `.cwfw` 的本地校验成功 |
| 发布固件与源码 | 本机重建的应用 BIN、引导 BIN、组合 BIN 和 `.cwfw` 与仓库发布物逐字节相同；不据此推断其他工具链的构建一致性 |
| 本地引用 | 检查全部 Markdown 相对链接、标题锚点和 HTML 图片路径；失效路径已修复 |
| 示例与图像 | 16 段 PowerShell 示例通过语法解析；8 份 PNG/ICO/SVG 均可解码或解析；两份数据手册 SHA-256 匹配 |
| 外部引用 | 检查 21 个外链，19 个直接 HTTP 可访问；GCT 与 Microchip I²S 两页通过网页工具确认可读，直接请求分别返回 403 和超时 |
| 修改边界 | 将受版本控制的非 Markdown 文件与本次开始时 SHA-256 比较，确认原有原理图编辑及其他文件保持原样 |

构建与测试在忽略目录内的临时副本执行，没有覆盖仓库中的发布 EXE 或固件。
MCP 复测复用已有依赖目录，没有重新安装测试依赖；文档中的全新 venv 安装过程未从零复测。
首次安装、USB 烧录、恢复、实板调音、参数保存、音频及断电操作仅核对源码和历史记录，本次未执行。

## 独立文档审查

由未参与本次编写的审查代理只读复核全部 Markdown、本地引用、关键代码、网表、实物图和测试原始结果。
未发现阻止交付的实质错误；SWD、音频引脚、参数范围、CLI/MCP 编码区别和烧录前置条件均与实现一致。
审查建议将“先构建”的要求移到源码测试小节，避免读者在核对原有发布文件之前覆盖发布物；该建议已采纳。
没有把实板重测、全新测试环境安装或全部平台兼容性写为本轮通过项目。

## 仍需人工确认

1. 为项目自有代码、硬件与文档明确许可证。
2. 按采购记录确认实板 MCU、codec 的完整订货码及封装；确认最终装配照片和 BOM。
3. 确认板厂与装配工艺，处理 DRC 及接口机械配合。
4. 重新开展所需实板与模拟性能测试；具体缺项由[限制清单](testing.md#已知问题与尚未验证内容)维护。

## Markdown 文件逐项结论

下表中 PASS 表示本次范围内的内容、入口及本地引用已核对。
WARN 表示保留历史版本语境，不作为当前版本的结论；N/A 为第三方上游内容，不按 Coral-Wave 功能说明重写。

| 文件 | 结果 | 处理或依据 |
| --- | --- | --- |
| [docs/architecture.md](architecture.md) | PASS | 按源码、工程或文件来源核对 |
| [docs/documentation-review.md](documentation-review.md) | PASS | 按源码、工程或文件来源核对 |
| [docs/firmware.md](firmware.md) | PASS | 按源码、工程或文件来源核对 |
| [docs/hardware.md](hardware.md) | PASS | 按源码、工程或文件来源核对 |
| [docs/README.md](README.md) | PASS | 按源码、工程或文件来源核对 |
| [docs/testing.md](testing.md) | PASS | 按源码、工程或文件来源核对 |
| [docs/usage.md](usage.md) | PASS | 按源码、工程或文件来源核对 |
| [Documentation/MCP.md](../Documentation/MCP.md) | PASS | 按源码、工程或文件来源核对 |
| [Documentation/README.md](../Documentation/README.md) | PASS | 按源码、工程或文件来源核对 |
| [Documentation/Reports/audio/2026-10-17/review.md](../Documentation/Reports/audio/2026-10-17/review.md) | WARN | 已标明历史基线；保留原始结论和证据 |
| [Documentation/Reports/board-test/2026-10-14/review.md](../Documentation/Reports/board-test/2026-10-14/review.md) | WARN | 已标明历史基线；保留原始结论和证据 |
| [Documentation/Reports/control/2026-10-17/review.md](../Documentation/Reports/control/2026-10-17/review.md) | WARN | 已标明历史基线；保留原始结论和证据 |
| [Documentation/Reports/footprint-binding/2026-10-03/review.md](../Documentation/Reports/footprint-binding/2026-10-03/review.md) | WARN | 已标明历史基线；保留原始结论和证据 |
| [Documentation/Reports/pcb-review/2026-10-02/review.md](../Documentation/Reports/pcb-review/2026-10-02/review.md) | WARN | 已标明历史基线；保留原始结论和证据 |
| [Documentation/Reports/pcb-review/2026-10-02-recheck/review.md](../Documentation/Reports/pcb-review/2026-10-02-recheck/review.md) | WARN | 已标明历史基线；保留原始结论和证据 |
| [Documentation/Reports/pcb-review/2026-10-02-recheck-2347/review.md](../Documentation/Reports/pcb-review/2026-10-02-recheck-2347/review.md) | WARN | 已标明历史基线；保留原始结论和证据 |
| [Documentation/Reports/project-snapshot/2026-09-23/review.md](../Documentation/Reports/project-snapshot/2026-09-23/review.md) | WARN | 已标明历史基线；保留原始结论和证据 |
| [Documentation/Reports/schematic-review/2026-09-22-evening/review.md](../Documentation/Reports/schematic-review/2026-09-22-evening/review.md) | WARN | 已标明历史基线；保留原始结论和证据 |
| [Documentation/Reports/schematic-review/2026-09-23-dual-clock/review.md](../Documentation/Reports/schematic-review/2026-09-23-dual-clock/review.md) | WARN | 已标明历史基线；保留原始结论和证据 |
| [Documentation/Reports/schematic-review/layout-readiness.md](../Documentation/Reports/schematic-review/layout-readiness.md) | WARN | 已标明历史基线；保留原始结论和证据 |
| [Documentation/Reports/schematic-review/recheck-current.md](../Documentation/Reports/schematic-review/recheck-current.md) | WARN | 已标明历史基线；保留原始结论和证据 |
| [Documentation/Reports/schematic-review/review.md](../Documentation/Reports/schematic-review/review.md) | WARN | 已标明历史基线；保留原始结论和证据 |
| [Documentation/Reports/usb-audio/2026-10-14/review.md](../Documentation/Reports/usb-audio/2026-10-14/review.md) | WARN | 已标明历史基线；保留原始结论和证据 |
| [Documentation/USB-Protocol.md](../Documentation/USB-Protocol.md) | PASS | 按源码、工程或文件来源核对 |
| [Firmware/host/README.md](../Firmware/host/README.md) | PASS | 按源码、工程或文件来源核对 |
| [Firmware/README.md](../Firmware/README.md) | PASS | 按源码、工程或文件来源核对 |
| [Firmware/selftest/README.md](../Firmware/selftest/README.md) | PASS | 按源码、工程或文件来源核对 |
| [Firmware/third_party/README.md](../Firmware/third_party/README.md) | PASS | 按源码、工程或文件来源核对 |
| [Firmware/third_party/tinyusb/src/class/midi/README_midi_host.md](../Firmware/third_party/tinyusb/src/class/midi/README_midi_host.md) | N/A | 上游附带资料；本地链接已检查，未改写 |
| [Firmware/third_party/tinyusb/src/portable/microchip/pic/README.md](../Firmware/third_party/tinyusb/src/portable/microchip/pic/README.md) | N/A | 上游附带资料；本地链接已检查，未改写 |
| [Firmware/third_party/tinyusb/src/portable/synopsys/dwc2/dwc2_info.md](../Firmware/third_party/tinyusb/src/portable/synopsys/dwc2/dwc2_info.md) | N/A | 上游附带资料；本地链接已检查，未改写 |
| [Hardware/WM8978.3dshapes/README.md](../Hardware/WM8978.3dshapes/README.md) | PASS | 按源码、工程或文件来源核对 |
| [Images/README.md](../Images/README.md) | PASS | 按源码、工程或文件来源核对 |
| [README.md](../README.md) | PASS | 按源码、工程或文件来源核对 |

## 优先补齐的三项信息

1. 明确项目级许可证。
2. 确认照片、采购器件与工程型号后缀的对应关系。
3. 确认制造规则并补充当前限制清单中的实板测量。

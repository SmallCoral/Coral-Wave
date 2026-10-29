# 测试记录与限制

本文将仓库已有的实板证据与可重复执行的检查分开。
历史通过记录只适用于当时的硬件、固件及测试条件；2026-10-29 文档核验没有重新烧录或测试实板。

## 已有实板证据

| 日期与范围 | 已记录结果 | 证据及适用边界 |
| --- | --- | --- |
| 2026-10-17 耳机及麦克风 | 用户确认耳机左右声道正常，板载麦克风讲话录音回放清楚 | [音频报告](../Documentation/Reports/audio/2026-10-17/review.md)、[结果](../Documentation/Reports/audio/2026-10-17/result.json)；讲话采样属于最终音量描述符和 DMA 中断完善前的阶段 |
| 2026-10-17 录放音压力测试 | 45 秒双向传输、20 轮音量及静音切换，窗口内错误计数无新增 | 同一份[音频结果](../Documentation/Reports/audio/2026-10-17/result.json)；无新增不等于设备整个运行期间累计计数始终为零 |
| 2026-10-17 发布版 3.0.0 | USB 更新、已保存参数保留、中断传输恢复、30 秒同时录放音及 24 轮参数测试 | [控制与更新报告](../Documentation/Reports/control/2026-10-17/review.md)、[结果](../Documentation/Reports/control/2026-10-17/result.json) |
| 2026-10-17 上位机与 MCP | 10 个 MCP 工具、官方 SDK 接入、固件包校验和三种窗口尺寸切页 | 同一份[控制结果](../Documentation/Reports/control/2026-10-17/result.json)；没有覆盖所有 DPI、主机系统或 AI 客户端 |

音频早期测试与发布版结果使用不同固件阶段，不能将所有数据合并成一次最终发布测试。
已有记录明确没有实测拔电瞬间的全部情况，也没有测量 SNR、THD 等模拟性能。
讲话录音只保存在本地，未提交仓库。

## 无需实板的检查

以下 PowerShell 命令在仓库根目录运行。
先核对仓库原有发布文件，再按需要构建源码并运行测试；以下检查不会烧录板子。

### 核对发布文件

```powershell
$release = (Resolve-Path ./Firmware/releases).Path
$manifest = Get-Content "$release/manifest.json" -Raw | ConvertFrom-Json
foreach ($entry in $manifest.files.PSObject.Properties) {
    $file = Join-Path $release $entry.Name
    if ((Get-Item -LiteralPath $file).Length -ne $entry.Value.bytes) {
        throw "文件大小不符：$($entry.Name)"
    }
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.Value.sha256) {
        throw "SHA-256 不符：$($entry.Name)"
    }
}
./Firmware/releases/Coral-Wave-CLI.exe --validate ./Firmware/releases/Coral-Wave.cwfw
```

循环无异常表示大小与 SHA-256 匹配；最后一条命令返回包校验 JSON。

### 固件包与 MCP 测试

先按[编译说明](firmware.md#编译完整工程)构建，让测试使用的 `Firmware/host/build/Coral-Wave-CLI.exe` 存在。
顶层构建会更新 `Firmware/releases/` 和清单，因此核对仓库原有发布文件应在构建之前进行。

```powershell
python ./Firmware/host/tests/test_packages.py
```

该文件包含 10 项用例，检查有效包及损坏、截断、型号、地址、向量表和长度异常。
测试调用 `Firmware/host/build/Coral-Wave-CLI.exe`，因此仅下载发布版 EXE 还不能直接运行这套测试。

MCP 测试需要[测试依赖](../Firmware/host/tests/requirements.txt)，建议装入单独环境：

```powershell
python -m venv .work/mcp-venv
./.work/mcp-venv/Scripts/python.exe -m pip install -r ./Firmware/host/tests/requirements.txt
./.work/mcp-venv/Scripts/python.exe ./Firmware/host/tests/test_mcp.py
```

不加参数时验证初始化、工具列表、包校验和错误返回，并枚举本机设备，但不执行音频参数写入或烧录。
输出 `passed: true` 及通过的检查列表。
加 `--hardware` 会实际调参后恢复原设置；同时加 `--flash` 才执行 USB 更新，不能把它们当作离线检查。

### 当前网表、ERC 与 DRC

`check_schematic.py` 的默认输入是历史网表。
检查当前工程必须先重新导出，并显式传入 `--netlist`：

```powershell
$kicad = 'C:/Program Files/KiCad/10.0/bin/kicad-cli.exe'
New-Item -ItemType Directory -Force .work/checks | Out-Null
& $kicad sch export netlist --format kicadxml --output .work/checks/netlist.xml Hardware/Coral-Wave.kicad_sch
python ./Hardware/tools/check_schematic.py --netlist .work/checks/netlist.xml --output .work/checks/topology.json
& $kicad sch erc --format json --output .work/checks/erc.json Hardware/Coral-Wave.kicad_sch
& $kicad pcb drc --format json --schematic-parity --output .work/checks/drc.json Hardware/Coral-Wave.kicad_pcb
```

KiCad 安装位置不同时修改 `$kicad`。
拓扑脚本当前预期 35 项检查、54 个器件；电路变动后，应审查每项约束，不能只追求保留通过数字。
读取原生 JSON 的违规明细，不能仅凭 CLI 退出码判断 ERC/DRC 通过。
本例不写回 PCB，也不修改制造规则。

## 2026-10-29 静态与离线复核

| 检查 | 本次结果 |
| --- | --- |
| 当前网表拓扑 | 35 项通过，54 个器件 |
| 原理图与 PCB 连接集合 | 已连接的多引脚网络集合一致 |
| KiCad 10.0.6 ERC | 0 条违规，沿用现有工程检查设置 |
| KiCad 10.0.6 DRC | 402 个错误、3 个警告；未连接 0、原理图一致性问题 0 |
| 源码构建 | 引导程序、USB 应用、GUI、CLI 及固件包在临时副本构建成功 |

本次没有消除 DRC 问题，也没有改变已有规则或忽略设置。
完整文档检查、命令验证及未执行项见[文档审查记录](documentation-review.md)。

## 已知问题与尚未验证内容

| 范围 | 状态与后续确认 |
| --- | --- |
| 制造规则 | 当前 DRC 各有 199 条过孔外径及环宽错误、4 条 USB2 内部孔距错误；丝印越板边 2 条、重叠 1 条。须按实际板厂与装配工艺处理，不能据连接全通直接放行制板 |
| 实物料号 | [照片](hardware.md#实物与设计图)与工程的 MCU、codec 完整后缀不同，采购订货码、封装及实物兼容性待确认 |
| SPK2 | 硬件预留，应用未实现完整 BTL 输出配置；没有扬声器功率、负载或温升测试 |
| 模拟性能 | 没有 SNR、THD、频响、最大输出及输入灵敏度测量；可听录放音不替代这些指标 |
| USB 与供电 | 未给出完整信号质量、ESD/EMC、插入浪涌、挂起电流、限流工作点及 LDO 温升实测；描述符声明 100 mA 不代表实测耗流或电源合规结论 |
| 长期与跨平台使用 | 已有记录是短时 Windows 测试；未验证长时间稳定性、全部 Windows 版本或 Linux/macOS 工具兼容性 |
| 更新完整性 | CRC 用于检错，没有固件签名或旧版本自动回滚；中断传输恢复不等于已覆盖所有真实断电时序 |
| 许可证 | 项目级授权尚未明确，第三方授权不能代替项目自有材料的授权说明 |

详细 PCB 几何及历史建议见[布局复核](../Documentation/Reports/pcb-review/2026-10-02-recheck-2347/review.md)，较新的模型与封装记录见[3D 模型复核](../Documentation/Reports/footprint-binding/2026-10-03/review.md)。
历史建议中的尺寸和位置属于各自文件基线，后续修改后需重新测量。

## 历史报告索引

| 阶段 | 记录 |
| --- | --- |
| 2026-09-22 原理图 | [初次连接复核](../Documentation/Reports/schematic-review/review.md)、[layout 前审查](../Documentation/Reports/schematic-review/layout-readiness.md)、[MCLK 断网复查](../Documentation/Reports/schematic-review/recheck-current.md)、[当晚更新](../Documentation/Reports/schematic-review/2026-09-22-evening/review.md) |
| 2026-09-23 精简与双时钟 | [双时钟复核](../Documentation/Reports/schematic-review/2026-09-23-dual-clock/review.md)、[工程快照](../Documentation/Reports/project-snapshot/2026-09-23/review.md) |
| 2026-10-02 PCB | [首次布局复核](../Documentation/Reports/pcb-review/2026-10-02/review.md)、[更新后复核](../Documentation/Reports/pcb-review/2026-10-02-recheck/review.md)、[23:47 复核](../Documentation/Reports/pcb-review/2026-10-02-recheck-2347/review.md) |
| 2026-10-03 模型 | [封装关联与 3D 模型](../Documentation/Reports/footprint-binding/2026-10-03/review.md) |
| 2026-10-14 初板与 USB | [更换 codec、缺少 Y2 阶段](../Documentation/Reports/board-test/2026-10-14/review.md)、[零样本 USB 传输](../Documentation/Reports/usb-audio/2026-10-14/review.md) |
| 2026-10-17 完整通路 | [录放音](../Documentation/Reports/audio/2026-10-17/review.md)、[上位机与 USB 更新](../Documentation/Reports/control/2026-10-17/review.md) |

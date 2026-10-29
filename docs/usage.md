# 使用方法

## 连接并播放或录音

Windows 需要 .NET Framework 4.8 运行环境。
以下步骤适用于已安装引导程序和应用的板子；空白主控先按[首次安装](firmware.md#首次通过-swd-安装)操作。

1. 用可传输数据的 USB-C 线连接板子与电脑。
2. 打开 [Coral-Wave-Control.exe](../Firmware/releases/Coral-Wave-Control.exe)，等待顶部显示设备序列号。
3. 在 Windows 输出设备中选择 `Coral-Wave Playback`，将耳机插入 CN2，从较低系统音量开始播放。
4. 录音时选择 `Coral-Wave Microphone`，音源为板载 MIC1。

当前格式及已验证通路见 [README](../README.md#已实现的功能)。
已有 Windows 原生音频驱动加载记录；正常 USB Audio/HID 操作不使用仓库外的专用声卡驱动。
这不覆盖每个 Windows 版本的兼容性保证。

## 调节音频参数

先“读取当前设置”，修改后点击“应用参数”，或勾选“改动后自动应用”。
“应用”只改变当前运行状态；要跨重启保留，还需要“保存到板子”。
默认耳机模拟增益为 −18 dB，麦克风 PGA 为 +18 dB，模拟提升开启。

下表为当前 GUI/MCP 暴露的可设范围，来源为 [MainForm.cs](../Firmware/host/MainForm.cs)、[McpServer.cs](../Firmware/host/McpServer.cs) 和[固件校验](../Firmware/usb-audio/control.c)，不是整板性能指标。

| 参数 | 可设范围或选项 | 作用及约束 |
| --- | --- | --- |
| 耳机模拟增益 | −57～+6 dB，1 dB 步进 | 左右声道联动 |
| DAC 数字余量 | −24～0 dB，0.5 dB 步进 | 提升 EQ/3D 后可降低电平留出余量 |
| 五段 EQ | 每段 −12～+12 dB，1 dB 步进 | 固定作用于 DAC 播放路径；频点使用界面给出的离散选项 |
| 3D 立体声扩展 | 0～15 档 | GUI 换算为百分比；固定作用于 DAC |
| 麦克风 PGA | −12～+35.25 dB，0.75 dB 步进 | 开启 ALC 后是起始增益 |
| 麦克风模拟提升 | 关闭 / +20 dB | 输入削顶时优先检查 PGA 与提升设置 |
| 高通 | 关闭 / 音频 / 语音 | 语音模式截止频率为 122、153、196、245、306、392、490、612 Hz |
| ADC/DAC 过采样 | 128 倍选项开关 | 不改变 USB 采样率 |
| DAC 限幅 | 开关，门限 −6～−1 dBFS | 不替代音量及模拟工作点核验 |
| ALC | 开关，目标 −22.5～−1.5 dBFS | 左通道；固件配置的自动增益范围为 −12～+23.25 dB |
| ALC 噪声门 | 开关，门限为 −39 至 −81 dBFS 的八档选项 | 必须同时开启 ALC；不是数字降噪 |

Windows 系统音量与这些硬件参数独立。
增加模拟增益、EQ 或 3D 后，重新检查耳机音量和录音削顶。
“建议余量”按 EQ 最大提升及 3D 状态给出 DAC 衰减值，不是输出无失真的测量保证。
读取操作返回固件设置镜像，不是 codec 硬件寄存器读回。

## 保存与预设

- **保存到板子**：先应用设置，停止全部播放和录音，再保存；设备会短暂重启。
- **导出预设**：在电脑上保存 JSON 文件。
- **导入预设**：先载入界面，再应用；开启自动应用时会按界面逻辑自动提交。
- **更新固件**：已保存的设置保留，未保存的运行中改动不会跨重启保留。

JSON 预设保存的是 `Headphone`、`MicPga` 等协议编码字段。
MCP 的 `headphone_db`、`microphone_pga_db` 是带物理单位的另一种接口，不能直接互换 JSON。

## 使用 CLI

以下 PowerShell 命令在仓库根目录执行。
列举设备无需改变板子设置：

```powershell
./Firmware/releases/Coral-Wave-CLI.exe --list
```

输出是设备数组，包含 `Serial` 和 `Bootloader` 等字段；没有设备时返回 `[]`。
除列举和本地包校验外，多设备操作需要先选择序列号：

```powershell
$env:CORAL_WAVE_SERIAL = '替换为 --list 返回的 Serial'
./Firmware/releases/Coral-Wave-CLI.exe --status
./Firmware/releases/Coral-Wave-CLI.exe --export ./my-preset.json
```

只连接一块板子时可不设置该环境变量。
`--status` 返回设备、固件信息、状态数组和参数镜像；状态布局见 [USB 协议](../Documentation/USB-Protocol.md)。
恢复预设及保存会改变设备状态：

```powershell
./Firmware/releases/Coral-Wave-CLI.exe --apply ./my-preset.json
./Firmware/releases/Coral-Wave-CLI.exe --save
```

执行 `--save` 前同样要停止播放和录音。
本轮操作后取消序列号筛选可运行 `Remove-Item Env:CORAL_WAVE_SERIAL`。
程序退出码为零表示成功，非零时查看标准错误。
当前 CLI 没有 `--help` 命令，入口及全部分支见 [Program.cs](../Firmware/host/Program.cs)。

USB 更新与恢复见[烧录说明](firmware.md#通过-usb-更新与恢复)，AI 客户端接入见 [MCP 文档](../Documentation/MCP.md)。

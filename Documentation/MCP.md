# AI 接入

本页维护 MCP 配置与工具用法；播放、调音和预设操作见[使用方法](../docs/usage.md)。

上位机的「AI / MCP 接入」页可以复制本机配置。支持 stdio MCP 的 AI 客户端使用同一个 `Coral-Wave-CLI.exe`，不需要另外安装服务或运行环境。

支持 `mcpServers` JSON 的客户端可参考下面的配置，把路径换成自己的实际位置。其他客户端分别填写命令、参数和 stdio 传输方式。

```json
{
  "mcpServers": {
    "coral-wave": {
      "command": "C:/Coral-Wave/Coral-Wave-CLI.exe",
      "args": ["--mcp"]
    }
  }
}
```

| 工具 | 用途 |
| --- | --- |
| `coral_list_devices` | 查 USB 设备和序列号 |
| `coral_get_status` | 查主控、WM8978 应答和录放音状态 |
| `coral_get_audio` / `coral_set_audio` | 读设置、按需修改音频参数 |
| `coral_save_to_board` | 停止录放音后保存设置，板子会重启 |
| `coral_validate_firmware` | 检查本地 `.cwfw` 包 |
| `coral_flash_firmware` | 经 USB 烧录并校验 |
| `coral_recover_firmware` | 等待拔插 USB 后恢复烧录 |
| `coral_enter_bootloader` / `coral_run_firmware` | 进入恢复模式、启动已装固件 |

例如调用 `coral_set_audio`，只传要改的项，其余设置保留。连了多块板子时还要传 `serial`。

```json
{
  "patch": {
    "eq_gain_db": [2, 0, -1, 1, 0],
    "dac_preamp_db": -6,
    "limiter": true,
    "limiter_threshold_dbfs": -3
  }
}
```

完整字段和范围由 `tools/list` 返回。工具按 [MCP 规范](https://modelcontextprotocol.io/specification/2025-11-25/server/tools)提供 JSON Schema、结构化结果和操作标记，烧录带进度通知。服务只走本地标准输入输出，不监听网络端口。固件路径须为绝对路径。

只想接脚本时可直接用命令行，退出码为零表示成功。
以下示例在仓库根目录执行；多块板子用环境变量 `CORAL_WAVE_SERIAL` 选择，详见 [CLI 用法](../docs/usage.md#使用-cli)。

```powershell
./Firmware/releases/Coral-Wave-CLI.exe --list
./Firmware/releases/Coral-Wave-CLI.exe --status
./Firmware/releases/Coral-Wave-CLI.exe --export ./my-preset.json
```

`--list` 返回设备数组，`--status` 返回所选设备状态，`--export` 写出协议编码格式的 JSON 预设。
CLI 预设不能直接作为 MCP `patch` 使用，后者使用 dB、Hz 等字段。
应用预设及 USB 更新会改变设备状态，步骤分别见[预设说明](../docs/usage.md#保存与预设)和[USB 更新](../docs/firmware.md#通过-usb-更新与恢复)。

WM8978 控制口只能写，读到的音频设置来自固件镜像，每次写入都会检查 I²C 应答。CRC 用于发现文件损坏，不是固件签名。当前版本只支持 Windows，录放音固定为 48 kHz、16 位。

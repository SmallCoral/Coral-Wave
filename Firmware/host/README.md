# Coral-Wave 上位机

双击 [Coral-Wave-Control.exe](../releases/Coral-Wave-Control.exe) 即可使用，Windows 需要 .NET Framework 4.8。板子接 USB-C，顶部出现序列号后可以调音或选 `.cwfw` 烧录。日常操作不需要 J-Link。

五段 EQ 为 ±12 dB，耳机模拟增益为 -57～+6 dB，麦克风 PGA 为 -12～+35.25 dB。还可调麦克风提升、高通、ALC、噪声门、DAC 限幅、3D 和过采样。提升 EQ 或 3D 时可以用「建议余量」降低数字电平，给后续处理留出空间。

「应用」立即生效，「保存到板子」会保留到下次上电。保存前先停掉视频、播放器和录音，保存后板子重启。JSON 预设也可在电脑上导入导出。系统音量与这些硬件参数独立。

USB 更新保留已保存的设置；未保存的改动会在重启后回到上次保存值。

AI 接入见 [MCP 说明](../../Documentation/MCP.md)，界面中也能直接复制配置。GUI、CLI 和 MCP 共享 USB 协议及校验代码，同一块板子会协调访问。

```powershell
./Firmware/host/build.ps1
python -m pip install -r ./Firmware/host/tests/requirements.txt
python ./Firmware/host/tests/test_mcp.py --hardware
```

最后一条会短暂改变音色，完成后恢复设置。加 `--flash` 会实际经 MCP 更新板子固件。

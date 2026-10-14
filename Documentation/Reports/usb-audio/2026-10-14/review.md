# USB 实测

SAMD21 已写入 Coral-Wave USB 固件并通过回读校验。Windows 加载 Microsoft `usbaudio` 驱动，播放和麦克风端点状态正常，设备错误码为 0。

一秒双向测试使用 48 kHz、16 位 PCM，播放提交 192,000 字节，采集返回 96,000 字节。主控累计接收 196,032 字节、发送 97,824 字节，包含 Windows 启停流期间的额外数据。主控错误码为 0，测试结束后两条流都关闭。

当前采集固件发送零样本，Windows 返回样本的最大绝对值为 1。本次只验证 USB 传输，未验证模拟输入输出。Y2 缺失，WM8978 的 I²S 音频尚未接入 USB。

[结构化结果](result.json) 记录主机和主控数据，[预编译固件](../../../../Firmware/releases/) 附带 SHA-256，可用 [Windows 测试脚本](../../../../Firmware/tools/test-usb-audio.ps1) 复测。

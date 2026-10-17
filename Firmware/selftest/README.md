# WM8978 实板自检

程序只加载到 SAMD21 的 RAM，结果在 `0x20007000`，不改 Flash 或熔丝。新换的 WM8978 已在 `0x1A` 应答，硬件 I²C 控制版完成 35 次写入，全部 ACK。[实测结果](../../Documentation/Reports/board-test/2026-10-14/replacement-codec-control-result.json) 保留在仓库里。

```powershell
./Firmware/selftest/build.ps1 -Mode Control -Transport Hardware
./Firmware/selftest/run.ps1 -Mode Control -Transport Hardware
```

编译脚本首次运行会下载 Microchip DFP 3.8.270 和 CMSIS 5.9.0。可用 `-Toolchain`、`-JLink` 指定工具位置。

音频自检把 `-Mode` 改为 `Audio`，默认需要 Y2 的 12.288 MHz 有源时钟。2026-10-17 补装 Y2 后，实板 I²S 收发已通过，正式 USB 录放音也已完成。若缺少 MCLK，音频版会报错误 4，不能据此判断 ADC/DAC 好坏。临时用 Y1 的 12 MHz 飞线测试时，编译和运行都加 `-CodecClock Pll12`，补装 Y2 前必须移除飞线。

默认输出静音。`-AudioCommand 1` 可测试低音量耳机正弦波，`2` 可测试麦克风回放，十秒后自动静音。

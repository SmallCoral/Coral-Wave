# 3D 模型

本目录保存项目专用 STEP 模型，用于 KiCad 3D 预览和整板 STEP 导出。其他标准器件使用 KiCad 10 的 3D 模型库。

| 器件 | 模型 | 来源 |
| --- | --- | --- |
| Y1、Y2 | [ASE.step](ASE.step) | Abracon 原厂 |
| MIC1 | [MIC-TH_BD4.0-P1.40-D0.4-L-FD.step](MIC-TH_BD4.0-P1.40-D0.4-L-FD.step) | 立创原库 |
| CN2 | [AUDIO-TH_PJ-3136-B_5pin.step](AUDIO-TH_PJ-3136-B_5pin.step) | 立创原库五脚适配版 |

CN2 保留原库外壳和触点细节，仅去除现有五脚封装不需要的板下插脚。这是项目适配模型；未经修改的 [六脚原库文件](AUDIO-TH_PJ-3136-B_original_6pin.step)一并保留。

模型已通过相对路径绑定到对应封装，无需手动配置。

[来源与文件校验信息](../../Documentation/Reports/footprint-binding/2026-10-03/original-model-sources.json) · [模型适配脚本](../tools/adapt_pj3136_5pin.py)

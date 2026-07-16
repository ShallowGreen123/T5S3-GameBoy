# EPD PaperBoy

面向 LilyGO T5S3-4.7-e-paper-PRO 的可触控 Game Boy 示例。界面参考
PaperBoy 掌机的布局，在竖屏安装的 540x960 显示区域上提供：

- 480x432、3 倍缩放的 Game Boy 画面；
- 十字键、A/B、SELECT/START 多点触控；
- `ON/OFF` 软暂停，以及内存中的 `SAVE/LOAD` 快速状态；
- Peanut-GB 核心、动态跳帧和 1bpp EPD 输出；
- 上电黑白全屏清除和可直接运行的合法自制标题 ROM。
- 540 条面板数据行直接扫描，不插入会造成边缘花屏的 dummy line。

`ON/OFF` 只暂停或恢复模拟器，不会关闭 EPD 电源。`SAVE/LOAD` 保存在 RAM
中，复位或断电后会丢失。

## 编译和烧录

在仓库根目录执行。根目录默认环境已经指向本示例：

```powershell
pio run
pio run -t upload
pio device monitor -b 115200
```

也可以把它作为独立项目构建：`pio run -d examples/epd_paperboy`。

默认触摸坐标直接使用 GT911 报告的 540x960 竖屏空间。如果触摸方向与显示不一致，可以在
`platformio.ini` 的 `build_flags` 中添加 `TOUCH_SWAP_XY`、`TOUCH_INVERT_X`
或 `TOUCH_INVERT_Y`，值设为 `1`。

## 使用自己的 ROM

仅使用你有权运行和分发的 `.gb` 文件。仓库不会附带商业游戏 ROM。

```powershell
python tools/gb_rom_to_header.py path/to/homebrew.gb examples/epd_paperboy/main/rom/test_rom.h
pio run -d examples/epd_paperboy
```

`rom/test_rom.h` 存在时会替代内置测试 ROM。删除该文件即可恢复测试 ROM。

## 控制

| 屏幕区域 | 功能 |
| --- | --- |
| ON/OFF | 暂停或恢复游戏 |
| SAVE | 将当前模拟器状态保存到内存 |
| LOAD | 恢复最近一次内存状态 |
| 十字键 | 方向，支持单指斜向和多点组合 |
| A / B | Game Boy A / B |
| SELECT / START | Game Boy SELECT / START |

串口每秒输出模拟帧、渲染帧、跳帧、VSYNC 丢失数、各阶段耗时和剩余内存。
GT911 还会每秒输出 `status`、`INT` 电平、轮询数和读取错误数；任意有效触点
都会在屏幕底部显示 `TOUCH x,y`，用于确认触摸方向和命中位置。

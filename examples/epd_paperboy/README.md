# EPD PaperBoy

面向 LilyGO T5S3-4.7-e-paper-PRO 的可触控 Game Boy 示例。界面参考
PaperBoy 掌机的布局，在竖屏安装的 540x960 显示区域上提供：

- 480x432、3 倍缩放的 Game Boy 画面；
- 十字键、A/B、SELECT/START 多点触控；
- `ON/OFF` 软暂停，以及内存中的 `SAVE/LOAD` 快速状态；
- `SETTING` 设置入口、电池状态、SD 卡游戏库占位页和系统信息页；
- 设置页支持屏幕 `BACK`、`HOME` 和 GT911 电容 HOME 键；
- 主页面长按 PCA9535 实体键 2 秒安全关机；
- Peanut-GB 核心、非阻塞动态跳帧和低残影 1bpp EPD 输出；
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
python tools/gb_rom_to_header.py "D:\ROMs\game.gb"
pio run -t clean
pio run -t upload
```

脚本默认生成 `examples/epd_paperboy/main/rom/test_rom.h`。该文件存在时会替代
内置演示 ROM；烧录完成后设备会直接启动该游戏。删除该文件、重新编译并烧录即可恢复
内置演示 ROM。

当前适合运行原版 Game Boy（DMG）兼容的 `.gb` ROM，不支持仅限 Game Boy Color 的
游戏，也暂未输出声音。MBC 等卡带特性的兼容性取决于 Peanut-GB；如果 ROM 不受支持，
屏幕和串口会显示 ROM 初始化错误。请只使用你合法拥有或获准使用的 ROM。

动态游戏默认使用固定空间位置的 2x2 有序抖动，把 Game Boy 的 4 阶灰度转换成 1bpp。
映射遵循 DMG 调色板的 `0=白色`、`3=黑色`，中间灰阶使用稳定网点，不进行容易闪烁
和产生拖影的时间抖动。最终 EPD 帧缓冲始终是每像素 1 bit。

面板使用三次成组驱动后才接收下一张游戏画面：模拟器维持约 60 FPS，EPD 实际更新约
8 FPS。三次驱动用于保证黑色密度；缩短驱动虽然会提高动态速度，但会让黑色明显变浅。

## 控制

| 屏幕区域 | 功能 |
| --- | --- |
| ON/OFF | 暂停或恢复游戏 |
| SAVE | 将当前模拟器状态保存到内存 |
| LOAD | 恢复最近一次内存状态 |
| SETTING | 进入设置页；打开设置页时游戏暂停运行 |
| 十字键 | 方向，支持单指斜向和多点组合 |
| A / B | Game Boy A / B |
| SELECT / START | Game Boy SELECT / START |
| 设置页 BACK | 返回上一级；设置首页返回游戏主页面 |
| 设置页 HOME / 电容 HOME | 直接返回游戏主页面 |
| 主页面长按 PCA9535 实体键 2 秒 | 显示关机页并通过 BQ25896 断开电池供电；USB 连接时进入深度睡眠 |

`Battery Status` 参考 T5S3-Reader，读取 BQ27220 的电量、电压、电流、容量、健康度、
温度和循环次数，并结合 BQ25896 显示 USB 与充电状态。`SD Card` 当前只提供页面和
交互占位，不会访问 SD 卡或从中加载游戏。

串口每秒输出模拟帧、渲染帧、跳帧、VSYNC 丢失数、各阶段耗时和剩余内存。
GT911 还会每秒输出 `status`、`INT` 电平、轮询数和读取错误数；每次按下的触点坐标
保留在串口日志中，避免屏幕底部反复重绘造成额外残影。

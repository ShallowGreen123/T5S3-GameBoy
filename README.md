

LilyGO 的 T5S3-4.7-e-paper-PRO 实现 60FPS Eink GameBoy Emulator

https://github.com/Modos-Labs/Glider
https://github.com/zephray/VerilogBoy
https://github.com/JIT2Holidays/gb-jit-xtensa

CrankBoy 仓库: https://github.com/CrankBoyHQ/crankboy-app
Playdate 官方规格: https://sdk.play.date/2.7.6/Inside%20Playdate.html#_playdate_specifications

https://www.hackster.io/wenting-zhang/60fps-eink-gameboy-emulator-on-m5papers3-57e4e5
https://www.tomshardware.com/video-games/retro-gaming/designer-turns-niche-e-ink-dev-board-into-a-60hz-game-boy-handheld-960x540-display-powered-by-ultra-low-cost-esp32-s3-microcontroller
https://hackaday.com/2026/06/13/behold-a-60-hz-refresh-rate-e-ink-monitor/

## 可触控 PaperBoy 示例

新增示例 [`examples/epd_paperboy`](examples/epd_paperboy/README.md)，在
T5S3 竖屏安装的 540x960 显示区域上实现可玩的掌机界面，包含 Game Boy 模拟器、
GT911 虚拟按键、ON/OFF 和内存 SAVE/LOAD。

```powershell
pio run
pio run -t upload
```

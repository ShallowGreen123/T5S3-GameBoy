# Custom ROM

Generate `test_rom.h` from a legal Game Boy ROM at the repository root:

```powershell
python tools/gb_rom_to_header.py "D:\ROMs\game.gb"
pio run -t clean
pio run -t upload
```
推荐从以下渠道找明确授权的 Game Boy 自制游戏：

https://hh.gbdev.io/search?typetag=game
https://itch.io/games/tag-gameboy
https://itch.io/jams/tag-gameboy

选择时注意：
- 文件最好直接是 .gb，不要选 .gbc。
- 页面标明 DMG、Original Game Boy 或 Game Boy compatible。
- GBC only、Game Boy Color only 当前不能运行。
- “老游戏”“绝版”或“abandonware”不等于合法免费下载。

The generated header must define `kTestRomData` and `kTestRomSize`. Do not commit
copyrighted commercial ROM data.

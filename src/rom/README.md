# Custom ROM

在项目根目录将合法的 DMG 兼容 Game Boy ROM 转换为固件头文件：

```powershell
python tools/gb_rom_to_header.py "D:\ROMs\game.gb"
pio run -t clean
pio run -t upload
```

默认输出为 `src/rom/test_rom.h`，并定义 `kTestRomData` 和 `kTestRomSize`。

可查找明确授权自制游戏的站点：

- <https://hh.gbdev.io/search?typetag=game>
- <https://itch.io/games/tag-gameboy>
- <https://itch.io/jams/tag-gameboy>

请选择标明 DMG、Original Game Boy 或 Game Boy compatible 的 `.gb` 文件。`GBC only`
游戏当前无法运行；“老游戏”“绝版”或 “abandonware” 也不代表可以合法下载。

请勿提交受版权保护的商业 ROM 数据。

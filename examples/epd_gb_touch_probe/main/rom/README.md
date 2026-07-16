# Embedded ROM

This example is runnable out of the box.

If `rom/test_rom.h` is missing, firmware falls back to a bundled legal homebrew demo ROM that animates the screen so you can verify the EPD pipeline immediately.

If you want to override that with your own ROM, place a generated header at `examples/epd_gb_touch_probe/main/rom/test_rom.h`.

Use only a ROM you have the right to distribute, such as your own homebrew build. Do not commit copyrighted commercial ROM data.

Generate the header with:

```bash
python tools/gb_rom_to_header.py path/to/your_homebrew.gb
```

That writes `examples/epd_gb_touch_probe/main/rom/test_rom.h` with the fixed symbols `kTestRomData` and `kTestRomSize` expected by `main.cpp`. When present, it takes priority over the bundled demo ROM.

If you want to keep multiple headers around, you can also pass an explicit output path:

```bash
python tools/gb_rom_to_header.py path/to/your_homebrew.gb examples/epd_gb_touch_probe/main/rom/test_rom.h
```

If your custom header is invalid, firmware logs the emulator init error and shows a static error screen.

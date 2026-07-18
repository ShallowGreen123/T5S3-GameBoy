# T5S3-GameBoy

[中文](README_CN.md) | **English**

A portrait-mode touchscreen Game Boy (DMG) emulator based on the [LilyGO T5S3-4.7-e-paper-PRO](https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO). Built with Peanut-GB, GT911, BQ27220, BQ25896, and a real-time 1bpp e-ink refresh scheme.

| ![](./docs/1.jpg) | ![](./docs/2.jpg) |
| --- | --- |

## Features

- 480×432 Game Boy display, converted to crisp 1bpp black-and-white output via fixed dithering.
- On-screen D-pad, A/B, SELECT/START multi-touch controls.
- In-memory save states and load states; saves are lost on power-off or reset.
- Settings, Battery Status, SD Card placeholder pages, and an About System page.
- BQ27220 fuel gauge and BQ25896 charge management; battery level and charging status shown on the home screen.
- `BOOT` button triggers a white→black→white screen clear and redraws the current page.
- Long-press the `IO48` physical button for 2 seconds on the home screen to clear the display, show a prompt, and safely shut down.
- Long-press `PWR` to power on.

## Project Structure

```text
T5S3-GameBoy/
├─ src/                   Main program, display, touch, and emulator source
│  ├─ gbcore/             Peanut-GB core
│  └─ rom/                Custom ROM notes and generated test_rom.h
├─ lib/                   BQ25896, BQ27220, and I²C compatibility layer
├─ boards/                LilyGO T5S3 PlatformIO board config
├─ tools/                 .gb ROM conversion tool
├─ firmware/              Release firmware
└─ platformio.ini         The sole project build configuration
```

## Build & Flash

Run these commands from the project root:

```powershell
pio run
pio run -t upload --upload-port COM45
pio device monitor -p COM45 -b 115200
```

The only PlatformIO environment name is `T5S3-GameBoy`.

## Using Your Own Game ROM

Using `maxpirateeb.gb` as an example:

1. Download the target game's `.gb` file from an authorized ROM site.
2. Place the `.gb` file in the project's `ROMs/` folder.
3. Run the following commands to flash it to the device (replace `COM45` with your port):

```powershell
python tools/gb_rom_to_header.py .\ROMs\maxpirateeb.gb
pio run -t clean
pio run -t upload --upload-port COM45
```

The tool generates `src/rom/test_rom.h` by default. When that file exists it replaces the built-in demo ROM; delete it and recompile to restore the built-in ROM. The generated file is excluded from Git by default to avoid accidentally committing copyright-protected data.

Game Boy Color-only `.gbc` games are not currently supported, and audio output is not yet implemented.

Sites that host explicitly licensed homebrew games:

- <https://hh.gbdev.io/search?typetag=game>
- <https://itch.io/games/tag-gameboy>
- <https://itch.io/jams/tag-gameboy>

Choose `.gb` files marked as DMG, Original Game Boy, or Game Boy compatible. `GBC only` games will not run. "Old", "out-of-print", or "abandonware" does not mean legally downloadable.

## Charging Parameters

Charging configuration matches the T5S3-Reader:

- Input current limit: 1000 mA
- Fast-charge current: 512 mA
- Pre-charge / termination current: 64 / 64 mA
- Charge voltage: 4208 mV
- Minimum system voltage: 3300 mV
- Battery model capacity: 1500 mAh

The firmware will not force-resume charging during NTC, temperature, or safety timer faults.

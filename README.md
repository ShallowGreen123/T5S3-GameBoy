# T5S3-GameBoy

[中文](README_CN.md) | **English**

A portrait-mode touchscreen Game Boy emulator for the [LilyGO T5S3-4.7-e-paper-PRO](https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO). It combines the CrankBoy emulator core with the board's GT911 touch controller, BQ27220/BQ25896 power hardware, SD card slot, and a real-time 1bpp e-paper refresh path. The ROM-library, persistence, and audio workflow is adapted from Paperboy while retaining the T5S3-specific display and power implementation.

| ![](./docs/1.jpg) | ![](./docs/2.jpg) |
| --- | --- |

## Features

- CrankBoy-based DMG emulation with a 480 x 432 game viewport and fixed dithering for crisp 1bpp output.
- GT911 multi-touch D-pad, A/B, SELECT/START, SAVE/LOAD, power, and settings controls.
- SD ROM library with sorted scanning of the card root and one subdirectory level. It recognizes `.gb` and `.gbc` files larger than 0 bytes and no larger than 4 MiB, and lists up to 64 ROMs.
- Library controls for previous/next selection, launching a ROM, restoring the last ROM, changing the sound engine, and rescanning the card.
- Persistent cartridge SRAM/RTC in a companion `.sav` file and a versioned full emulator snapshot in a companion `.state` file.
- `/paperboy.cfg` on the SD card remembers the last launched ROM and selected sound engine.
- Three runtime sound modes: PCM, POLY, and MUTE. PCM and POLY use LEDC PWM on a configurable external-audio GPIO.
- T5S3 EPD double buffering and real-time partial refresh, preserved from this project's hardware implementation.
- BQ27220 fuel-gauge and BQ25896 charge-management pages, home-screen battery status, and a prominent low-battery warning with 3500/3600 mV hysteresis.
- `BOOT` performs a white-black-white panel clean and redraws the current screen.
- Holding the on-board `S3` function button for two seconds on any screen saves pending persistent data, cleans the display, and shuts the system down safely. Hold `PWR` to start it again.

## SD ROM Library

Use a FAT32-formatted SD card and insert it before powering on. ROM files can be placed in the root directory or exactly one directory below it:

```text
/
|-- paperboy.cfg          generated and maintained by the firmware
|-- Tetris.gb
|-- Tetris.gb.sav         battery-backed SRAM/RTC for Tetris.gb
|-- Tetris.gb.state       full snapshot for Tetris.gb
`-- games/
    |-- Zelda.gbc         accepted only if the ROM is DMG-compatible
    |-- Zelda.gbc.sav
    `-- Zelda.gbc.state
```

Open **Settings > SD Card** to use the library:

- `PREV` / `NEXT` changes the selected entry and repeats while held; `PLAY` launches it.
- `LOAD LAST` opens the ROM recorded in `paperboy.cfg` and restores its `.state` snapshot when one is available.
- `SOUND` cycles through PCM, POLY, and MUTE. The selection is saved in `paperboy.cfg`.
- `RESCAN` refreshes the catalog on the currently mounted card.

The scanner is intentionally limited to 64 entries and does not recurse below the first subdirectory level. Extension checks are case-insensitive. A ROM must be larger than 0 bytes and no larger than 4 MiB (4,194,304 bytes); zero-length and oversized files are ignored and do not consume a catalog entry. A `.gbc` extension does not imply full Game Boy Color support: only ROMs that can run in DMG compatibility mode are supported. GBC-only games are not supported.

## Saves And Snapshots

For an SD-loaded ROM named `game.gb` or `game.gbc`, the game-screen `SAVE` action writes files beside that ROM:

- `game.gb.sav` / `game.gbc.sav` stores battery-backed cartridge SRAM and RTC data when the cartridge provides them.
- `game.gb.state` / `game.gbc.state` stores a versioned CrankBoy emulator snapshot for resuming the complete running state, including the MiniGB APU audio state.

`LOAD` restores the current ROM's snapshot. Its versioned APU section preserves audio registers, channel synthesis counters, and fractional frame/sample phase; the selected output engine and physical GPIO configuration still come from `paperboy.cfg` and the firmware build. Battery-backed data is also flushed before changing ROMs or shutting down. The firmware writes `paperboy.cfg`, `.sav`, and `.state` through a temporary file and backup/rename sequence so an interrupted update can recover the previous valid copy.

SD cards written by the original Paperboy firmware are read-compatible. Legacy `/sdcard/...` config entries are normalized, `game.sav` and `game.state` are tried when `game.gb.sav` and `game.gb.state` are absent, and raw CrankBoy snapshots plus PBSV v1 saves are accepted. New writes use the unambiguous sidecar names and PBSV v2; only a path too long for the appended name falls back to the shorter legacy filename. Paperboy v1 timestamps were boot-relative rather than wall-clock values, so they are not used for powered-off RTC catch-up; the saved RTC registers are still restored.

Snapshots are tied to the ROM and CrankBoy state format. Keep the `.sav` file as the durable game-progress record when moving data between firmware versions. The built-in or compile-time ROM has no companion SD path, so its quick SAVE/LOAD remains memory-only and is lost after reset or power-off.

## External Audio

The T5S3-4.7-e-paper-PRO has no on-board speaker. PCM and POLY can produce a one-wire PWM signal for an external filter/amplifier, but physical pin output is disabled by default (`PAPERBOY_AUDIO_GPIO=-1`). The APU and all three selectable sound modes remain active without claiming that this board contains a speaker.

After selecting a verified, isolated output, connect that GPIO to a suitable low-pass filter/amplifier input and connect grounds together. Do not drive a passive speaker directly from an ESP32-S3 GPIO. The output is mono even though the emulated APU mix is generated from the Game Boy channels.

`GPIO1` is the optional LoRa module's `LORA_RST` signal and is deliberately not used as the default audio pin. Driving it can conflict with or back-power that module. To enable physical output, choose a pin verified against your exact board and wiring, then append an override to the existing `build_flags` list in `platformio.ini`:

```ini
-DPAPERBOY_AUDIO_GPIO=YOUR_VERIFIED_GPIO
```

Confirm the alternate pin against the board schematic and [pin map](docs/pinmap.md) before wiring it. The SD interface itself uses MISO `GPIO21`, MOSI `GPIO13`, SCK `GPIO14`, and CS `GPIO12`.

## Project Structure

```text
T5S3-GameBoy/
|-- src/                   Application, display, touch, storage, and emulator code
|   |-- crankboy_core/     CrankBoy core and versioned state compatibility code
|   |-- minigb_apu/        Game Boy audio processing unit emulation
|   `-- rom/               Compile-time custom ROM notes and generated test_rom.h
|-- lib/                   BQ25896, BQ27220, and I2C compatibility libraries
|-- boards/                LilyGO T5S3 PlatformIO board configuration
|-- docs/                  Hardware pin maps and project images
|-- tools/                 .gb-to-header conversion tool
|-- firmware/              Release firmware
`-- platformio.ini         Project build configuration
```

## Build And Flash

Install PlatformIO, then run these commands from the project root:

```powershell
pio run
pio run -t upload --upload-port COM45
pio device monitor -p COM45 -b 115200
```

Replace `COM45` with the device's serial port. The only PlatformIO environment is `T5S3-GameBoy`.

## Compile A ROM Into The Firmware

The SD library is the normal way to add games. A legal DMG-compatible ROM can also be compiled into the firmware as a fallback. Using `maxpirateeb.gb` as an example:

1. Obtain the game's `.gb` file from a source that authorizes you to use it.
2. Place it in the project's `ROMs/` directory.
3. Generate the header, clean the old build, and flash the firmware:

```powershell
python tools/gb_rom_to_header.py .\ROMs\maxpirateeb.gb
pio run -t clean
pio run -t upload --upload-port COM45
```

The tool generates `src/rom/test_rom.h`. When present, this file replaces the built-in demo ROM; delete it and rebuild to restore the demo. The generated header is excluded from Git by default to reduce the risk of committing copyright-protected ROM data.

Sites that host explicitly licensed homebrew games include:

- <https://hh.gbdev.io/search?typetag=game>
- <https://itch.io/games/tag-gameboy>
- <https://itch.io/jams/tag-gameboy>

Choose ROMs marked DMG, Original Game Boy, or Game Boy compatible. Do not use a GBC-only ROM. "Old," "out of print," or "abandonware" does not by itself grant permission to download or redistribute a game; use only ROMs that you own or are licensed to use, and never commit commercial ROM data to this repository.

## Charging Parameters

The charging configuration matches the T5S3-Reader:

- Input-current limit: 1000 mA
- Fast-charge current: 512 mA
- Pre-charge / termination current: 64 / 64 mA
- Charge voltage: 4208 mV
- Minimum system voltage: 3300 mV
- Battery-model capacity: 1500 mAh

The firmware does not force charging to resume during NTC, temperature, or safety-timer faults.


## Landscape controls

Tap **ROTATE SCREEN** below the portrait game display to cycle through landscape,
reverse landscape, and portrait. Landscape keeps the original 480 x 432 game
image centered on the 960 x 540 panel, with the D-pad on the left and A/B on the
right. Select, Start, Save, Load, power, light controls and Settings remain
available. Settings, battery, SD browser and About retain their portrait layout;
returning to the game restores the selected orientation. Rotation takes effect
without restarting the emulator. Lift your fingers after rotating before using
the new controls. Orientation starts in portrait on boot.

### Automatic orientation hardware

LilyGO's H752-01 Pro and Pro Lite hardware inventory and schematic do not list
an onboard accelerometer. The bundled generic SensorLib includes accelerometer
drivers for other boards; their presence is not evidence of a fitted sensor.
In particular, address 0x6B on this board is the BQ25896 charger, not a QMI8658.
This implementation therefore provides manual rotation only. Automatic rotation
requires identification of an attached accelerometer (model, address, and axis
mounting) or a different board revision before a driver can safely be added.

Hardware reference: https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO/tree/H752-01

# Nintendo SNES/NES Classic Mini controller over I2C

This firmware supports the **SNES Classic Mini / NES Classic Mini I2C controller**, which uses the Wii-extension-style connector. A **1990s original SNES controller is not I2C** and cannot be connected directly. Use a suitable Wii-extension breakout or a correctly wired adapter; the controller's physical connector does not directly fit the T5S3 board header.

## Wiring

Connect the controller breakout to the T5S3 Pro's bottom I2C header, checking the actual connector orientation/pin labels before powering either device:

| SNES Mini breakout | T5S3 Pro header |
| --- | --- |
| SDA | GPIO39 / SDA |
| SCL | GPIO40 / SCL |
| VCC | **3.3 V only** |
| GND | GND |

**Do not use 5 V.** Do not assume a cable's conductor order or connect the legacy SNES plug to this header. The onboard PCA9535 is at `0x20`; the controller is at `0x52`. They share SDA/SCL, but each has its own slave address. The firmware already initializes the board `Wire` bus at 400 kHz and the driver reuses it without a second `Wire.begin()` or a whole-bus scan.

## Operation

- Connect the controller, open/play a Game Boy ROM, and use the physical D-pad, A/B, Start and Select. Hold **X for turbo A** or **Y for turbo B** (10 presses per second). A/B remain normal held buttons. Press **R to save** the current session and **L to load** the saved state, using the same save/load behavior as the touchscreen. Each bumper press acts once, even when held.
- Hold **Select** and press **L to dim** or **R to brighten** the backlight by one step per press, on any page. This chord suppresses save/load and consumes Select until it is released; Select pressed before the chord may already have reached the game. Brightness uses the existing persisted 0–10% PWM range (0 is off). Release and press the bumper again for another step. Both bumpers together are reserved for shortcuts.
- During gameplay, press **L + R + D-pad Right** to cycle portrait and the two landscape orientations. Holding the chord rotates only once; keep L/R held and tap Right again to cycle further. The chord and its release do not send Right to the game or trigger save/load.
- The touchscreen stays active: its buttons are ORed with the controller's button mask each emulated frame. Press **L + R + Start + Select together** to open Settings. The complete chord takes priority over save/load and brightness and is consumed until all four buttons are released. In Settings, **Up/Down** moves the outlined selection and **A** opens it. **B** returns from a view to Settings, or closes Settings to resume the game. In the SD card view, **Up/Down** scrolls the game list and **A** launches the selected game. Hold Up/Down to repeat after 500 ms, then every 150 ms. A/B act once per press; release them after a page change before using them again. X/Y turbo applies only to gameplay.
- The driver probes only `0x52` (once per second while absent). On detection it sends `F0 55` and `FB 00` initialization, requests register `00`, reads six bytes, and decodes the active-low buttons in bytes 4 and 5. It polls no faster than every 16 ms and uses a 2 ms report settling interval.
- Short reads, invalid reports and unplugging release all buttons immediately and initiate a new probe after a second. Connection and disconnection are logged at 115200 baud with the `snes_mini` tag. No SD configuration or new library dependency is necessary.

Protocol and mapping reference: Albert Gonzalez, [Connecting a (S)NES Mini controller to an Arduino](https://albertgonzalez.coffee/projects/snes_mini_arduino/) and his [published SNES/NES Mini controller driver](https://github.com/theisolinearchip/nesmini_usb_adapter/blob/main/nesminicontrollerdrv.c).

## Hardware validation

1. With no controller connected, boot and verify touchscreen, RTC, battery status and display still work, with no continuous controller I2C traffic or log spam.
2. Attach the controller using the labeled 3.3 V/GND/SDA/SCL signals. Start a ROM and verify all four D-pad directions and A/B/Start/Select. Hold multiple buttons simultaneously. Verify X/Y turbo repeats and normal A/B remain held. Press R, advance the game, then press L and verify restoration. Hold a bumper and verify only one save/load occurs. Hold Select and tap L/R to adjust brightness without saving/loading; release Select before the bumper and verify no save/load is triggered. Test brightness limits and persistence after reboot.
3. While holding a direction, unplug the controller. Confirm movement stops and touchscreen controls remain responsive; reattach and confirm automatic reconnection within about one second.
4. Open Settings with L+R+Start+Select. Verify no save/load/brightness action fires for the complete chord or its release. Navigate all three outlined options with Up/Down and A; use B to return and close Settings. In SD Card, scroll beyond the visible rows, launch with A, and confirm the launch/back buttons do not leak into the game.
5. Verify periodic battery polling, EPD refresh and the existing PCA9535 power button continue to function while the controller is connected.

The PlatformIO build can validate compilation and artifact creation, but real controller communication and connector pinout require an on-device test.

# T5S3 Pro night light

The LilyGO T5S3 4.7-inch E-Paper Pro has an LED illumination circuit controlled by GPIO11 (the boost driver's enable pin). The GameBoy firmware drives this pin with a 1 kHz, 10-bit LEDC PWM signal. Light PWM uses LEDC timer 2/channel 2, separate from the audio engine's timer 0/channel 0.

## Controls

- **During gameplay:** tap **LIGHT -** and **LIGHT +** on the dark bar immediately below the game display. Each tap changes the hardware PWM duty by **one percentage point**. The available range is **0% (off), 1% (dimmest), through 10% (maximum)**. The old 10%-brightness output is now the maximum.
- **Settings > Battery + Light:** the current hardware PWM percentage and 10% maximum are displayed. Use **OFF**, **DIM -**, and **BRIGHT +**. OFF disables illumination without changing emulator state.
- Brightness is stored in the ESP32's NVS and restored at startup, including when no SD card is present. On first boot the default is **OFF**. Previously saved brightness values above 10% are clamped to 10% on startup so an update cannot unexpectedly restore the old higher output.
- Light changes happen without pausing the emulator or waiting for an e-paper screen refresh. The Settings page redraws its percentage after a change; during gameplay, the light changes immediately without redrawing the screen.

## Hardware and testing

The control assumes the LilyGO T5S3-4.7-e-paper-PRO board with the GPIO11-connected PT4103B23F light driver. It does not apply to unlit e-paper variants or boards wired differently. Confirm on hardware that 0% extinguishes the LEDs, 1% is suitably dim, 10% matches the old firmware's 10% output, and the audio engine still works. PlatformIO CI can validate firmware compilation without a physical device; LED illumination must be verified on the board.

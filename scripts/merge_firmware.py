"""PlatformIO post-build hook: combine the exact images used by the uploader.

firmware.bin is the application image for address ESP32_APP_OFFSET (normally
0x10000). firmware-merged.bin contains the bootloader, partition table,
boot_app0 and application, and is flashed at address 0x0.

Use PlatformIO's FLASH_EXTRA_IMAGES and ESP32_APP_OFFSET rather than assuming
an offset or bootloader layout for the custom T5S3 board.
"""

import os

Import("env")  # noqa: F821; provided by PlatformIO/SCons

board = env.BoardConfig()


def merge_firmware(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    merged = os.path.join(build_dir, env.subst("${PROGNAME}") + "-merged.bin")
    extra_images = env.get("FLASH_EXTRA_IMAGES", [])
    if not extra_images:
        raise RuntimeError("PlatformIO did not provide bootloader/partition flash images")

    # The private __get_board_f_image helper expands to an empty string with
    # espressif32@6.5.0. Read the declared board settings instead.
    flash_hz = int(str(board.get("build.f_flash", "80000000L")).rstrip("Ll"))
    if flash_hz % 1000000:
        raise RuntimeError(f"Unsupported board flash frequency: {flash_hz} Hz")
    flash_frequency = f"{flash_hz // 1000000}m"
    flash_mode = board.get("build.flash_mode", "qio")

    command = [
        '"$PYTHONEXE"',
        '"$OBJCOPY"',
        "--chip", board.get("build.mcu", "esp32s3"),
        "merge_bin",
        "-o", '"' + merged + '"',
        "--flash_mode", flash_mode,
        "--flash_freq", flash_frequency,
        "--flash_size", board.get("upload.flash_size", "16MB"),
    ]
    for offset, image in extra_images:
        command.extend((str(offset), '"' + env.subst(image) + '"'))
    command.extend(("$ESP32_APP_OFFSET", '"$BUILD_DIR/${PROGNAME}.bin"'))

    print(f"GameBoy flash layout: mode={flash_mode}, freq={flash_frequency}, extra_images={extra_images}")
    result = env.Execute(env.VerboseAction(" ".join(command), "Merge GameBoy firmware -> " + merged))
    if result:
        raise RuntimeError("Failed to create merged GameBoy firmware")
    if not os.path.isfile(merged) or os.path.getsize(merged) == 0:
        raise RuntimeError("Merged GameBoy firmware missing or empty: " + merged)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_firmware)

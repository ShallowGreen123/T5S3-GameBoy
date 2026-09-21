# Building and releasing T5S3-GameBoy

The only supported PlatformIO target in this repository is `T5S3-GameBoy`, for the LilyGO T5S3-4.7 E-Paper Pro. The build uses the custom `boards/T5-ePaper-S3.json` and the version in `[version]` of `platformio.ini`.

## Download a development build

Open **Actions → GameBoy PlatformIO Build → Run workflow** and select `master`. Pushes to `master` and pull requests also build automatically. Download the `gameboy-t5s3-<commit>` artifact from the successful run. This is a GitHub Actions artifact; GitHub automatically downloads it as a ZIP. It does not create a tag or GitHub Release.

The build contains:

| File | Use |
| --- | --- |
| `T5S3-GameBoy-vVERSION-app.bin` | Application image only; flash at **0x10000** on a compatible installation. Never flash at 0x0. |
| `T5S3-GameBoy-vVERSION-merged.bin` | Complete USB image including bootloader, partition table, and app; flash at **0x0**. Not an app-only/OTA image. |
| `T5S3-GameBoy-vVERSION.elf` | Debug symbols; do not flash. |
| `SHA256SUMS` | SHA-256 hashes for the three outputs. |

The staging script rejects missing/empty files, invalid ESP32 headers, a missing embedded version, and merged images that do not contain the exact app at 0x10000. The merged image uses PlatformIO's actual upload images and offsets.

## Release individual files when tagging

1. Update `[version]` in `platformio.ini` (for example, to `1.2.1`) and merge the change into `master`.
2. Tag that commit and push the tag, for example `git checkout master && git pull && git tag v1.2.1 && git push origin v1.2.1`. Push a Git tag rather than publishing an empty GitHub Release yourself; the workflow creates the Release with its assets.
3. **Cut GameBoy release** starts automatically. It verifies that the tag matches the embedded version and points to a commit on `master`, builds and checks the images, and creates the GitHub Release.
4. Open **Releases → v1.2.1 → Assets**. Download the desired `.bin` directly. The application binary, merged binary, ELF, and `SHA256SUMS` appear as **four independent release assets, not a ZIP**.

A tagged release deliberately does not call `actions/upload-artifact`, which always creates ZIP downloads. GitHub's automatically provided **Source code (zip/tar.gz)** entries may still appear below the assets; these are source archives, not firmware binaries.

You can still use **Actions → Cut GameBoy release → Run workflow** on `master` with a matching tag, or set `.github/release-request.json` to `"enabled": true` with the matching `"tag"` and push it on `master`. These routes create the tag if needed and publish the same four individual files. The JSON request is initially disabled. Publishing never overwrites an existing release or reuses a tag pointing at a different commit, and it does not commit binary outputs back into source control.

## Local build and flashing

```sh
python -m pip install platformio==6.1.19
pio run -e T5S3-GameBoy
python scripts/stage_firmware.py
(cd dist && sha256sum -c SHA256SUMS)
```

For a clean installation over USB serial, replace the port and version below with your own:

```sh
python -m pip install esptool
python -m esptool --chip esp32s3 --port YOUR_PORT write_flash 0x0 dist/T5S3-GameBoy-v1.2.1-merged.bin
```

Only for an existing compatible installation, application-only flashing uses the *app* binary at address `0x10000`. Never interchange these images or offsets. Direct PlatformIO programming remains supported with `pio run -e T5S3-GameBoy -t upload --upload-port YOUR_PORT`.

The GitHub build ships the built-in demo ROM; an untracked local `src/rom/test_rom.h` is absent on GitHub Actions runners. Release workflows do not download or bundle external game ROMs.

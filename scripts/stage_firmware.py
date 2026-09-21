#!/usr/bin/env python3
"""Stage validated, versioned GameBoy firmware images after a PlatformIO build."""

import configparser
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / ".pio" / "build" / "T5S3-GameBoy"
DIST = ROOT / "dist"
APP_OFFSET = 0x10000


def version_from_ini():
    config = configparser.ConfigParser(interpolation=None)
    if not config.read(ROOT / "platformio.ini"):
        raise RuntimeError("platformio.ini is missing")
    version = config.get("version", "version")
    if not version or any(character not in "0123456789." for character in version):
        raise RuntimeError("Expected numeric dotted [version] version in platformio.ini")
    return version


def main():
    version = version_from_ini()
    source = {name: BUILD / name for name in ("firmware.bin", "firmware-merged.bin", "firmware.elf")}
    for name, path in source.items():
        if not path.is_file() or path.stat().st_size == 0:
            raise RuntimeError(f"Build did not produce a nonempty {path}")

    app = source["firmware.bin"].read_bytes()
    merged = source["firmware-merged.bin"].read_bytes()
    if app[:1] != b"\xe9" or merged[:1] != b"\xe9":
        raise RuntimeError("ESP32 bootloader/application header missing (expected 0xE9)")
    if merged[APP_OFFSET:APP_OFFSET + len(app)] != app:
        raise RuntimeError("Merged image does not contain the exact app at 0x10000")
    if version.encode("ascii") not in app:
        raise RuntimeError(f"Application binary does not contain its configured version {version}")

    DIST.mkdir(exist_ok=True)
    stem = f"T5S3-GameBoy-v{version}"
    output = {
        f"{stem}-app.bin": source["firmware.bin"],
        f"{stem}-merged.bin": source["firmware-merged.bin"],
        f"{stem}.elf": source["firmware.elf"],
    }
    checksum_lines = []
    for filename, src in output.items():
        data = src.read_bytes()
        (DIST / filename).write_bytes(data)
        checksum_lines.append(f"{hashlib.sha256(data).hexdigest()}  {filename}")
        print(f"Staged {filename} ({len(data)} bytes)")
    (DIST / "SHA256SUMS").write_text("\n".join(checksum_lines) + "\n", encoding="utf-8")
    print(f"Verified application version {version}; merged app offset 0x{APP_OFFSET:x}")


if __name__ == "__main__":
    main()

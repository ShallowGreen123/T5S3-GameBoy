#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path


DEFAULT_OUTPUT = Path("examples/epd_paperboy/main/rom/test_rom.h")
BYTES_PER_LINE = 12


def render_header(input_path: Path, rom_bytes: bytes) -> str:
    lines = [
        "// Auto-generated from: {}".format(input_path.as_posix()),
        "#pragma once",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "static const uint8_t kTestRomData[] = {",
    ]

    for start in range(0, len(rom_bytes), BYTES_PER_LINE):
        chunk = rom_bytes[start : start + BYTES_PER_LINE]
        encoded = ", ".join(f"0x{byte:02X}" for byte in chunk)
        suffix = "," if (start + BYTES_PER_LINE) < len(rom_bytes) else ""
        lines.append(f"    {encoded}{suffix}")

    lines.extend(
        [
            "};",
            "",
            "static const size_t kTestRomSize = sizeof(kTestRomData);",
            "",
        ]
    )
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    if len(argv) < 2 or len(argv) > 3:
        print("Usage: python tools/gb_rom_to_header.py <input.gb> [output.h]", file=sys.stderr)
        return 1

    input_path = Path(argv[1]).resolve()
    output_path = Path(argv[2]).resolve() if len(argv) == 3 else DEFAULT_OUTPUT.resolve()

    if not input_path.is_file():
        print(f"Input ROM not found: {input_path}", file=sys.stderr)
        return 1

    rom_bytes = input_path.read_bytes()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(render_header(input_path, rom_bytes), encoding="utf-8", newline="\n")
    print(f"Wrote {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

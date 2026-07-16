# Custom ROM

Generate `test_rom.h` from a legal Game Boy ROM at the repository root:

```powershell
python tools/gb_rom_to_header.py path/to/homebrew.gb examples/epd_paperboy/main/rom/test_rom.h
```

The generated header must define `kTestRomData` and `kTestRomSize`. Do not commit
copyrighted commercial ROM data.

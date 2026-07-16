# Custom ROM

Generate `test_rom.h` from a legal Game Boy ROM at the repository root:

```powershell
python tools/gb_rom_to_header.py "D:\ROMs\game.gb"
pio run -t clean
pio run -t upload
```

The generated header must define `kTestRomData` and `kTestRomSize`. Do not commit
copyrighted commercial ROM data.

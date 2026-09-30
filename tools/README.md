# tools

| Path | What |
|---|---|
| `check_reset_path.py` | runs on every build: follows the code that runs without globals (reset launch, wrappers inside other applications) in the 68k disassembly and fails on any A5-relative access |
| `check_sections.py` | the native ARM code must have only a `.text` section |
| `gen_offsets.py` | offsets of the native entry points for the 68k side |
| `make_icon.py` | the hand-drawn 1-bit launcher icons |
| `icon/` | the colour and grey icons: `render.swift` draws them (CoreGraphics, macOS), `convert.py` writes the BMP depths pilrc accepts. The BMPs are committed, building needs neither |
| `emulator/` | CloudpilotEmu with a writable PXA real-time clock (upstream ignores clock writes, so nothing that sets the clock can be tested); `build.sh <dir>` builds and serves it |
| `probes/` | small diagnostic applications from the investigation (export table layout, form layout) - not part of DateFix |

# tools

| Path | What |
|---|---|
| `check_reset_path.py` | runs on every build: follows the code that runs without globals (reset launch, wrappers inside other applications) in the 68k disassembly and fails on any A5-relative access |
| `check_sections.py` | the native ARM code must have only a `.text` section |
| `gen_offsets.py` | offsets of the native entry points for the 68k side |
| `make_icon.py` | the hand-drawn 1-bit launcher icons |
| `icon/` | the colour and grey icons: `render.swift` draws them (CoreGraphics, macOS), `convert.py` writes the BMP depths pilrc accepts. The BMPs are committed, building needs neither |
| `emulator/` | CloudpilotEmu with a writable PXA real-time clock (upstream ignores clock writes, so nothing that sets the clock can be tested); `build.sh <dir>` builds and serves it |
| `probes/` | small diagnostic applications from the investigation (export table layout, form layout) - not part of DateFix; `yearprobe.c` is the test fixture of the per-application patches (`src/apppatch.c`) |
| `yearfinder/yearfinder.py` | scans a `.prc` for places that turn a `DateType` year into a real year (`year + 1904`) and classifies what happens to it: `DRAW`/`CALLDRAW` (shown with StrIToA/StrPrintF, directly or through the application's own functions) are candidates for the per-application patch table; `API`, `CALL`, `CMP`, `STORE` are not. Needs `m68k-palmos-objdump`. Example: `python3 tools/yearfinder/yearfinder.py DateBk3h.prc [--all] [--json out.json]`; `--table NAME CREATOR` prints the `addi.w #1904,Dn` candidates as lines for `apps/apps.txt` |
| `apptable.py` | turns `apps/apps.txt` into the application table: the resource built into DateFix.prc (`--resource`, used by `make`) or `DateFixApps.pdb` (`--pdb`, `make apps`) to update the table without a new DateFix; `--list` shows what the table contains |

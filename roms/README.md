# ROM images

Ostrich 7765 needs dumps of the machines' sound ROMs. **None are included, and none ever
will be.** Each machine plays once its own images are found; the others can be missing.

The images are recognised by their contents (SHA-1), so the file names do not matter. The
checksums of the known dumps follow once the ROM analysis is done (ROADMAP.md, phase 1).

| Machine | Images | Size |
|---|---|---|
| TR-505 | one sound ROM (raw or decoded dumps are both accepted) | 128 KiB |
| TR-626 | one sound ROM | 256 KiB |
| TR-707 | four sound ROMs: IC34 and IC35 (the voices), IC19 (crash), IC22 (ride) | 32 KiB each |
| TR-727 | the sound ROMs (not yet dumped by anyone we know of) | -- |

Where they go:

- Windows: `C:\ProgramData\Ostrich 7765\roms\`
- Linux: `~/.local/share/Ostrich 7765/roms/` (or `$XDG_DATA_HOME/Ostrich 7765/roms/`)

The environment variable `OSTRICH_ROMS` names a folder searched first.

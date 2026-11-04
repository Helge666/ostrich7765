# ROM images

Ostrich 7765 needs dumps of the machines' sound ROMs. **None are included, and none ever
will be.** Each machine plays once its own images are found; the others can be missing.

The images are recognised by their contents (SHA-1), so the file names do not matter.

| Machine | Images | Size |
|---|---|---|
| TR-505 | one sound ROM (raw or decoded dumps are both accepted) | 128 KiB |
| TR-626 | one sound ROM | 256 KiB |
| TR-707 | four sound ROMs: IC34 and IC35 (the voices), IC19 (crash), IC22 (ride) | 32 KiB each |
| TR-727 | the sound ROMs (not yet dumped by anyone we know of) | -- |

Known dumps:

| Machine | Image | SHA-1 |
|---|---|---|
| TR-505 | raw dump | `6441d3e7b53aff4511b23021dc854b7a5cc57689` |
| TR-505 | pin order (often named "TR-505 Firmware.bin") | `14e80b926aed1e78cb027752dfbbe51d9b3dcf4d` |
| TR-505 | decoded | `70f622f07fd94bcbfdb731f303ab4e8df845469f` |
| TR-626 | sound ROM | `d7891edecc68a0652d2cad82a637430ee8568818` |
| TR-707 | IC34 | `fd2bfe67d4d03d2b2134aa7feebe9167c44b1f8d` |
| TR-707 | IC35 | `5cc866f345906d817147ae2a61bc36d7be926511` |
| TR-707 | IC19 (crash) | `965e23ad71e1f95d56307fa67272725dff46ba67` |
| TR-707 | IC22 (ride) | `6c7c0f002ed66e4ccf182a4538d9bb239623ac43` |

Where they go:

- Windows: `C:\ProgramData\Ostrich 7765\roms\`
- Linux: `~/.local/share/Ostrich 7765/roms/` (or `$XDG_DATA_HOME/Ostrich 7765/roms/`)

The environment variable `OSTRICH_ROMS` names a folder searched first.

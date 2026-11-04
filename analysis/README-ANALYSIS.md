# What is known about the hardware

One file per machine, filled in phase 1 (ROADMAP.md). Every finding says where it comes
from:

- **measured**: on a real unit (the owner's TR-505 and TR-626);
- **ROM** / **firmware**: read in a dump (only the TR-727's firmware is at hand);
- **documented**: from a manual, service notes or a published analysis (named);
- **derived**: inferred from another machine, not checked on this one.

| File | Machine | State |
|---|---|---|
| TR-505.md | TR-505 | sound ROM decoded; measured: note map, names, 25 kHz, velocity, timing; engine checked against the ROM |
| TR-626.md | TR-626 | sound ROM decoded; measured: note map, names, 25 kHz, velocity, timing; engine checked against the ROM |
| TR-707.md | TR-707 | sound ROMs split at their rates; engine (MAME's circuit model) checked against the ROMs; firmware not at hand |
| TR-727.md | TR-727 | firmware: MIDI receive, note map, velocity to accent, 2.8 ms trigger grid; main-loop period open |

Sources at hand (outside the repository, with the owner): the four sound-ROM sets of the
TR-505, TR-626 and TR-707, the TR-727 firmware (OS v1.0), and Chris Kann's notes on the
TR-707's sound hardware (voice slots, clock rates, DACs, ROM pinouts).

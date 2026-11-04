# Ostrich 7765

## About the project

A DAW plugin (VST3, CLAP, standalone; Windows and Linux) that plays the sounds of four
sample-based Roland drum machines of the 1980s from dumps of their real sound ROMs: TR-505,
TR-626, TR-707 and TR-727. Only the sound generation is rebuilt, triggered by MIDI; the
sequencers are not. No firmware runs on an emulated CPU. What the units do with their sound
data (rates, pitch, accent, envelopes, voice sharing, the analogue stages, MIDI timing) is
worked out from the ROMs, the TR-727 firmware, documentation and measurements of real
units, and written down in `analysis/`.

README.md says what it does, BUILDING.md how to build it, ROADMAP.md what is open,
analysis/ what is known about the hardware.

## Rules

- **Never commit Roland data**: no sound ROM, firmware, manual, service notes or
  schematic, not even briefly. Recordings of a real unit stay out of the repository too.
  `.gitignore` covers the known formats; check `git status` before every commit.
- Make commits; do not push to the public repository. Pushing is done together with the
  owner, step by step.
- Do not contact other developers or projects.
- Compile with at most `-j5` and `nice -n 19` on the build host; it stays responsive.
- On the build host change no audio or video settings and open no audio device (it streams
  its desktop); listening, recording and DAW tests happen on the owner's Windows machine.
- Ask before drawing windows on the owner's desktop.
- Do not remove anything in `analysis/`; mark what is obsolete instead.
- Record what is learnt about a unit in `analysis/TR-<model>.md`, and keep
  `analysis/README-ANALYSIS.md` up to date. Say for every finding where it comes from:
  measured on a real unit, read in the ROM or firmware, taken from documentation, or
  derived (inferred from another unit).
- Before a listening test, measure pitch and length against the hardware.
- Do not offer to run commands as root; say what needs installing.
- The user interface is English; the conversation with the owner is German.

## Facts that are easy to get wrong

- All four machines play at the same time, each on a MIDI channel of its own; the tabs of
  the panel only choose what is shown.
- No firmware dump exists for the TR-505, TR-626 or TR-707. Only the TR-727's firmware is
  at hand (its hardware is the TR-707's). The owner has a working TR-505 and TR-626 for
  measurements; the TR-727's sound ROMs are not yet at hand.
- The version is in `plugin/ostrich/Version.hpp`, nowhere else; CI refuses a `v*` tag that
  does not match it.
- DPF is our fork Helge666/DPF (branch `logan`, pinned to a tag); nothing is patched at
  build time and `plugin/dpf` stays clean. Changes to DPF are commits in the fork.
- On Windows, files written by tools inside a packaged (MSIX) app under `%LOCALAPPDATA%` are
  invisible to the DAW; use `C:\ProgramData`.

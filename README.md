# Ostrich 7765

The sounds of four sample-based **Roland drum machines** of the 1980s as a VST3/CLAP plugin
and a standalone program: **TR-505, TR-626, TR-707 and TR-727**. A non-commercial fan
project.

> **Work in progress.** Nothing plays yet: the plugin builds, opens and shows its panel.
> See [ROADMAP.md](ROADMAP.md).

Ostrich 7765 plays the machines' **original sound ROMs**, not recordings of them: a
recording always carries the recording hardware with it. Around the ROM data it rebuilds
what each machine does with it: sample rates, pitch, accent, envelopes, which sounds share
a voice, and the analogue filters and output stages. It does not run the machines'
firmware, and it has no sequencer: the sounds are played by MIDI.

You need your own ROM images. **This repository contains no Roland data and never will.**

## What it will do

- **Four machines in one plugin**, each on its own tab and its own MIDI channel (TR-505 10,
  TR-626 11, TR-707 12, TR-727 13; set on the SETTINGS page), all playing at the same time.
  The TR-727 is prepared but needs its sound ROMs, which have not been dumped yet.
- **A panel per machine** with a level per sound and the machine's own controls where it
  has them (such as the TR-626's pitch).
- **16 mono outputs** besides the stereo mix: a sound assigned to one of them leaves the
  mix.
- **Vintage timing (optional):** notes on the grid on which the original CPU handled MIDI,
  with its jitter.

Formats: VST3, CLAP and a standalone program on Windows (64-bit); VST3, CLAP and a JACK
standalone on Linux. No macOS build yet.

## Building

[BUILDING.md](BUILDING.md). What is known about the hardware:
[analysis/](analysis/README-ANALYSIS.md). What is planned: [ROADMAP.md](ROADMAP.md).

## Licences

Ostrich 7765 is distributed under the GNU General Public License, version 3 or later
([LICENSE](LICENSE)). Third-party code and its licences: [THIRD-PARTY.md](THIRD-PARTY.md).

"Roland", "TR-505", "TR-626", "TR-707" and "TR-727" are trademarks of Roland Corporation.
This project is not affiliated with Roland.

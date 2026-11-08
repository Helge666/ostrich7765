# Roadmap

The approach: the machines' sound ROMs, played the way the machines play them. No firmware
on an emulated CPU and no sequencer; MIDI triggers the sounds. Everything the machines do
with their data is worked out from the ROMs, the TR-727 firmware, documentation and
measurements of a real TR-505 and TR-626, and recorded in [analysis/](analysis/README-ANALYSIS.md).

Decisions so far:

- One plugin, four machines playing at the same time, each on a MIDI channel of its own;
  the panel's tabs (TR-505, TR-626, TR-707, TR-727, Settings) only choose the view.
- Outputs: the stereo mix and 16 mono outputs, a pool shared by all machines. Each column
  (decided 2026-11-08: as LEVEL and PAN, not per sound) can be assigned to one of them and
  then leaves the mix.
- Note mapping (decided 2026-11-04): every machine keeps its original note map on its own
  MIDI channel (TR-505 35-68, TR-626 35-75, TR-707 35-56, TR-727 60-74; analysis/). An
  octave offset per machine cannot put them all on one channel, since the maps span up to
  three and a half octaves; a one-channel layout (the machines' sounds in blocks next to
  each other) can come later as an option.
- Vintage timing: the timing grid of the original CPU's MIDI handling, plus CPU/MIDI
  jitter, optional (as in VPROM 3.0).
- Version in `plugin/ostrich/Version.hpp`; a `v*` tag publishes a release, `preview-*` is
  the dry run.

## Phase 0: the scaffold

- [x] Repository, licence (GPL-3.0-or-later), AGENTS.md, `.gitignore` against ROM formats.
- [x] DPF (our fork, `logan-1`) as a submodule.
- [x] An empty plugin that builds: VST3, CLAP, standalone; the stereo mix and 16 mono
      outputs (off until the host switches them on); the panel with its tabs; the version.
- [x] CI: Linux and Windows on GitHub, smoke test, zips; `v*` publishes a release.
- [x] GitHub repository with topics; first push together with the owner.
- [x] DPF fork (`logan-2`): the CLAP descriptor's version string, and the port type of CLAP
      buses of a group of our own (mono outputs).

## Phase 1: ROM analysis

- [x] Per machine in `analysis/`: ROM layout, sample boundaries, playback rates, decoding
      (the TR-505's and TR-626's scrambled dumps), known dumps and their SHA-1.
- [x] TR-727 firmware: MIDI handling, accent, note tables, timing (2.8 ms grid). Open: the
      main loop's period. Its hardware is the TR-707's; what carries over is marked as
      derived.
- [x] TR-505 and TR-626 measured (`tools/measure/`): note maps, names, rates, velocity
      curves, timing. Open: the bass drums' rate (+1.4 % on both).
- [x] The original MIDI note maps, verified; mapping decided (above).

## Phase 2: sound generation

- [ ] TR-707 first (best documented; engine checked against the ROMs, by ear, and playing in the plugin's mix on its MIDI channel, heard right in the owner's DAW on Windows, 2026-11-04): playback at the original rates, 8-bit multiplying
      DAC with accent, 6-bit DACs for crash and ride, the closed hi-hat's VCA, sounds that
      share a voice cutting each other, sample-and-hold steps, resampling to the host rate.
      Against a recording of a real unit (2026-11-06, TR-707.md): hat VCA, diode band
      passes, highs done; open: the bass drum's spectrum.
      Open, no date: an A/B with a real TR-707 on site, under equal conditions (single hits at
      several velocities, unprocessed): HIGHS, the 0.47 uF EGs, the bass drum, soft snares.
      The HIGHS knob may go again after that, if the unit shows it is not needed.
- [ ] TR-505, TR-626 (with pitch), TR-727 (prepared; testable once ROMs exist). TR-505 and TR-626 play in the plugin (25 kHz, measured accent, checked against the ROMs); TR-505 hi-hats and cymbals in the sample order measured on the unit, its envelopes, levels, pan, envelope pulse and centre-bus limit (measured) and channel groups (documented); TR-626 the same, all measured: sample order as is, envelopes, eight channels, levels, pan, envelope pulse; open: the 626's pitch, both units below velocity 127.

## Phase 3: the analogue stages

- [ ] Schematics and service notes; filters and output stages per machine, fitted to the
      recordings, switchable as in Ostrich 220.

## Phase 4: the plugin

- [x] ROM setup per tab (as in Ostrich 220): choose a file (or a zip), the rest of its
      folder is found by content and copied into the ROM folder, scrambled dumps put in
      order; the machine starts at once. Each machine's page lists its images and where
      they came from.
- [x] Panel in the style of `Research/gui_example.png`: a column per group of sounds
      with its LEVEL (an automatable parameter), pads with their notes that play when
      clicked, the machine's VOLUME (dB). Two looks, VINTAGE (default) and DARK, kept for
      all instances in `panel.txt` (Windows `%ProgramData%\Ostrich 7765`, Linux
      `~/.config/Ostrich 7765`).
- [x] The machines' own controls on their pages: the TR-626's PITCH per sound (-7..+7,
      its manual; measured: a 1.6 MHz clock over a count, the upward step per sound, 28 of 30 still to measure). The TR-707 gets no accent knob:
      over MIDI its velocity sets the accent (TR-727 firmware; TR-707.md).
- [x] PAN per column, defaults as on the units; flam while a key note is held, per machine
      (analysis/TIMING.md); the TR-707's HIGHS (TR-707.md).
- [x] MIDI channel per machine, the mono-output assignment per column (mono there, at the
      level it has in the mix, PAN left out), session state (a CLAP state saved and loaded
      gives the same output, bit for bit; in a VST3 host still to be tried).

## Phase 5: vintage timing

- [x] Grid and spread per machine from the firmware and the measurements, plus the MIDI
      line; switchable per machine (analysis/TIMING.md). VPROM 3.0 itself was not looked
      into. On by default (the owner's A/B tests against recordings). Open: the TR-707/727
      main loop's period.

## Phase 6: release 0.1.0

- [ ] Test in Cubase; README with screenshot; Windows and Linux zips.
- [ ] TR-727 sound ROMs once available; macOS later.

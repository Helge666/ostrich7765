# Vintage timing

When a note reaches a unit's sounds, and how unevenly. With VINTAGE TIMING on (per machine,
SETTINGS page; the parameter `<machine>_timing`; on by default since 2026-11-06, the owner
found it closer to recordings of the units in A/B tests) Ostrich 7765 plays a note when the
unit would; off, at its frame. The plugin has no way to play a note
earlier than it arrives, so the unit's whole delay is added (a few milliseconds), not only
its spread. Code: `vintage()` in `plugin/ostrich/OstrichPlugin.cpp`.

## The MIDI line (all machines)

- **documented** (MIDI 1.0): 31250 baud, ten bits per byte: 0.32 ms a byte, a note on of
  three bytes 0.96 ms. A unit can act on a message only once its last byte is in.
- **derived:** messages sent at the same moment (a chord, or note offs together with the
  next notes) follow each other on the line. The model keeps, per machine, the moment its
  line is free again: a message sent onto a free line takes all its bytes, one sent while
  the line is busy follows the one before without its status byte (running status, as
  sequencers send). Note offs play nothing, but take their time on the line. The model
  takes one line (one MIDI port) per machine.

## TR-707 and TR-727

- **firmware** (TR-727 OS, TR-727.md, "Timing"): MIDI is parsed every 2.8 ms; the voices
  the main loop has prepared fire 0.7 ms after that. The model: a free-running 2.8 ms grid
  (where it stands is chosen per plugin instance, as a unit's depends on when it was
  switched on); a note fires 0.7 ms after the first grid point at or after it is in.
  Notes parsed together fire together and share one accent, which the engine already does
  for notes started in the same sample (Tr707.hpp).
- **derived:** the TR-707's OS is not at hand; it runs on the same board, so it is taken
  to time the same.
- **Open:** the main loop's period: a note the main loop takes up later than 0.7 ms after
  the parse slips by a round (2.8 ms). Not modelled.

## TR-505 and TR-626

- **measured** (TR-505.md, TR-626.md, "timing"): no service period shows; 300 bass drum
  notes at random intervals spread over 1.7 ms (TR-505, standard deviation 0.36 ms) and
  1.55 ms (TR-626, 0.27 ms), the PC's USB MIDI included.
- **derived:** the model adds to each note, after the line, a random delay of that
  deviation, about normal, clipped to 0-4.8 deviations (TR-505 0-1.7 ms, TR-626 0-1.3 ms,
  mean 2.4 deviations); notes never overtake each other. Since the measurement includes
  USB MIDI, the units' own spread is at most this; the share of each is not separable
  without a reference.

## Flam

- **documented** (owner's manuals): the TR-626 has a flam per voice and step for snares and
  toms, interval 0-4 per pattern ("the first weakly and then the second stronger"); the
  TR-707 on snare and toms, the TR-727 on congas and timbales, four intervals independent of
  the tempo. The TR-505 has none. No manual mentions flam over MIDI.
- **firmware** (TR-727 OS): the table at `FF38`, 8, 11, 15, 20, counts down in the 2.8 ms
  phase: **22.4, 30.8, 42 and 56 ms**. The second hit fires the same voices with accent + 3
  (velocity + 6).
- **derived:** the TR-707 the same (same board); the TR-505 and TR-626 take the same table.
- **Ours:** a latch on a key note per machine (SETTINGS; default A0, MIDI note 21 with middle C
  as C4; hosts that call note 60 C3 show it as A-1). A note on switches flam; the key plays
  nothing. A start of the transport switches it off, so that playback does not depend on
  where the last run stopped. All sounds flam, not only those of the units. Flam on: a hit
  at velocity - 6 where the note is, then the note at its velocity, the interval late: a
  plugin cannot play early. Parameters `<machine>_flam_key`, `_flam_interval`, `_flam_on`.

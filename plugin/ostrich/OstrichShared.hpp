// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the DSP and the panel share: the machines, the parameters, the states, the outputs.

#ifndef OSTRICH_SHARED_HPP_INCLUDED
#define OSTRICH_SHARED_HPP_INCLUDED

#include <cstdint>

namespace ostrich {

// ---- the machines ----------------------------------------------------------------------

enum Machine : uint32_t
{
	kTR505 = 0,
	kTR626,
	kTR707,
	kTR727,
	kNumMachines
};

constexpr uint32_t kMachineCount = kNumMachines;   ///< for arithmetic with other enums

constexpr const char *kMachineNames[kNumMachines] = { "TR-505", "TR-626", "TR-707", "TR-727" };
constexpr const char *kMachineSymbols[kNumMachines] = { "tr505", "tr626", "tr707", "tr727" };

// The panel's tabs: one per machine, then the settings.
constexpr uint32_t kTabSettings = kNumMachines;
constexpr uint32_t kNumTabs = kNumMachines + 1;

// ---- the columns of a machine's page ---------------------------------------------------

// A pad: its label and the note it plays (the machine's own note map). A column: a group of
// sounds with one LEVEL control. TR-707 and TR-727: the mixer's ten channels and their
// level sliders, in channel order (Tr707::Channel). TR-505 and TR-626: no level controls on
// the units; ours, grouped by instrument.
struct Pad { const char *label; uint8_t note; };
struct Column { const char *title; Pad pads[3]; uint32_t numPads; };

constexpr uint32_t kMaxColumns = 13;

constexpr Column kColumns505[] = {
	{ "BASS DRUM", { { "BD", 35 } }, 1 },
	{ "SNARE DRUM", { { "SD", 38 } }, 1 },
	{ "LOW TOM", { { "LT", 41 } }, 1 },
	{ "MID TOM", { { "MT", 45 } }, 1 },
	{ "HI TOM", { { "HT", 48 } }, 1 },
	{ "TIMBALE", { { "TI", 65 } }, 1 },
	{ "CONGA", { { "LC", 63 }, { "HC", 62 } }, 2 },
	{ "RIM SHOT", { { "RS", 37 } }, 1 },
	{ "HAND CLAP", { { "CP", 39 } }, 1 },
	{ "COWBELL", { { "LB", 68 }, { "HB", 67 } }, 2 },
	{ "HI-HAT", { { "CH", 42 }, { "OH", 46 } }, 2 },
	{ "CRASH", { { "CC", 49 } }, 1 },
	{ "RIDE", { { "RD", 51 } }, 1 },
};
constexpr Column kColumns626[] = {
	{ "BASS DRUM", { { "B1", 35 }, { "B2", 36 } }, 2 },
	{ "SNARE DRUM", { { "S1", 38 }, { "S2", 40 }, { "S3", 58 } }, 3 },
	{ "LOW TOM", { { "L1", 41 }, { "L2", 43 } }, 2 },
	{ "MID TOM", { { "M1", 45 }, { "M2", 47 } }, 2 },
	{ "HI TOM", { { "H1", 48 }, { "H2", 50 } }, 2 },
	{ "RIM SHOT\nHAND CLAP", { { "RS", 37 }, { "CP", 39 } }, 2 },
	{ "CONGA", { { "MC", 62 }, { "HC", 63 }, { "LC", 64 } }, 3 },
	{ "TIMBALE", { { "TH", 65 }, { "TL", 66 } }, 2 },
	{ "COWBELL\nAGOGO", { { "CB", 56 }, { "AH", 67 }, { "AL", 68 } }, 3 },
	{ "TAMBOURINE\nSHAKER CLAVES", { { "TB", 54 }, { "SH", 69 }, { "CL", 75 } }, 3 },
	{ "HI-HAT", { { "CH", 42 }, { "OH", 46 } }, 2 },
	{ "CRASH\nCHINA", { { "CC", 49 }, { "CN", 52 } }, 2 },
	{ "RIDE\nCUP", { { "RD", 51 }, { "CU", 53 } }, 2 },
};
constexpr Column kColumns707[] = {
	{ "BASS DRUM", { { "B1", 35 }, { "B2", 36 } }, 2 },
	{ "SNARE DRUM", { { "S1", 38 }, { "S2", 40 } }, 2 },
	{ "LOW TOM", { { "LT", 41 } }, 1 },
	{ "MID TOM", { { "MT", 45 } }, 1 },
	{ "HI TOM", { { "HT", 48 } }, 1 },
	{ "RIM SHOT\nCOWBELL", { { "RS", 37 }, { "CB", 56 } }, 2 },
	{ "HAND CLAP\nTAMBOURINE", { { "CP", 39 }, { "TB", 54 } }, 2 },
	{ "HI-HAT", { { "CH", 42 }, { "OH", 46 } }, 2 },
	{ "CRASH", { { "CC", 49 } }, 1 },
	{ "RIDE", { { "RD", 51 } }, 1 },
};
constexpr Column kColumns727[] = {
	{ "BONGO", { { "HB", 60 }, { "LB", 61 } }, 2 },
	{ "CONGA", { { "MC", 62 }, { "HC", 63 } }, 2 },
	{ "LOW CONGA", { { "LC", 64 } }, 1 },
	{ "HI TIMBALE", { { "TH", 65 } }, 1 },
	{ "LOW TIMBALE", { { "TL", 66 } }, 1 },
	{ "AGOGO", { { "AH", 67 }, { "AL", 68 } }, 2 },
	{ "CABASA\nMARACAS", { { "CA", 69 }, { "MA", 70 } }, 2 },
	{ "WHISTLE", { { "WS", 71 }, { "WL", 72 } }, 2 },
	{ "QUIJADA", { { "QU", 73 } }, 1 },
	{ "STAR CHIME", { { "SC", 74 } }, 1 },
};

struct Columns { const Column *columns; uint32_t count; };
constexpr Columns kColumns[kNumMachines] = {
	{ kColumns505, 13 }, { kColumns626, 13 }, { kColumns707, 10 }, { kColumns727, 10 },
};

// ---- parameters --------------------------------------------------------------------------

// The level parameters of the columns, machine after machine: kParamLevel + kLevelOffset[m] + c.
constexpr uint32_t kLevelOffset[kNumMachines + 1] = { 0, 13, 26, 36, 46 };

enum Param : uint32_t
{
	kParamVolume = 0,           ///< master volume, dB
	kParamChannel,              ///< + machine: its MIDI channel, 1..16 (kDefaultChannel)
	kParamMachineVolume = kParamChannel + kMachineCount,   ///< + machine: its volume, dB
	kParamLevel = kParamMachineVolume + kMachineCount,      ///< see kLevelOffset: 0..1
	kParamCount = kParamLevel + 46
};

constexpr uint32_t levelParam(uint32_t machine, uint32_t column)
{
	return kParamLevel + kLevelOffset[machine] + column;
}

static_assert(kLevelOffset[1] - kLevelOffset[0] == kColumns[0].count
              && kLevelOffset[2] - kLevelOffset[1] == kColumns[1].count
              && kLevelOffset[3] - kLevelOffset[2] == kColumns[2].count
              && kLevelOffset[4] - kLevelOffset[3] == kColumns[3].count
              && kParamCount == kParamLevel + kLevelOffset[4], "a level per column");

// Each machine starts on a channel of its own, so that all can play at once (the units
// themselves all start on 10).
constexpr int kDefaultChannel[kNumMachines] = { 10, 11, 12, 13 };

// ---- states ------------------------------------------------------------------------------

enum State : uint32_t
{
	kStateUiSize = 0,           ///< host session: the panel's size, "<w> <h>" unscaled
	kStateTab,                  ///< host session: the tab shown, "0".."4"
	kStateRomSet,               ///< DSP -> UI: the ROM images found, see below
	kStateRomFile,              ///< UI -> DSP, "<seq> <machine> <path>": a file the user picked
	kStateCount
};

// "romset": lines of tab-separated fields.
//   "img" <RomImage> <the file it came from, as shown; empty if missing>
//   "dir" <the folder picked images are copied to>
//   "msg" <machine> <seq of the request> <text>   (the answer to the last "romfile")
constexpr char kRomSetFieldSep = '\t';

// ---- outputs -----------------------------------------------------------------------------

// Ports 0/1 are the stereo mix; ports 2..17 are OUT 1..16, mono, a bus each.  The mono buses
// are off until the user switches them on in the host (our DPF fork); a sound assigned to one
// of them leaves the mix.
constexpr uint32_t kNumMonoOuts = 16;
constexpr uint32_t kFirstMonoPort = 2;
constexpr uint32_t kPortGroupMonoOut = 100;   ///< + 0..15: the group (= bus) of OUT 1..16

} // namespace ostrich

#endif // OSTRICH_SHARED_HPP_INCLUDED

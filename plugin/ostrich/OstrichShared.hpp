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

// ---- parameters --------------------------------------------------------------------------

enum Param : uint32_t
{
	kParamVolume = 0,           ///< master volume, dB
	kParamChannel,              ///< + machine: its MIDI channel, 1..16 (kDefaultChannel)
	kParamCount = kParamChannel + kMachineCount
};

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

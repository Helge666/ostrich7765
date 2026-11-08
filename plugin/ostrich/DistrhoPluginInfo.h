// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// Ostrich 7765: the TR-505, TR-626, TR-707 and TR-727 in one plugin.  The IDs are fixed
// from the first public version on; a host takes a plugin with other IDs for another one.
#define DISTRHO_PLUGIN_BRAND   "Ostrich 7765"
#define DISTRHO_PLUGIN_NAME    "Ostrich 7765"
#define DISTRHO_PLUGIN_URI     "urn:ostrich7765:tr"
#define DISTRHO_PLUGIN_CLAP_ID "org.ostrich7765.tr"

#define DISTRHO_PLUGIN_BRAND_ID  O776
#define DISTRHO_PLUGIN_UNIQUE_ID TR77

#define DISTRHO_PLUGIN_HAS_UI           1
#define DISTRHO_PLUGIN_IS_SYNTH         1
#define DISTRHO_PLUGIN_IS_RT_SAFE       1
#define DISTRHO_PLUGIN_NUM_INPUTS       0
#define DISTRHO_PLUGIN_NUM_OUTPUTS      18    // MIX L/R, then OUT 1..16 (mono, see OstrichShared.hpp)
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT  1
#define DISTRHO_PLUGIN_WANT_MIDI_OUTPUT 0

// The panel's size and tab go into the host's session through state (hints 0): a
// parameter edit from the UI reaches the DSP under VST3 only through the host's automation
// path, a state message directly.
#define DISTRHO_PLUGIN_WANT_STATE       1
#define DISTRHO_PLUGIN_WANT_FULL_STATE  1
#define DISTRHO_PLUGIN_WANT_TIMEPOS     1     // transport start: flam goes off

#define DISTRHO_UI_USE_NANOVG           1
// Design units = these pixels at 100 %.  Both sides divisible by 4, so that every Windows
// scaling in quarter steps gives whole pixels (125 %: 1625 x 730) and a host that converts
// to its own units and back -- Cubase -- has nothing to round.
#define DISTRHO_UI_DEFAULT_WIDTH        1300
#define DISTRHO_UI_DEFAULT_HEIGHT       584
#define DISTRHO_UI_USER_RESIZABLE       1
#define DISTRHO_UI_FILE_BROWSER         1     // the ROM setup (phase 4)

#define DISTRHO_PLUGIN_LV2_CATEGORY "lv2:InstrumentPlugin"
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Drum"
#define DISTRHO_PLUGIN_CLAP_FEATURES "instrument", "drum-machine", "sampler", "stereo"

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED

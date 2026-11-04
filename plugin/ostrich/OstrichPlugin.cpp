// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ostrich 7765 -- the DSP side.  Phase 0 (ROADMAP.md): the plugin's frame without sound.
// It has its outputs (the stereo mix and 16 mono outputs), takes MIDI, and keeps the
// panel's size and tab in the host's session; the machines follow in phase 2.

#include "DistrhoPlugin.hpp"
#include "OstrichShared.hpp"
#include "Version.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

START_NAMESPACE_DISTRHO

using namespace ostrich;

class OstrichPlugin : public Plugin
{
public:
	OstrichPlugin()
		: Plugin(kParamCount, 0, kStateCount)
	{
	}

protected:
	// ---- information ---------------------------------------------------------------------

	const char *getLabel() const override { return "Ostrich7765"; }
	const char *getMaker() const override { return "Ostrich 7765 project"; }
	const char *getLicense() const override { return "GPL-3.0-or-later"; }
	const char *getHomePage() const override { return "https://github.com/Helge666/ostrich7765"; }
	uint32_t getVersion() const override
	{
		return d_version(OSTRICH_VERSION_MAJOR, OSTRICH_VERSION_MINOR, OSTRICH_VERSION_PATCH);
	}
	int64_t getUniqueId() const override { return d_cconst('O', '7', '7', '6'); }
	const char *getDescription() const override
	{
		return "The Roland TR-505, TR-626, TR-707 and TR-727 played from their original sound "
		       "ROMs. Requires the user's own ROM images.";
	}

	// ---- parameters ----------------------------------------------------------------------

	void initParameter(uint32_t index, Parameter &p) override
	{
		if (index == kParamVolume)
		{
			p.hints = kParameterIsAutomatable;
			p.name = "Volume";
			p.symbol = "volume";
			p.unit = "dB";
			p.ranges.def = 0.0f;
			p.ranges.min = -40.0f;
			p.ranges.max = 12.0f;
		}
	}

	float getParameterValue(uint32_t index) const override
	{
		return index == kParamVolume ? m_volumeDb : 0.0f;
	}

	void setParameterValue(uint32_t index, float value) override
	{
		if (index == kParamVolume)
			m_volumeDb = value;
	}

	// ---- outputs -------------------------------------------------------------------------

	// The mix is the stereo main bus; OUT 1..16 are a mono bus each, off until the user
	// switches them on in the host (our DPF fork makes every group after the first an extra
	// main bus that starts off).
	void initAudioPort(bool input, uint32_t index, AudioPort &port) override
	{
		if (input)
			return Plugin::initAudioPort(input, index, port);
		if (index < kFirstMonoPort)
		{
			port.name = index == 0 ? "MIX L" : "MIX R";
			port.symbol = index == 0 ? "mix_l" : "mix_r";
			port.groupId = kPortGroupStereo;
			return;
		}
		const uint32_t n = index - kFirstMonoPort + 1;
		char name[16], symbol[16];
		std::snprintf(name, sizeof(name), "OUT %u", unsigned(n));
		std::snprintf(symbol, sizeof(symbol), "out_%u", unsigned(n));
		port.name = name;
		port.symbol = symbol;
		port.groupId = kPortGroupMonoOut + n - 1;
	}

	void initPortGroup(uint32_t groupId, PortGroup &group) override
	{
		if (groupId >= kPortGroupMonoOut && groupId < kPortGroupMonoOut + kNumMonoOuts)
		{
			const unsigned n = unsigned(groupId - kPortGroupMonoOut + 1);
			char name[16], symbol[16];
			std::snprintf(name, sizeof(name), "OUT %u", n);
			std::snprintf(symbol, sizeof(symbol), "out_%u", n);
			group.name = name;
			group.symbol = symbol;
			return;
		}
		Plugin::initPortGroup(groupId, group);
	}

	// ---- state ---------------------------------------------------------------------------

	void initState(uint32_t index, State &state) override
	{
		switch (index)
		{
		case kStateUiSize:
			// Kept here only so that the host saves it with the session: the panel sets it
			// when the user resizes it, and gets it back when it opens (OstrichUI.cpp).
			state.key = "uisize";
			state.label = "Panel size";
			break;
		case kStateTab:
			state.key = "tab";
			state.label = "Panel tab";
			break;
		}
		state.hints = 0;
		state.defaultValue = "";
	}

	String getState(const char *key) const override
	{
		std::lock_guard<std::mutex> lock(m_stateLock);
		if (std::strcmp(key, "uisize") == 0)
			return String(m_uiSize.c_str());
		if (std::strcmp(key, "tab") == 0)
			return String(m_tab.c_str());
		return String();
	}

	void setState(const char *key, const char *value) override
	{
		std::lock_guard<std::mutex> lock(m_stateLock);
		if (std::strcmp(key, "uisize") == 0)
			m_uiSize = value;
		else if (std::strcmp(key, "tab") == 0)
			m_tab = value;
	}

	// ---- audio ---------------------------------------------------------------------------

	void run(const float **, float **outputs, uint32_t frames, const MidiEvent *, uint32_t) override
	{
		// No machine plays yet: every output is silent.
		for (uint32_t c = 0; c < DISTRHO_PLUGIN_NUM_OUTPUTS; c ++)
			std::memset(outputs[c], 0, sizeof(float) * frames);
	}

private:
	float m_volumeDb = 0.0f;

	mutable std::mutex m_stateLock;
	std::string m_uiSize, m_tab;

	DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OstrichPlugin)
};

Plugin *createPlugin()
{
	return new OstrichPlugin();
}

END_NAMESPACE_DISTRHO

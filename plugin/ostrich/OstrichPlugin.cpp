// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ostrich 7765 -- the DSP side: the outputs (the stereo mix and 16 mono outputs), MIDI per
// machine, the panel's size and tab in the host's session.  Phase 2 (ROADMAP.md): the
// TR-707 plays into the mix; the other machines and the mono outputs follow.

#include "DistrhoPlugin.hpp"
#include "OstrichShared.hpp"
#include "Version.hpp"

#include "engine/Resampler.hpp"
#include "engine/Roms.hpp"
#include "engine/Tr707.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

using namespace ostrich;

class OstrichPlugin : public Plugin
{
public:
	OstrichPlugin()
		: Plugin(kParamCount, 0, kStateCount)
	{
		for (uint32_t m = 0; m < kNumMachines; m ++)
			m_channel[m] = 10;
		m_roms.scan();
		if (m_roms.has(kRom707IC34) && m_roms.has(kRom707IC35) && m_roms.has(kRom707IC19)
		    && m_roms.has(kRom707IC22))
		{
			// IC34, then IC35 (both 32 KiB, checked by their SHA-1).
			const auto &ic34 = m_roms.image(kRom707IC34), &ic35 = m_roms.image(kRom707IC35);
			m_voices707.resize(ic34.size() + ic35.size());
			std::memcpy(m_voices707.data(), ic34.data(), ic34.size());
			std::memcpy(m_voices707.data() + ic34.size(), ic35.data(), ic35.size());
			m_tr707 = std::make_unique<Tr707>(false);
			m_tr707->setRoms({ m_voices707.data(), m_roms.image(kRom707IC19).data(),
			                   m_roms.image(kRom707IC22).data() });
		}
		sampleRateChanged(getSampleRate());
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
		else if (index >= kParamChannel && index < kParamChannel + kMachineCount)
		{
			const uint32_t m = index - kParamChannel;
			p.hints = kParameterIsInteger;
			p.name = String(kMachineNames[m]) + " MIDI channel";
			p.symbol = String(kMachineSymbols[m]) + "_channel";
			p.ranges.def = 10.0f;
			p.ranges.min = 1.0f;
			p.ranges.max = 16.0f;
		}
	}

	float getParameterValue(uint32_t index) const override
	{
		if (index == kParamVolume)
			return m_volumeDb;
		if (index >= kParamChannel && index < kParamChannel + kMachineCount)
			return float(m_channel[index - kParamChannel]);
		return 0.0f;
	}

	void setParameterValue(uint32_t index, float value) override
	{
		if (index == kParamVolume)
			m_volumeDb = value;
		else if (index >= kParamChannel && index < kParamChannel + kMachineCount)
			m_channel[index - kParamChannel] = std::clamp(int(std::lround(value)), 1, 16);
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

	void sampleRateChanged(double rate) override
	{
		if (rate <= 0.0)
			return;
		// Input of the longest segment run() renders. The first one after a reset also fills
		// the resampler's window (up to 2 * kHalf more).
		m_maxIn = int(std::ceil(kMaxSegment * Tr707::kRate / rate)) + 2 * Resampler::kHalf + 8;
		m_resampler707.init(Tr707::kRate, rate, 2, m_maxIn);
		m_in707L.assign(size_t(m_maxIn), 0.0f);
		m_in707R.assign(size_t(m_maxIn), 0.0f);
		m_out707L.assign(kMaxSegment, 0.0f);
		m_out707R.assign(kMaxSegment, 0.0f);
	}

	void activate() override
	{
		m_resampler707.reset();
		if (m_tr707)
			m_tr707->reset();
	}

	void run(const float **, float **outputs, uint32_t frames, const MidiEvent *events,
	         uint32_t eventCount) override
	{
		for (uint32_t c = 0; c < DISTRHO_PLUGIN_NUM_OUTPUTS; c ++)
			std::memset(outputs[c], 0, sizeof(float) * frames);
		if (!m_tr707)
			return;

		const float gain = std::pow(10.0f, m_volumeDb / 20.0f) * kTr707Trim;
		uint32_t pos = 0, ev = 0;
		while (pos < frames)
		{
			// Notes start at the segment that begins at their frame.
			for (; ev < eventCount && events[ev].frame <= pos; ev ++)
				midi(events[ev]);
			uint32_t end = std::min(frames, pos + kMaxSegment);
			if (ev < eventCount)
				end = std::min(end, std::max(events[ev].frame, pos + 1));
			const int n = int(end - pos);
			const int nIn = m_resampler707.inputFor(n);
			if (nIn > m_maxIn)   // cannot happen (sampleRateChanged); never read past the buffers
				break;
			m_tr707->render(nIn, m_in707L.data(), m_in707R.data(), nullptr);
			const float *in[2] = { m_in707L.data(), m_in707R.data() };
			float *out[2] = { m_out707L.data(), m_out707R.data() };
			m_resampler707.process(in, nIn, out, n);
			for (int i = 0; i < n; i ++)
			{
				outputs[0][pos + i] += m_out707L[i] * gain;
				outputs[1][pos + i] += m_out707R[i] * gain;
			}
			pos = end;
		}
		for (; ev < eventCount; ev ++)
			midi(events[ev]);
	}

private:
	// The longest piece run() renders at once, in host frames.
	static constexpr uint32_t kMaxSegment = 128;
	// The TR-707's level in the mix: MAME's output stage peaks at about +1.3 dBFS for a bass
	// drum at velocity 127; -6 dB leaves room for several sounds at once.
	static constexpr float kTr707Trim = 0.5f;

	void midi(const MidiEvent &e)
	{
		if (e.size < 3 || e.size > MidiEvent::kDataSize)
			return;
		const uint8_t status = e.data[0] & 0xf0, channel = (e.data[0] & 0x0f) + 1;
		const uint8_t note = e.data[1] & 0x7f, velocity = e.data[2] & 0x7f;
		// Note off and velocity 0 do nothing (TR-727 firmware; the units play one shots).
		if (status != 0x90 || velocity == 0)
			return;
		if (m_tr707 && channel == m_channel[kTR707])
			m_tr707->trigger(Tr707::noteToSound(false, note), velocity);
	}

	float m_volumeDb = 0.0f;
	int m_channel[kNumMachines];

	RomLibrary m_roms;
	std::vector<uint8_t> m_voices707;
	std::unique_ptr<Tr707> m_tr707;
	Resampler m_resampler707;
	int m_maxIn = 0;
	std::vector<float> m_in707L, m_in707R, m_out707L, m_out707R;

	mutable std::mutex m_stateLock;
	std::string m_uiSize, m_tab;

	DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OstrichPlugin)
};

Plugin *createPlugin()
{
	return new OstrichPlugin();
}

END_NAMESPACE_DISTRHO

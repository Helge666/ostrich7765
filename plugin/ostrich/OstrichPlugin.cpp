// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ostrich 7765 -- the DSP side: the outputs (the stereo mix and 16 mono outputs), MIDI per
// machine, the panel's size and tab in the host's session, the ROM setup.  Phase 2
// (ROADMAP.md): the TR-505, TR-626 and TR-707 play into the mix; the TR-727 (no ROMs yet)
// and the mono outputs follow.

#include "DistrhoPlugin.hpp"
#include "OstrichShared.hpp"
#include "Version.hpp"

#include "engine/PcmMachine.hpp"
#include "engine/Resampler.hpp"
#include "engine/Roms.hpp"
#include "engine/SoundOrder.hpp"
#include "engine/Tr707.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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
			m_channel[m] = kDefaultChannel[m];
		for (float &l : m_level)
			l = 1.0f;
		std::copy(std::begin(kPanDefault), std::end(kPanDefault), m_pan);
		for (uint32_t m = 0; m < kNumMachines; m ++)
		{
			m_timing[m] = true;
			m_flamKey[m] = kDefaultFlamKey;
			m_flamInterval[m] = kDefaultFlamInterval;
		}
		m_roms.scan();
		Engines e;
		buildEngines(m_roms, e);
		takeEngines(e);
		publishRomSet(0, 0, std::string());
		sampleRateChanged(getSampleRate());
	}

	~OstrichPlugin() override
	{
		if (m_romThread.joinable())
			m_romThread.join();
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
			p.ranges.def = float(kDefaultChannel[m]);
			p.ranges.min = 1.0f;
			p.ranges.max = 16.0f;
		}
		else if (index >= kParamMachineVolume && index < kParamMachineVolume + kMachineCount)
		{
			const uint32_t m = index - kParamMachineVolume;
			p.hints = kParameterIsAutomatable;
			p.name = String(kMachineNames[m]) + " volume";
			p.symbol = String(kMachineSymbols[m]) + "_volume";
			p.unit = "dB";
			p.ranges.def = 0.0f;
			p.ranges.min = -40.0f;
			p.ranges.max = 6.0f;
		}
		else if (index >= kParamLevel && index < kParamTiming)
		{
			uint32_t m = 0;
			while (index >= kParamLevel + kLevelOffset[m + 1])
				m ++;
			const uint32_t c = index - kParamLevel - kLevelOffset[m];
			// "BASS DRUM", "RIM SHOT / COWBELL": the column's title on one line.
			std::string title = kColumns[m].columns[c].title, symbol;
			std::replace(title.begin(), title.end(), '\n', ' ');
			for (char ch : title)
				symbol += std::isalnum(static_cast<unsigned char>(ch)) ? char(std::tolower(static_cast<unsigned char>(ch))) : '_';
			p.hints = kParameterIsAutomatable;
			p.name = String(kMachineNames[m]) + " " + String(title.c_str()) + " level";
			p.shortName = String(title.c_str());
			p.symbol = String(kMachineSymbols[m]) + "_" + String(symbol.c_str());
			p.ranges.def = 1.0f;
			p.ranges.min = 0.0f;
			p.ranges.max = 1.0f;
		}
		else if (index >= kParamTiming && index < kParamTiming + kMachineCount)
		{
			const uint32_t m = index - kParamTiming;
			p.hints = kParameterIsBoolean | kParameterIsInteger;
			p.name = String(kMachineNames[m]) + " vintage timing";
			p.shortName = "Timing";
			p.symbol = String(kMachineSymbols[m]) + "_timing";
			p.ranges.def = 1.0f;
			p.ranges.min = 0.0f;
			p.ranges.max = 1.0f;
		}
		else if (index >= kParamPitch && index < kParamPan)
		{
			const Pad &pad = pitchPad(index - kParamPitch);
			std::string symbol;
			for (char ch : std::string(pad.label))
				symbol += char(std::tolower(static_cast<unsigned char>(ch)));
			p.hints = kParameterIsAutomatable | kParameterIsInteger;
			p.name = String("TR-626 ") + pad.label + " pitch";
			p.shortName = String(pad.label) + " pitch";
			p.symbol = String("tr626_") + symbol.c_str() + "_pitch";
			p.ranges.def = 0.0f;
			p.ranges.min = -7.0f;
			p.ranges.max = 7.0f;
		}
		else if (index >= kParamPan && index < kParamFlamKey)
		{
			uint32_t m = 0;
			while (index >= kParamPan + kLevelOffset[m + 1])
				m ++;
			const uint32_t c = index - kParamPan - kLevelOffset[m];
			std::string title = kColumns[m].columns[c].title, symbol;
			std::replace(title.begin(), title.end(), '\n', ' ');
			for (char ch : title)
				symbol += std::isalnum(static_cast<unsigned char>(ch)) ? char(std::tolower(static_cast<unsigned char>(ch))) : '_';
			p.hints = kParameterIsAutomatable;
			p.name = String(kMachineNames[m]) + " " + String(title.c_str()) + " pan";
			p.shortName = String(title.c_str()) + " pan";
			p.symbol = String(kMachineSymbols[m]) + "_" + String(symbol.c_str()) + "_pan";
			p.ranges.def = kPanDefault[index - kParamPan];
			p.ranges.min = -1.0f;
			p.ranges.max = 1.0f;
		}
		else if (index >= kParamFlamKey && index < kParamFlamInterval)
		{
			const uint32_t m = index - kParamFlamKey;
			p.hints = kParameterIsInteger;
			p.name = String(kMachineNames[m]) + " flam key";
			p.shortName = "Flam key";
			p.symbol = String(kMachineSymbols[m]) + "_flam_key";
			p.ranges.def = float(kDefaultFlamKey);
			p.ranges.min = 0.0f;
			p.ranges.max = 127.0f;
		}
		else if (index >= kParamFlamInterval && index < kParamFlamOn)
		{
			const uint32_t m = index - kParamFlamInterval;
			p.hints = kParameterIsAutomatable | kParameterIsInteger;
			p.name = String(kMachineNames[m]) + " flam interval";
			p.shortName = "Flam";
			p.symbol = String(kMachineSymbols[m]) + "_flam_interval";
			p.ranges.def = float(kDefaultFlamInterval);
			p.ranges.min = 1.0f;
			p.ranges.max = 4.0f;
		}
		else if (index >= kParamFlamOn && index < kParamHighs)
		{
			const uint32_t m = index - kParamFlamOn;
			p.hints = kParameterIsOutput | kParameterIsBoolean | kParameterIsInteger;
			p.name = String(kMachineNames[m]) + " flam on";
			p.shortName = "Flam on";
			p.symbol = String(kMachineSymbols[m]) + "_flam_on";
			p.ranges.def = 0.0f;
			p.ranges.min = 0.0f;
			p.ranges.max = 1.0f;
		}
		else if (index == kParamHighs)
		{
			p.hints = kParameterIsAutomatable;
			p.name = "TR-707 highs";
			p.shortName = "Highs";
			p.symbol = "tr707_highs";
			p.ranges.def = 1.0f;
			p.ranges.min = 0.0f;
			p.ranges.max = 1.0f;
		}
	}

	// The TR-626 pad of pitch parameter i (kColumns626 order).
	static const Pad &pitchPad(uint32_t i)
	{
		for (uint32_t c = 0;; c ++)
		{
			if (i < kColumns626[c].numPads)
				return kColumns626[c].pads[i];
			i -= kColumns626[c].numPads;
		}
	}

	float getParameterValue(uint32_t index) const override
	{
		if (index == kParamVolume)
			return m_volumeDb;
		if (index >= kParamChannel && index < kParamChannel + kMachineCount)
			return float(m_channel[index - kParamChannel]);
		if (index >= kParamMachineVolume && index < kParamMachineVolume + kMachineCount)
			return m_machineVolumeDb[index - kParamMachineVolume];
		if (index >= kParamLevel && index < kParamTiming)
			return m_level[index - kParamLevel];
		if (index >= kParamTiming && index < kParamTiming + kMachineCount)
			return m_timing[index - kParamTiming] ? 1.0f : 0.0f;
		if (index >= kParamPitch && index < kParamPan)
			return float(m_pitch[index - kParamPitch]);
		if (index >= kParamPan && index < kParamFlamKey)
			return m_pan[index - kParamPan];
		if (index >= kParamFlamKey && index < kParamFlamInterval)
			return float(m_flamKey[index - kParamFlamKey]);
		if (index >= kParamFlamInterval && index < kParamFlamOn)
			return float(m_flamInterval[index - kParamFlamInterval]);
		if (index >= kParamFlamOn && index < kParamHighs)
			return m_flam[index - kParamFlamOn] ? 1.0f : 0.0f;
		if (index == kParamHighs)
			return m_highs;
		return 0.0f;
	}

	void setParameterValue(uint32_t index, float value) override
	{
		if (index == kParamVolume)
			m_volumeDb = value;
		else if (index >= kParamChannel && index < kParamChannel + kMachineCount)
			m_channel[index - kParamChannel] = std::clamp(int(std::lround(value)), 1, 16);
		else if (index >= kParamMachineVolume && index < kParamMachineVolume + kMachineCount)
			m_machineVolumeDb[index - kParamMachineVolume] = value;
		else if (index >= kParamLevel && index < kParamTiming)
			m_level[index - kParamLevel] = std::clamp(value, 0.0f, 1.0f);
		else if (index >= kParamTiming && index < kParamTiming + kMachineCount)
			m_timing[index - kParamTiming] = value > 0.5f;
		else if (index >= kParamPitch && index < kParamPan)
			m_pitch[index - kParamPitch] = std::clamp(int(std::lround(value)), -7, 7);
		else if (index >= kParamPan && index < kParamFlamKey)
			m_pan[index - kParamPan] = std::clamp(value, -1.0f, 1.0f);
		else if (index >= kParamFlamKey && index < kParamFlamInterval)
			m_flamKey[index - kParamFlamKey] = std::clamp(int(std::lround(value)), 0, 127);
		else if (index >= kParamFlamInterval && index < kParamFlamOn)
			m_flamInterval[index - kParamFlamInterval] = std::clamp(int(std::lround(value)), 1, 4);
		else if (index == kParamHighs)
			m_highs = std::clamp(value, 0.0f, 1.0f);
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
		case kStateRomSet:
			state.key = "romset";
			state.label = "ROM images";
			state.hints = kStateIsOnlyForUI;
			state.defaultValue = "";
			return;
		case kStateRomFile:
			// "<seq> <machine> <path>": see requestRomFile.
			state.key = "romfile";
			state.label = "ROM image file";
			state.hints = kStateIsOnlyForDSP;
			state.defaultValue = "";
			return;
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
		if (std::strcmp(key, "romset") == 0)
			return String(m_romSet.c_str());
		return String();
	}

	void setState(const char *key, const char *value) override
	{
		if (std::strcmp(key, "romfile") == 0)
			return requestRomFile(value);
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
		// the resampler's window (up to 2 * kHalf more). All engines run at 100 kHz.
		static_assert(Tr707::kRate == PcmMachine::kRate, "one internal rate");
		m_maxIn = int(std::ceil(kMaxSegment * Tr707::kRate / rate)) + 2 * Resampler::kHalf + 8;
		for (Slot &s : m_slots)
		{
			s.resampler.init(Tr707::kRate, rate, 2, m_maxIn);
			s.inL.assign(size_t(m_maxIn), 0.0f);
			s.inR.assign(size_t(m_maxIn), 0.0f);
			s.outL.assign(kMaxSegment, 0.0f);
			s.outR.assign(kMaxSegment, 0.0f);
		}
	}

	void activate() override
	{
		std::lock_guard<std::mutex> lock(m_engineLock);
		for (Slot &s : m_slots)
			s.resampler.reset();
		if (m_tr707)
			m_tr707->reset();
		for (auto &p : m_pcm)
			if (p)
				p->reset();
		m_numPending = 0;
		for (uint32_t m = 0; m < kNumMachines; m ++)
		{
			m_lineFree[m] = m_lastFire[m] = 0.0;
			m_flam[m] = false;
		}
		m_clock = 0;
	}

	void run(const float **, float **outputs, uint32_t frames, const MidiEvent *events,
	         uint32_t eventCount) override
	{
		for (uint32_t c = 0; c < DISTRHO_PLUGIN_NUM_OUTPUTS; c ++)
			std::memset(outputs[c], 0, sizeof(float) * frames);

		if (m_romSetPending.load())
		{
			// Set by the ROM thread: never wait for it here.
			std::unique_lock<std::mutex> sl(m_stateLock, std::try_to_lock);
			if (sl.owns_lock())
			{
				m_romSetPending.store(false);
				updateStateValue("romset", m_romSet.c_str());
			}
		}
		// Held for a moment only, while the ROM thread hands over new engines: this block is
		// silent then.
		std::unique_lock<std::mutex> engines(m_engineLock, std::try_to_lock);
		if (!engines.owns_lock())
			return;
		applyLevels();

		const float gain = std::pow(10.0f, m_volumeDb / 20.0f) * kTrim;
		// A start of the transport finds flam off, should a key's note off have got lost.
		const bool rolling = getTimePosition().playing;
		if (rolling && !m_rolling)
			for (bool &f : m_flam)
				f = false;
		m_rolling = rolling;
		// The block's notes into the queue, each with the frame it starts at: its own, or
		// with vintage timing as late as the unit would play it. The flam keys of a frame
		// count before its notes.
		for (uint32_t ev = 0; ev < eventCount;)
		{
			uint32_t end = ev;
			while (end < eventCount && events[end].frame == events[ev].frame)
				end ++;
			for (uint32_t i = ev; i < end; i ++)
				flamKey(events[i]);
			for (; ev < end; ev ++)
				midi(events[ev]);
		}
		uint32_t pos = 0;
		while (pos < frames)
		{
			// Notes start at the segment that begins at their frame.
			const uint64_t now = m_clock + pos;
			uint64_t next = m_clock + std::min(frames, pos + kMaxSegment);
			uint32_t kept = 0;
			for (uint32_t i = 0; i < m_numPending; i ++)
			{
				const Pending p = m_pending[i];
				if (p.at <= now)
					play(p.machine, p.note, p.velocity);
				else
				{
					next = std::min(next, p.at);
					m_pending[kept ++] = p;
				}
			}
			m_numPending = kept;
			const uint32_t end = uint32_t(next - m_clock);
			const int n = int(end - pos);
			for (uint32_t m = 0; m < kNumMachines; m ++)
			{
				if (!playing(m))
					continue;
				Slot &s = m_slots[m];
				const int nIn = s.resampler.inputFor(n);
				if (nIn > m_maxIn)   // cannot happen (sampleRateChanged); never read past the buffers
					continue;
				if (m == kTR707)
					m_tr707->render(nIn, s.inL.data(), s.inR.data(), nullptr);
				else
					m_pcm[m]->render(nIn, s.inL.data(), s.inR.data());
				const float *in[2] = { s.inL.data(), s.inR.data() };
				float *out[2] = { s.outL.data(), s.outR.data() };
				s.resampler.process(in, nIn, out, n);
				const float g = gain * std::pow(10.0f, m_machineVolumeDb[m] / 20.0f);
				for (int i = 0; i < n; i ++)
				{
					outputs[0][pos + i] += s.outL[i] * g;
					outputs[1][pos + i] += s.outR[i] * g;
				}
			}
			pos = end;
		}
		m_clock += frames;
	}

private:
	// The longest piece run() renders at once, in host frames.
	static constexpr uint32_t kMaxSegment = 128;
	// Every machine's level in the mix, -6 dB: the TR-707 (MAME's output stage) peaks at
	// about +1.3 dBFS for a bass drum at velocity 127 (with HIGHS at 100 % the hats at about
	// +7 dBFS), the TR-505's and TR-626's full-scale samples at up to +2.5 dBFS after the
	// resampler. Leaves room for several sounds at once.
	static constexpr float kTrim = 0.5f;

	struct Slot
	{
		Resampler resampler;
		std::vector<float> inL, inR, outL, outR;
	};

	bool playing(uint32_t m) const
	{
		return m == kTR707 ? m_tr707 != nullptr : m < 2 && m_pcm[m] != nullptr;
	}

	// The columns' levels into the engines: the TR-707's go to its level sliders (one per
	// mixer channel, the columns in channel order), the TR-505's and TR-626's to each sound
	// of the column.
	void applyLevels()
	{
		if (m_pcm[kTR626])
			for (uint32_t i = 0; i < kParamPan - kParamPitch; i ++)
			{
				const int sound = PcmMachine::noteToSound(PcmMachine::kTr626, pitchPad(i).note);
				if (sound >= 0)
					m_pcm[kTR626]->setPitch(sound, PcmMachine::tr626PitchFactor(sound, m_pitch[i]));
			}
		if (m_tr707)
		{
			m_tr707->setHighs(m_highs);
			for (uint32_t c = 0; c < kColumns[kTR707].count; c ++)
			{
				const uint32_t i = kLevelOffset[kTR707] + c;
				m_tr707->setLevel(int(c), m_level[i]);
				m_tr707->setPan(int(c), double(m_pan[i]) - double(kPanDefault[i]));
			}
		}
		for (uint32_t m : { uint32_t(kTR505), uint32_t(kTR626) })
		{
			if (!m_pcm[m])
				continue;
			for (uint32_t c = 0; c < kColumns[m].count; c ++)
			{
				const Column &col = kColumns[m].columns[c];
				for (uint32_t k = 0; k < col.numPads; k ++)
				{
					const int sound = PcmMachine::noteToSound(m_pcm[m]->model(), col.pads[k].note);
					if (sound < 0)
						continue;
					const uint32_t i = kLevelOffset[m] + c;
					m_pcm[m]->setLevel(sound, m_level[i]);
					m_pcm[m]->setPan(sound, double(m_pan[i]) - double(kPanDefault[i]));
				}
			}
		}
	}

	// ---- ROMs ----------------------------------------------------------------------------

	// What a set of ROM images makes: the engines of the machines it is complete for.
	struct Engines
	{
		std::vector<uint8_t> voices707;
		std::unique_ptr<Tr707> tr707;
		std::vector<uint8_t> soundOrder[2];          // TR-505, TR-626
		std::unique_ptr<PcmMachine> pcm[2];
	};

	static void buildEngines(const RomLibrary &roms, Engines &e)
	{
		if (roms.complete(kTR707))
		{
			// IC34, then IC35 (both 32 KiB, checked by their SHA-1).
			const auto &ic34 = roms.image(kRom707IC34), &ic35 = roms.image(kRom707IC35);
			e.voices707.resize(ic34.size() + ic35.size());
			std::memcpy(e.voices707.data(), ic34.data(), ic34.size());
			std::memcpy(e.voices707.data() + ic34.size(), ic35.data(), ic35.size());
			e.tr707 = std::make_unique<Tr707>(false);
			e.tr707->setRoms({ e.voices707.data(), roms.image(kRom707IC19).data(),
			                   roms.image(kRom707IC22).data() });
		}
		// TR-505: any of the three known forms of its dump.
		if (roms.has(kRom505Decoded))
			e.soundOrder[kTR505] = roms.image(kRom505Decoded);
		else if (roms.has(kRom505PinOrder))
			e.soundOrder[kTR505] = tr505SoundOrder(roms.image(kRom505PinOrder), false);
		else if (roms.has(kRom505Raw))
			e.soundOrder[kTR505] = tr505SoundOrder(roms.image(kRom505Raw), true);
		if (roms.has(kRom626))
			e.soundOrder[kTR626] = tr626SoundOrder(roms.image(kRom626));
		for (uint32_t m : { uint32_t(kTR505), uint32_t(kTR626) })
		{
			if (e.soundOrder[m].empty())
				continue;
			e.pcm[m] = std::make_unique<PcmMachine>(m == kTR505 ? PcmMachine::kTr505 : PcmMachine::kTr626);
			e.pcm[m]->setRom(e.soundOrder[m].data());
		}
	}

	// Puts in the engines of the machines that had none; a machine that plays keeps its
	// own. What is left in `e` (the engines not taken) goes with it.
	void takeEngines(Engines &e)
	{
		std::lock_guard<std::mutex> lock(m_engineLock);
		if (!m_tr707 && e.tr707)
		{
			m_voices707.swap(e.voices707);
			m_tr707.swap(e.tr707);
		}
		for (uint32_t m : { uint32_t(kTR505), uint32_t(kTR626) })
			if (!m_pcm[m] && e.pcm[m])
			{
				// A swapped vector keeps its buffer: the engine's pointer into it stays good.
				m_soundOrder[m].swap(e.soundOrder[m]);
				m_pcm[m].swap(e.pcm[m]);
			}
	}

	// The panel's ROM list (OstrichShared.hpp, "romset"), with the answer to request `seq`.
	void publishRomSet(uint32_t machine, unsigned seq, const std::string &message)
	{
		const std::string sep(1, kRomSetFieldSep);
		std::string t;
		for (int i = 0; i < kNumRomImages; i ++)
			t += "img" + sep + std::to_string(i) + sep
			     + (m_roms.has(RomImage(i)) ? m_roms.shownName(RomImage(i)) : std::string()) + "\n";
		t += "dir" + sep + RomLibrary::installFolder() + "\n";
		if (seq)
			t += "msg" + sep + std::to_string(machine) + sep + std::to_string(seq) + sep + message + "\n";
		std::lock_guard<std::mutex> lock(m_stateLock);
		m_romSet = t;
		m_romSetPending.store(true);
	}

	// From the panel: "<seq> <machine> <path>", a file the user picked. It and its folder
	// are searched on a thread of their own (SHA-1 over up to a few MB, file copies).
	void requestRomFile(const char *value)
	{
		unsigned seq = 0, machine = 0;
		int used = 0;
		if (std::sscanf(value, "%u %u %n", &seq, &machine, &used) < 2 || used == 0
		    || value[used] == '\0' || machine >= kNumMachines)
			return;
		if (m_romBusy.exchange(true))
			return;
		if (m_romThread.joinable())
			m_romThread.join();
		const std::string picked = value + used;
		m_romThread = std::thread([this, seq, machine, picked] {
			installRoms(seq, machine, picked);
			m_romBusy.store(false);
		});
	}

	// Copies what the picked file and its folder hold and the ROM folders lack into the
	// install folder, looks again, and starts the machines that are now complete.
	void installRoms(unsigned seq, uint32_t machine, const std::string &picked)
	{
		namespace fs = std::filesystem;
		const fs::path file(std::u8string(picked.begin(), picked.end()));
		RomLibrary found;
		std::string why, error;
		const bool known = found.scanFile(file, &why);
		found.scanFolder(file.parent_path());
		const int copied = m_roms.install(found, error);

		RomLibrary now;
		now.scan();
		Engines e;
		buildEngines(now, e);
		takeEngines(e);
		m_roms = std::move(now);

		std::string msg;
		if (!known)
			msg = why + ". ";
		if (copied)
			msg += "Copied " + std::to_string(copied) + (copied == 1 ? " image" : " images")
			       + " to the ROM folder. ";
		if (!error.empty())
			msg += "Could not copy all of them: " + error + ". ";
		if (machine == kTR727)
			msg += "No dumps of the TR-727's sound ROMs are known yet.";
		else if (m_roms.complete(machine))
			msg += std::string(kMachineNames[machine]) + (copied ? " plays." : " has all it needs.");
		else if (machine == kTR707)
		{
			std::string missing;
			for (int i = kRom707IC34; i <= kRom707IC22; i ++)
				if (!m_roms.has(RomImage(i)))
					missing += std::string(missing.empty() ? "" : ", ") + kRom707Chips[i - kRom707IC34];
			msg += "Still missing: " + missing + ".";
		}
		else
			msg += "Its sound ROM is still missing.";
		publishRomSet(machine, seq, msg);
	}

	// A machine's flam is on while its flam key is held.
	void flamKey(const MidiEvent &e)
	{
		const uint8_t status = e.data[0] & 0xf0;
		if (e.size < 3 || e.size > MidiEvent::kDataSize || (status != 0x80 && status != 0x90))
			return;
		const uint8_t channel = (e.data[0] & 0x0f) + 1;
		for (uint32_t m = 0; m < kNumMachines; m ++)
			if (channel == m_channel[m] && e.data[1] == m_flamKey[m])
				m_flam[m] = status == 0x90 && e.data[2] != 0;
	}

	// A MIDI message of this block: a note on goes into the queue for each machine on its
	// channel, at its frame or, with vintage timing, when the unit would play it. With flam
	// on, a softer hit goes first: the note keeps its velocity and comes the interval late.
	void midi(const MidiEvent &e)
	{
		if (e.size < 2 || e.size > MidiEvent::kDataSize || e.data[0] < 0x80 || e.data[0] >= 0xf0)
			return;
		const uint8_t status = e.data[0] & 0xf0, channel = (e.data[0] & 0x0f) + 1;
		const uint8_t note = e.data[1] & 0x7f, velocity = e.size >= 3 ? e.data[2] & 0x7f : 0;
		// Note off and velocity 0 play nothing (TR-727 firmware; on the TR-505 a note off has
		// no audible effect, measured): the units play one shots. With vintage timing they
		// still take their time on the MIDI line.
		const bool on = status == 0x90 && velocity != 0;
		for (uint32_t m = 0; m < kNumMachines; m ++)
		{
			if (!playing(m) || channel != m_channel[m])
				continue;
			double at = double(m_clock + e.frame);
			if (m_timing[m])
				at = vintage(m, at, on, e.size);
			if (!on || note == m_flamKey[m])
				continue;
			if (m_flam[m])
			{
				push(at, m, note, uint8_t(std::max(1, int(velocity) - kFlamSoftBy)));
				at += getSampleRate() * double(kFlamMs[m_flamInterval[m] - 1]) / 1000.0;
			}
			push(at, m, note, velocity);
		}
	}

	void push(double at, uint32_t m, uint8_t note, uint8_t velocity)
	{
		if (m_numPending < kMaxPending)
			m_pending[m_numPending ++] = { uint64_t(std::ceil(at)), uint8_t(m), note, velocity };
	}

	void play(uint32_t m, uint8_t note, uint8_t velocity)
	{
		if (m == kTR707 && m_tr707)
			m_tr707->trigger(Tr707::noteToSound(false, note), velocity);
		else if (m < 2 && m_pcm[m])
			m_pcm[m]->trigger(PcmMachine::noteToSound(m_pcm[m]->model(), note), velocity);
	}

	// Vintage timing (analysis/TIMING.md): the frame at which the unit plays a message sent
	// at frame t.  First the MIDI line, 31250 baud, ten bits a byte: the message is there
	// once its last byte is, and messages sent at once follow each other (running status:
	// one byte less each after the first).  Then the unit: the TR-707 / TR-727 parse MIDI
	// every 2.8 ms and fire the voices 0.7 ms later (TR-727 firmware); the TR-505 and
	// TR-626 have no grid, but a spread measured on the owner's units.
	double vintage(uint32_t m, double t, bool noteOn, uint32_t size)
	{
		const double rate = getSampleRate();
		const double byte = rate * 10.0 / 31250.0;
		const bool idle = t >= m_lineFree[m];
		const double avail = std::max(t, m_lineFree[m]) + double(idle ? size : size - 1) * byte;
		m_lineFree[m] = avail;
		if (!noteOn)
			return avail;
		double fire;
		if (m == kTR707 || m == kTR727)
		{
			const double period = rate * 0.0028, phase = m_gridPhase * period;
			fire = phase + std::ceil((avail - phase) / period) * period + rate * 0.0007;
		}
		else
		{
			// Measured: 300 bass drum notes spread over 1.7 ms (TR-505, standard deviation
			// 0.36 ms) and 1.55 ms (TR-626, 0.27 ms), the PC's USB MIDI included.
			const double sd = rate * (m == kTR505 ? 0.36e-3 : 0.27e-3);
			fire = avail + sd * (2.4 + std::clamp(gauss(), -2.4, 2.4));
		}
		fire = std::max(fire, m_lastFire[m]);       // never out of order
		m_lastFire[m] = fire;
		return fire;
	}

	// About normal, mean 0, deviation 1: the sum of four uniform numbers.
	double gauss()
	{
		double sum = 0.0;
		for (int i = 0; i < 4; i ++)
		{
			m_rng ^= m_rng << 13;
			m_rng ^= m_rng >> 17;
			m_rng ^= m_rng << 5;
			sum += double(m_rng) / 4294967296.0;
		}
		return (sum - 2.0) / std::sqrt(4.0 / 12.0);
	}

	float m_volumeDb = 0.0f;
	int m_channel[kNumMachines];
	float m_machineVolumeDb[kNumMachines] = {};
	float m_level[kLevelOffset[kNumMachines]];
	bool m_timing[kNumMachines] = {};
	int m_pitch[kParamPan - kParamPitch] = {};
	float m_pan[kLevelOffset[kNumMachines]];
	int m_flamKey[kNumMachines], m_flamInterval[kNumMachines];
	bool m_flam[kNumMachines] = {};
	bool m_rolling = false;
	float m_highs = 1.0f;

	// Notes waiting for their frame (vintage timing, or later in the block).
	struct Pending { uint64_t at; uint8_t machine, note, velocity; };
	static constexpr uint32_t kMaxPending = 512;
	Pending m_pending[kMaxPending];
	uint32_t m_numPending = 0;
	uint64_t m_clock = 0;                          // host frames since activate()
	double m_lineFree[kNumMachines] = {};          // vintage timing: the MIDI line free again
	double m_lastFire[kNumMachines] = {};
	uint32_t m_rng = 0x9e3779b9u;
	// The TR-707's 2.8 ms grid runs free: where it stands, a fraction of its period, is up to
	// when the unit was switched on.
	const double m_gridPhase = double(std::hash<const void *>{}(this) % 1000) / 1000.0;

	static constexpr const char *kRom707Chips[4] = { "IC34", "IC35", "IC19", "IC22" };

	RomLibrary m_roms;                             // the ROM thread's, once it runs
	std::mutex m_engineLock;                       // the engines below, against the ROM thread
	std::vector<uint8_t> m_voices707;
	std::unique_ptr<Tr707> m_tr707;
	std::vector<uint8_t> m_soundOrder[2];          // TR-505, TR-626
	std::unique_ptr<PcmMachine> m_pcm[2];
	Slot m_slots[kNumMachines];
	int m_maxIn = 0;

	mutable std::mutex m_stateLock;
	std::string m_uiSize, m_tab, m_romSet;
	std::atomic<bool> m_romSetPending { false }, m_romBusy { false };
	std::thread m_romThread;

	DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OstrichPlugin)
};

Plugin *createPlugin()
{
	return new OstrichPlugin();
}

END_NAMESPACE_DISTRHO

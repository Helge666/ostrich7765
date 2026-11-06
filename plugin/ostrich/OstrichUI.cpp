// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ostrich 7765 -- the panel.  A header, a tab per machine and one for the settings, the
// machine's page, and a status line.  A machine's page has a column per group of sounds
// (its LEVEL, pads with their notes that play the sound when clicked) and the machine's
// VOLUME; while its ROM images are missing, or on request, the page shows its ROM setup.
// Two looks: VINTAGE (the units' beige, the default) and DARK; the choice is kept for all
// instances in the panel's settings file.

#include "DistrhoUI.hpp"
#include "OstrichShared.hpp"
#include "Version.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {

using namespace ostrich;
using Clock = std::chrono::steady_clock;

// Design units: the window's pixels at 100 %.
constexpr float kW = DISTRHO_UI_DEFAULT_WIDTH, kH = DISTRHO_UI_DEFAULT_HEIGHT;

struct Rect { float x, y, w, h; bool contains(float px, float py) const
	{ return px >= x && px < x + w && py >= y && py < y + h; } };

constexpr float kMargin = 16.0f;
constexpr float kTabY = 72.0f, kTabH = 30.0f, kTabW = 132.0f, kTabGap = 6.0f;
constexpr float kBodyY = kTabY + kTabH;
constexpr float kFooterH = 30.0f;
const Rect kBody { kMargin, kBodyY, kW - 2 * kMargin, kH - kBodyY - kFooterH - 6.0f };

inline Rect tabRect(uint32_t t)
{
	return { kMargin + float(t) * (kTabW + kTabGap), kTabY, kTabW, kTabH };
}

const char *const kTabLabels[kNumTabs] = { "TR-505", "TR-626", "TR-707", "TR-727", "SETTINGS" };
const char *const kMachineTitles[kNumMachines] = {
	"TR-505  RHYTHM COMPOSER", "TR-626  RHYTHM COMPOSER", "TR-707  RHYTHM COMPOSER",
	"TR-727  RHYTHM COMPOSER",
};

// ---- the looks ---------------------------------------------------------------------------

struct Theme
{
	Color back, panel, column, edge, ink, inkDim, accent;
	Color tabOff, tabOffEdge, button, field;
	Color knob, knobTrack, knobMark;
	Color ok, warn, bad;
	Color tab[kNumTabs];                    ///< a colour per tab: the stripes, the titles
	Color zone[4];                          ///< the columns' colours, left to right
	Color padInk[4];                        ///< the text on a pad of each zone
};

const Theme kDark {
	Color(28, 28, 30), Color(36, 36, 40), Color(42, 42, 47), Color(58, 58, 64),
	Color(235, 235, 235), Color(150, 150, 156), Color(232, 128, 40),
	Color(30, 30, 33), Color(44, 44, 48), Color(48, 48, 54), Color(28, 28, 30),
	Color(24, 24, 26), Color(70, 70, 76), Color(235, 235, 235),
	Color(140, 210, 140), Color(230, 170, 120), Color(150, 60, 52),
	{ Color(232, 140, 40), Color(80, 160, 225), Color(215, 58, 48), Color(232, 196, 60), Color(170, 170, 176) },
	{ Color(205, 52, 44), Color(226, 118, 34), Color(230, 186, 52), Color(222, 216, 198) },
	{ Color(255, 255, 255), Color(255, 255, 255), Color(40, 36, 30), Color(40, 36, 30) },
};

// The beige of the units' cases, their dark grey lettering, the colours of their keys.
const Theme kVintage {
	Color(196, 189, 172), Color(214, 208, 193), Color(226, 221, 207), Color(160, 152, 135),
	Color(38, 36, 33), Color(100, 95, 86), Color(196, 92, 28),
	Color(184, 177, 160), Color(165, 158, 142), Color(202, 196, 180), Color(238, 234, 223),
	Color(48, 47, 46), Color(176, 169, 153), Color(240, 238, 232),
	Color(46, 128, 56), Color(168, 88, 28), Color(176, 62, 50),
	{ Color(204, 104, 24), Color(40, 112, 180), Color(190, 44, 36), Color(196, 150, 20), Color(110, 106, 98) },
	{ Color(200, 50, 42), Color(222, 114, 32), Color(232, 186, 48), Color(246, 244, 238) },
	{ Color(255, 255, 255), Color(255, 255, 255), Color(40, 36, 30), Color(40, 36, 30) },
};

enum Look : int { kLookVintage = 0, kLookDark = 1 };

// The panel's own settings, for all instances: the look.
std::filesystem::path settingsFile()
{
#ifdef _WIN32
	const char *pd = std::getenv("ProgramData");
	return std::filesystem::path(std::string(pd && *pd ? pd : "C:\\ProgramData")) / "Ostrich 7765" / "panel.txt";
#else
	if (const char *x = std::getenv("XDG_CONFIG_HOME"); x && *x)
		return std::filesystem::path(x) / "Ostrich 7765" / "panel.txt";
	const char *h = std::getenv("HOME");
	return std::filesystem::path(h && *h ? h : ".") / ".config" / "Ostrich 7765" / "panel.txt";
#endif
}

int loadLook()
{
	FILE *f = std::fopen(settingsFile().string().c_str(), "rb");
	if (!f)
		return kLookVintage;
	char line[64] = {};
	int look = kLookVintage;
	while (std::fgets(line, sizeof(line), f))
		if (std::strncmp(line, "look=dark", 9) == 0)
			look = kLookDark;
	std::fclose(f);
	return look;
}

void saveLook(int look)
{
	std::error_code ec;
	std::filesystem::create_directories(settingsFile().parent_path(), ec);
	if (FILE *f = std::fopen(settingsFile().string().c_str(), "wb"))
	{
		std::fputs(look == kLookDark ? "look=dark\n" : "look=vintage\n", f);
		std::fclose(f);
	}
}

// ---- a machine's page: the columns ---------------------------------------------------------

constexpr float kColGap = 6, kColTop = kBodyY + 36, kColBottom = kBodyY + 410;
constexpr float kKnobR = 22, kKnobDy = 72;
constexpr float kPanR = 14, kPanDy = 148;         // PAN (TR-707: HIGHS in the VOLUME column)
constexpr float kPitchR = 14, kPitchDy = 210;     // the TR-626's PITCH
constexpr float kPadW = 34, kPadH = 40, kPadGap = 5;

// Column c of a machine's n columns; c == n is its VOLUME.
inline Rect columnRect(uint32_t n, uint32_t c)
{
	const float w = (kBody.w - 16 - float(n) * kColGap) / float(n + 1);
	return { kBody.x + 8 + float(c) * (w + kColGap), kColTop, w, kColBottom - kColTop };
}

inline float knobX(const Rect &r) { return r.x + r.w / 2; }
inline float knobY(const Rect &r) { return r.y + kKnobDy; }

// Pad k of a column of `count` pads: as many in a row as fit, the rows at the bottom; a
// short row goes on top (three pads: one above, two below).
inline Rect padRect(const Rect &col, uint32_t count, uint32_t k)
{
	const uint32_t perRow = std::max(1u, std::min(count, uint32_t((col.w - 8 + kPadGap) / (kPadW + kPadGap))));
	const uint32_t rows = (count + perRow - 1) / perRow;
	const uint32_t first = count - (rows - 1) * perRow;     // pads in the top row
	const uint32_t row = k < first ? 0 : 1 + (k - first) / perRow;
	const uint32_t inRow = row == 0 ? first : perRow;
	const uint32_t pos = row == 0 ? k : (k - first) % perRow;
	const float rowW = float(inRow) * kPadW + float(inRow - 1) * kPadGap;
	const float x = col.x + (col.w - rowW) / 2 + float(pos) * (kPadW + kPadGap);
	const float y = col.y + col.h - 14 - float(rows - row) * (kPadH + 16) + 16;
	return { x, y, kPadW, kPadH };
}

// The colour zone of column c of n (as the reference panel: red, orange, yellow, cream).
inline uint32_t zoneOf(uint32_t n, uint32_t c) { return std::min(3u, c * 4 / n); }

const Rect kRomToggle { kBody.x + kBody.w - 158, kBody.y + 8, 150, 22 };

// ---- a machine's page: the ROM setup -------------------------------------------------------

// The rows of a machine's ROM list: what it needs, and the images (RomImage, Roms.hpp)
// any one of which fills the row.  -1 ends a list.
struct RomRow { const char *chip, *what; int images[4]; };
const RomRow kRomRows505[] = {
	{ "SOUND ROM", "128 KiB: the raw, the pin-order or the decoded dump", { 4, 5, 6, -1 } },
};
const RomRow kRomRows626[] = {
	{ "SOUND ROM", "256 KiB, the raw dump (put in order when it loads)", { 7, -1 } },
};
const RomRow kRomRows707[] = {
	{ "IC34", "voices: bass drum, snare, low and mid tom (32 KiB)", { 0, -1 } },
	{ "IC35", "voices: high tom, hi-hat, rim / cowbell, clap / tambourine (32 KiB)", { 1, -1 } },
	{ "IC19", "crash cymbal (32 KiB)", { 2, -1 } },
	{ "IC22", "ride cymbal (32 KiB)", { 3, -1 } },
};
struct RomRows { const RomRow *rows; uint32_t count; };
const RomRows kRomRows[kNumMachines] = {
	{ kRomRows505, 1 }, { kRomRows626, 1 }, { kRomRows707, 4 }, { nullptr, 0 },
};
constexpr int kNumRomImagesUi = 8;   // RomImage's count (Roms.hpp is DSP side)

constexpr float kRomX = kMargin + 40, kRomY = kBodyY + 92, kRomRowH = 30;
inline Rect romButton(uint32_t rows)
{
	return { kRomX, kRomY + float(rows) * kRomRowH + 22, 220, 32 };
}

// ---- the settings page ---------------------------------------------------------------------

// A row per machine with its MIDI channel, - and + either side; the look below.
const float kChanX = kBody.x + 40, kChanY = kBody.y + 92;
constexpr float kChanRowH = 44;
constexpr float kChanBtn = 28, kChanBoxW = 56;
inline Rect chanMinus(uint32_t m) { return { kChanX + 150, kChanY + m * kChanRowH, kChanBtn, kChanBtn }; }
inline Rect chanBox(uint32_t m) { return { kChanX + 150 + kChanBtn + 6, kChanY + m * kChanRowH, kChanBoxW, kChanBtn }; }
inline Rect chanPlus(uint32_t m) { return { kChanX + 150 + kChanBtn + 12 + kChanBoxW, kChanY + m * kChanRowH, kChanBtn, kChanBtn }; }
inline Rect timingButton(uint32_t m) { return { kChanX + 330, kChanY + m * kChanRowH, 150, kChanBtn }; }
// A stepper further right: - , the value, +.
constexpr float kFlamKeyX = 520, kFlamIntX = 680, kFlamLampX = 836;
inline Rect stepMinus(float x, uint32_t m) { return { kChanX + x, kChanY + m * kChanRowH, kChanBtn, kChanBtn }; }
inline Rect stepBox(float x, uint32_t m) { return { kChanX + x + kChanBtn + 6, kChanY + m * kChanRowH, kChanBoxW, kChanBtn }; }
inline Rect stepPlus(float x, uint32_t m) { return { kChanX + x + kChanBtn + 12 + kChanBoxW, kChanY + m * kChanRowH, kChanBtn, kChanBtn }; }
const float kLookY = kChanY + 4 * kChanRowH + 78;
inline Rect lookButton(int look) { return { kChanX + 150 + float(look) * 118, kLookY, 110, 28 }; }

} // anonymous namespace


class OstrichUI : public UI
{
public:
	OstrichUI()
		// DPF scales for the desktop itself and keeps the aspect ratio; everything below
		// draws in design units scaled to whatever size the window has.
		: UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT, true)
	{
		loadSharedResources();
		for (uint32_t m = 0; m < kNumMachines; m ++)
			m_channel[m] = kDefaultChannel[m];
		for (float &v : m_value)
			v = 1.0f;
		for (uint32_t m = 0; m < kNumMachines; m ++)
		{
			m_value[kParamMachineVolume + m] = m_value[kParamFlamOn + m] = 0.0f;
			m_value[kParamFlamKey + m] = float(kDefaultFlamKey);
			m_value[kParamFlamInterval + m] = float(kDefaultFlamInterval);
		}
		for (uint32_t p = kParamPitch; p < kParamPan; p ++)
			m_value[p] = 0.0f;
		for (uint32_t p = kParamPan; p < kParamFlamKey; p ++)
			m_value[p] = kPanDefault[p - kParamPan];
		m_value[kParamVolume] = 0.0f;
		m_romFile.resize(kNumRomImagesUi);
		m_look = loadLook();
	}

protected:
	void parameterChanged(uint32_t index, float value) override
	{
		if (index >= kParamCount)
			return;
		m_value[index] = value;
		if (index >= kParamChannel && index < kParamChannel + kMachineCount)
			m_channel[index - kParamChannel] = int(std::lround(value));
		repaint();
	}

	bool onScroll(const ScrollEvent &ev) override
	{
		const float s = mouseScale();
		const float x = float(ev.pos.getX()) / s, y = float(ev.pos.getY()) / s;
		if (ev.delta.getY() == 0)
			return false;
		const int dir = ev.delta.getY() > 0 ? 1 : -1;
		if (m_tab == kTabSettings)
		{
			for (uint32_t m = 0; m < kNumMachines; m ++)
			{
				if (chanBox(m).contains(x, y) || chanMinus(m).contains(x, y) || chanPlus(m).contains(x, y))
					return changeChannel(m, dir), true;
				for (const auto &[sx, p] : { std::pair(kFlamKeyX, kParamFlamKey + m), std::pair(kFlamIntX, kParamFlamInterval + m) })
					if (stepBox(sx, m).contains(x, y) || stepMinus(sx, m).contains(x, y) || stepPlus(sx, m).contains(x, y))
						return setValue(p, m_value[p] + float(dir), true), true;
			}
			return false;
		}
		const int p = knobAt(x, y);
		if (p < 0)
			return false;
		const bool fine = ev.mod & kModifierShift;
		const float f = fine ? (isVolume(uint32_t(p)) ? 0.5f : 0.25f) : 1.0f;
		setValue(uint32_t(p), m_value[p] + float(dir) * step(uint32_t(p)) * f, true);
		return true;
	}

	void stateChanged(const char *key, const char *value) override
	{
		if (std::strcmp(key, "uisize") == 0)
			restoreSize(value);
		else if (std::strcmp(key, "romset") == 0)
		{
			setRomSet(value);
			repaint();
		}
		else if (std::strcmp(key, "tab") == 0)
		{
			const int t = std::atoi(value);
			if (*value && t >= 0 && t < int(kNumTabs))
			{
				m_tab = uint32_t(t);
				repaint();
			}
		}
	}

	bool onMouse(const MouseEvent &ev) override
	{
		if (ev.button != 1)
			return false;
		const float s = mouseScale();
		const float x = float(ev.pos.getX()) / s, y = float(ev.pos.getY()) / s;
		if (!ev.press)
		{
			if (m_drag >= 0)
			{
				editParameter(uint32_t(m_drag), false);
				m_drag = -1;
				repaint();
				return true;
			}
			return false;
		}
		for (uint32_t t = 0; t < kNumTabs; t ++)
			if (tabRect(t).contains(x, y))
			{
				if (t != m_tab)
				{
					m_tab = t;
					char value[8];
					std::snprintf(value, sizeof(value), "%u", unsigned(t));
					setState("tab", value);
					repaint();
				}
				return true;
			}
		if (m_tab == kTabSettings)
		{
			for (uint32_t m = 0; m < kNumMachines; m ++)
			{
				if (chanMinus(m).contains(x, y))
					return changeChannel(m, -1), true;
				if (chanPlus(m).contains(x, y))
					return changeChannel(m, 1), true;
				if (m != kTR727 && timingButton(m).contains(x, y))
					return setValue(kParamTiming + m, m_value[kParamTiming + m] > 0.5f ? 0.0f : 1.0f, true), true;
				for (const auto &[sx, p] : { std::pair(kFlamKeyX, kParamFlamKey + m), std::pair(kFlamIntX, kParamFlamInterval + m) })
				{
					if (stepMinus(sx, m).contains(x, y))
						return setValue(p, m_value[p] - 1.0f, true), true;
					if (stepPlus(sx, m).contains(x, y))
						return setValue(p, m_value[p] + 1.0f, true), true;
				}
			}
			for (int look : { kLookVintage, kLookDark })
				if (lookButton(look).contains(x, y))
				{
					if (look != m_look)
					{
						m_look = look;
						saveLook(look);
						repaint();
					}
					return true;
				}
			return false;
		}
		const uint32_t m = m_tab;
		if (showingRoms(m))
		{
			if (romsComplete(m) && kRomToggle.contains(x, y))
				return m_showRoms[m] = false, repaint(), true;
			if (kRomRows[m].count && romButton(kRomRows[m].count).contains(x, y))
				return browseRom(m), true;
			return false;
		}
		if (kRomToggle.contains(x, y))
			return m_showRoms[m] = true, repaint(), true;
		// A knob: drag it; a second click soon after puts it back to its default.
		if (const int p = knobAt(x, y); p >= 0)
		{
			const auto now = Clock::now();
			if (p == m_lastKnob && now - m_lastKnobAt < std::chrono::milliseconds(350))
			{
				setValue(uint32_t(p), defaultOf(uint32_t(p)), true);
				m_lastKnob = -1;
				return true;
			}
			m_lastKnob = p;
			m_lastKnobAt = now;
			m_drag = p;
			m_dragY = y;
			m_dragFrom = m_value[p];
			editParameter(uint32_t(p), true);
			repaint();
			return true;
		}
		// A pad: plays its sound on the machine's channel.
		const Columns &cs = kColumns[m];
		for (uint32_t c = 0; c < cs.count; c ++)
		{
			const Column &col = cs.columns[c];
			const Rect r = columnRect(cs.count, c);
			for (uint32_t k = 0; k < col.numPads; k ++)
				if (padRect(r, col.numPads, k).contains(x, y))
				{
					sendNote(uint8_t(m_channel[m] - 1), col.pads[k].note, (ev.mod & kModifierShift) ? 64 : 127);
					m_flash[m][c * 3 + k] = Clock::now();
					m_selected[m][c] = uint8_t(k);      // the TR-626's PITCH sets this one
					repaint();
					return true;
				}
		}
		return false;
	}

	bool onMotion(const MotionEvent &ev) override
	{
		if (m_drag < 0)
			return false;
		const float s = mouseScale();
		const float y = float(ev.pos.getY()) / s;
		const uint32_t p = uint32_t(m_drag);
		// 200 units of travel for the whole range; Shift: five times finer.
		const float span = rangeMax(p) - rangeMin(p);
		const float per = (ev.mod & kModifierShift) ? 1000.0f : 200.0f;
		setValue(p, m_dragFrom + (m_dragY - y) / per * span, false);
		return true;
	}

	void uiFileBrowserSelected(const char *filename) override
	{
		const int m = m_browseMachine;
		m_browseMachine = -1;
		if (m < 0 || filename == nullptr || filename[0] == '\0')
			return;                         // cancelled
		m_romDir = dirOf(filename);
		m_romAsked = ++ m_romSeq;
		m_romAskedMachine = uint32_t(m);
		m_romAskedAt = Clock::now();
		const std::string msg = std::to_string(m_romSeq) + " " + std::to_string(m) + " " + filename;
		setState("romfile", msg.c_str());
		repaint();
	}

	void onNanoDisplay() override
	{
		// Nothing may escape a draw: under Windows it unwinds through the window
		// procedure, which swallows it and leaves pugl believing it is still drawing.
		try
		{
			drawPanel();
		}
		catch (...)
		{
		}
	}

	void uiIdle() override
	{
		const auto now = Clock::now();
		if (!m_frameFitted && now - m_created < std::chrono::seconds(2))
			fitHostFrame();
		saveSizeWhenSettled();
		// No answer from the DSP: say so rather than wait for ever.
		if (m_romAsked && now - m_romAskedAt > std::chrono::seconds(20))
		{
			m_romAsked = 0;
			m_romMsgMachine = m_romAskedMachine;
			m_romMsg = "No answer from the plugin. Is it running (an active track)?";
			repaint();
		}
		// A pad's lamp goes out again.
		bool lit = false;
		for (const auto &f : m_flash)
			for (const auto &t : f)
				lit |= now - t < kFlash + std::chrono::milliseconds(60);
		if (lit)
			repaint();
	}

private:
	static constexpr auto kFlash = std::chrono::milliseconds(140);

	const Theme &theme() const { return m_look == kLookDark ? kDark : kVintage; }

	// ---- values ----------------------------------------------------------------------------

	static bool isVolume(uint32_t p)
	{
		return p == kParamVolume || (p >= kParamMachineVolume && p < kParamMachineVolume + kMachineCount);
	}
	static bool isPitch(uint32_t p) { return p >= kParamPitch && p < kParamPan; }
	static bool isPan(uint32_t p) { return p >= kParamPan && p < kParamFlamKey; }
	static bool isFlamKey(uint32_t p) { return p >= kParamFlamKey && p < kParamFlamInterval; }
	static bool isFlamInterval(uint32_t p) { return p >= kParamFlamInterval && p < kParamFlamOn; }
	static bool isSwitch(uint32_t p) { return p >= kParamTiming && p < kParamTiming + kMachineCount; }
	static float rangeMin(uint32_t p)
	{
		return isVolume(p) ? -40.0f : isPitch(p) ? -7.0f : isPan(p) ? -1.0f : isFlamInterval(p) ? 1.0f : 0.0f;
	}
	static float rangeMax(uint32_t p)
	{
		return isVolume(p) ? 6.0f : isPitch(p) ? 7.0f : isFlamKey(p) ? 127.0f : isFlamInterval(p) ? 4.0f : 1.0f;
	}
	static float defaultOf(uint32_t p)
	{
		return isPan(p) ? kPanDefault[p - kParamPan] : isVolume(p) || isPitch(p) ? 0.0f : 1.0f;
	}
	static float step(uint32_t p) { return isVolume(p) || isPitch(p) ? 1.0f : 0.02f; }

	// The knob's position, 0..1.
	float position(uint32_t p) const
	{
		return (m_value[p] - rangeMin(p)) / (rangeMax(p) - rangeMin(p));
	}

	void setValue(uint32_t p, float v, bool gesture)
	{
		v = std::max(rangeMin(p), std::min(rangeMax(p), v));
		if (isVolume(p))
			v = std::round(v * 2.0f) / 2.0f;     // half dB steps
		else if (isPitch(p) || isSwitch(p) || isFlamKey(p) || isFlamInterval(p))
			v = std::round(v);
		if (v == m_value[p])
			return;
		m_value[p] = v;
		if (gesture)
			editParameter(p, true);
		setParameterValue(p, v);
		if (gesture)
			editParameter(p, false);
		repaint();
	}

	// The parameter of the knob under (x, y) on the current machine's page, or -1.
	int knobAt(float x, float y) const
	{
		if (m_tab >= kNumMachines || showingRoms(m_tab))
			return -1;
		const uint32_t m = m_tab, n = kColumns[m].count;
		for (uint32_t c = 0; c <= n; c ++)
		{
			const Rect r = columnRect(n, c);
			const float dx = x - knobX(r), dy = y - knobY(r);
			if (dx * dx + dy * dy <= (kKnobR + 8) * (kKnobR + 8))
				return int(c == n ? kParamMachineVolume + m : levelParam(m, c));
			const float ny = y - (r.y + kPanDy);
			if (dx * dx + ny * ny <= (kPanR + 8) * (kPanR + 8))
			{
				if (c < n)
					return int(panParam(m, c));
				if (m == kTR707)
					return int(kParamHighs);
			}
			const float py = y - (r.y + kPitchDy);
			if (m == kTR626 && c < n && dx * dx + py * py <= (kPitchR + 8) * (kPitchR + 8))
				return int(pitchParam(c, m_selected[m][c]));
		}
		return -1;
	}

	// ---- ROMs --------------------------------------------------------------------------------

	bool romsComplete(uint32_t m) const
	{
		const RomRows &rr = kRomRows[m];
		if (rr.count == 0)
			return false;
		for (uint32_t r = 0; r < rr.count; r ++)
		{
			bool found = false;
			for (int k = 0; k < 4 && rr.rows[r].images[k] >= 0; k ++)
				found |= !m_romFile[size_t(rr.rows[r].images[k])].empty();
			if (!found)
				return false;
		}
		return true;
	}

	bool showingRoms(uint32_t m) const { return m_showRoms[m] || !romsComplete(m); }

	// ---- drawing -------------------------------------------------------------------------------

	void drawPanel()
	{
		const Theme &t = theme();
		const float s = getWidth() / kW;
		scale(s, s);

		beginPath();
		rect(0, 0, kW, kH);
		fillColor(t.back);
		fill();

		// The stripe along the top, in the colours of the machines.
		const float sw = kW / float(kNumMachines);
		for (uint32_t m = 0; m < kNumMachines; m ++)
		{
			beginPath();
			rect(float(m) * sw, 0, sw, 4);
			fillColor(t.tab[m]);
			fill();
		}

		fontFace(NANOVG_DEJAVU_SANS_TTF);
		textAlign(ALIGN_LEFT | ALIGN_BASELINE);
		fillColor(t.ink);
		fontSize(28);
		text(kMargin, 46, "OSTRICH 7765", nullptr);
		fontSize(12);
		fillColor(t.accent);
		text(kMargin + 218, 46, "TR-505  \xc2\xb7  TR-626  \xc2\xb7  TR-707  \xc2\xb7  TR-727", nullptr);

		textAlign(ALIGN_RIGHT | ALIGN_BASELINE);
		fontSize(12);
		fillColor(t.inkDim);
		text(kW - kMargin, 46, "Ostrich 7765 " OSTRICH_VERSION_STRING "  \xe2\x80\x93  github.com/Helge666/ostrich7765", nullptr);

		drawTabs();
		drawBody();
		drawFooter();
	}

	void drawTabs()
	{
		const Theme &th = theme();
		for (uint32_t t = 0; t < kNumTabs; t ++)
		{
			const Rect r = tabRect(t);
			const bool on = t == m_tab;
			beginPath();
			roundedRect(r.x, r.y, r.w, r.h + (on ? 4 : -2), 4);
			fillColor(on ? th.panel : th.tabOff);
			fill();
			strokeColor(on ? th.edge : th.tabOffEdge);
			strokeWidth(1);
			stroke();
			beginPath();
			rect(r.x + 10, r.y + 4, r.w - 20, 2);
			fillColor(on ? th.tab[t] : Color(th.tab[t], th.tabOff, 0.65f));
			fill();
			fontSize(13);
			textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
			fillColor(on ? th.ink : th.inkDim);
			text(r.x + r.w / 2, r.y + r.h / 2 + 2, kTabLabels[t], nullptr);
		}
	}

	void drawBody()
	{
		const Theme &th = theme();
		beginPath();
		roundedRect(kBody.x, kBody.y, kBody.w, kBody.h, 4);
		fillColor(th.panel);
		fill();
		strokeColor(th.edge);
		strokeWidth(1);
		stroke();
		// The open tab merges into the page.
		const Rect t = tabRect(m_tab);
		beginPath();
		rect(t.x + 1, kBody.y - 1, t.w - 2, 3);
		fillColor(th.panel);
		fill();

		if (m_tab < kNumMachines)
		{
			if (showingRoms(m_tab))
				drawRoms(m_tab);
			else
				drawColumns(m_tab);
			return;
		}
		drawSettings();
	}

	void drawButton(const Rect &b, const char *label, bool lit, bool enabled = true)
	{
		const Theme &th = theme();
		beginPath();
		roundedRect(b.x, b.y, b.w, b.h, 4);
		fillColor(lit ? Color(th.button, th.accent, 0.25f) : th.button);
		fill();
		strokeColor(lit ? th.accent : th.edge);
		strokeWidth(1);
		stroke();
		textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
		fontSize(12);
		fillColor(enabled ? th.ink : th.inkDim);
		text(b.x + b.w / 2, b.y + b.h / 2 + 1, label, nullptr);
	}

	// A machine's page: its title and channel, a column per group of sounds, its volume.
	void drawColumns(uint32_t m)
	{
		const Theme &th = theme();
		textAlign(ALIGN_LEFT | ALIGN_BASELINE);
		fontSize(15);
		fillColor(th.tab[m]);
		text(kBody.x + 12, kBody.y + 25, kMachineTitles[m], nullptr);
		char ch[32];
		std::snprintf(ch, sizeof(ch), "MIDI CHANNEL %d", m_channel[m]);
		fontSize(11);
		fillColor(th.inkDim);
		text(kBody.x + 250, kBody.y + 24, ch, nullptr);
		drawButton(kRomToggle, "ROM IMAGES ...", false);

		const Columns &cs = kColumns[m];
		const auto now = Clock::now();
		for (uint32_t c = 0; c <= cs.count; c ++)
		{
			const Rect r = columnRect(cs.count, c);
			const bool volume = c == cs.count;
			const uint32_t z = volume ? 3 : zoneOf(cs.count, c);
			const Color colour = volume ? th.inkDim : th.zone[z];
			beginPath();
			roundedRect(r.x, r.y, r.w, r.h, 4);
			fillColor(th.column);
			fill();
			strokeColor(th.edge);
			strokeWidth(1);
			stroke();

			// The title, one or two lines, and the stripe under it.
			const char *title = volume ? "VOLUME" : cs.columns[c].title;
			const char *nl = std::strchr(title, '\n');
			textAlign(ALIGN_CENTER | ALIGN_BASELINE);
			fontSize(10.5f);
			fillColor(th.ink);
			if (nl)
			{
				text(r.x + r.w / 2, r.y + 18, title, nl);
				text(r.x + r.w / 2, r.y + 31, nl + 1, nullptr);
			}
			else
				text(r.x + r.w / 2, r.y + 25, title, nullptr);
			beginPath();
			rect(r.x + 8, r.y + 38, r.w - 16, 2);
			fillColor(volume ? th.edge : Color(colour, th.ink, 0.15f));
			fill();

			const uint32_t p = volume ? kParamMachineVolume + m : levelParam(m, c);
			drawKnob(knobX(r), knobY(r), position(p), volume ? th.inkDim : colour, p == uint32_t(m_drag));
			char label[24];
			if (p == uint32_t(m_drag))
				volume ? std::snprintf(label, sizeof(label), "%+.1f dB", double(m_value[p]))
				       : std::snprintf(label, sizeof(label), "%d %%", int(std::lround(m_value[p] * 100)));
			else
				std::snprintf(label, sizeof(label), volume ? "%+.1f dB" : "LEVEL", double(m_value[p]));
			textAlign(ALIGN_CENTER | ALIGN_BASELINE);
			fontSize(10.5f);
			fillColor(p == uint32_t(m_drag) ? th.ink : th.inkDim);
			text(knobX(r), knobY(r) + kKnobR + 18, label, nullptr);
			if (volume)
			{
				if (m == kTR707)
				{
					const float hy = r.y + kPanDy;
					const bool drag = kParamHighs == uint32_t(m_drag);
					drawKnob(knobX(r), hy, position(kParamHighs), th.inkDim, drag, kPanR);
					std::snprintf(label, sizeof(label), drag ? "%.0f %%" : "HIGHS", double(m_value[kParamHighs] * 100));
					fontSize(10.5f);
					fillColor(drag ? th.ink : th.inkDim);
					text(knobX(r), hy + kPanR + 18, label, nullptr);
				}
				continue;
			}

			// PAN.
			{
				const uint32_t pp = panParam(m, c);
				const float py = r.y + kPanDy;
				const bool drag = pp == uint32_t(m_drag);
				drawKnob(knobX(r), py, position(pp), colour, drag, kPanR, true);
				const int v = int(std::lround(m_value[pp] * 100));
				if (!drag)
					std::snprintf(label, sizeof(label), "PAN");
				else if (v == 0)
					std::snprintf(label, sizeof(label), "CENTRE");
				else
					std::snprintf(label, sizeof(label), "%c %d", v < 0 ? 'L' : 'R', std::abs(v));
				fontSize(10.5f);
				fillColor(drag ? th.ink : th.inkDim);
				text(knobX(r), py + kPanR + 18, label, nullptr);
			}

			// The TR-626's PITCH, of the column's sound clicked last.
			const Column &col = cs.columns[c];
			const uint32_t sel = m_selected[m][c];
			if (m == kTR626)
			{
				const uint32_t pp = pitchParam(c, sel);
				const float py = r.y + kPitchDy;
				drawKnob(knobX(r), py, position(pp), colour, pp == uint32_t(m_drag), kPitchR, true);
				const int v = int(std::lround(m_value[pp]));
				char pitch[32];
				std::snprintf(pitch, sizeof(pitch), v ? "%s PITCH %+d" : "%s PITCH 0", col.pads[sel].label, v);
				fontSize(10.5f);
				fillColor(pp == uint32_t(m_drag) ? th.ink : th.inkDim);
				text(knobX(r), py + kPitchR + 18, pitch, nullptr);
			}

			// The pads: label and note; the lamp above lights when clicked.
			for (uint32_t k = 0; k < col.numPads; k ++)
			{
				const Rect pr = padRect(r, col.numPads, k);
				const bool lit = now - m_flash[m][c * 3 + k] < kFlash;
				beginPath();
				circle(pr.x + pr.w / 2, pr.y - 8, 2.5f);
				fillColor(lit ? Color(255, 70, 50) : Color(th.edge, th.column, 0.3f));
				fill();
				beginPath();
				roundedRect(pr.x, pr.y, pr.w, pr.h, 3);
				fillColor(lit ? Color(th.zone[z], Color(255, 255, 255), 0.35f) : th.zone[z]);
				fill();
				const bool picked = m == kTR626 && k == sel;
				strokeColor(picked ? th.ink : Color(th.zone[z], Color(0, 0, 0), 0.25f));
				strokeWidth(picked ? 2 : 1);
				stroke();
				fillColor(th.padInk[z]);
				textAlign(ALIGN_CENTER | ALIGN_BASELINE);
				fontSize(11);
				text(pr.x + pr.w / 2, pr.y + 18, col.pads[k].label, nullptr);
				char note[8];
				std::snprintf(note, sizeof(note), "%u", unsigned(col.pads[k].note));
				fontSize(9);
				fillColor(Color(th.padInk[z], th.zone[z], 0.3f));
				text(pr.x + pr.w / 2, pr.y + 33, note, nullptr);
			}
		}
	}

	// A knob of 270 degrees: the track, the value's arc in the column's colour, the cap with
	// its mark.
	// A bipolar knob's arc starts at the top, at its centre value.
	void drawKnob(float cx, float cy, float pos, const Color &colour, bool active,
	              float radius = kKnobR, bool bipolar = false)
	{
		const Theme &th = theme();
		const float a0 = float(M_PI) * 0.75f, a1 = float(M_PI) * 2.25f, am = float(M_PI) * 1.5f;
		const float a = a0 + (a1 - a0) * std::max(0.0f, std::min(1.0f, pos));
		const float from = bipolar ? std::min(a, am) : a0, to = bipolar ? std::max(a, am) : a;
		beginPath();
		arc(cx, cy, radius + 5, a0, a1, CW);
		strokeColor(th.knobTrack);
		strokeWidth(3);
		stroke();
		if (to - from > 0.01f)
		{
			beginPath();
			arc(cx, cy, radius + 5, from, to, CW);
			strokeColor(colour);
			strokeWidth(3);
			stroke();
		}
		beginPath();
		circle(cx, cy, radius);
		fillPaint(radialGradient(cx - radius * 0.27f, cy - radius * 0.36f, 2, radius + 4,
		                         Color(th.knob, Color(255, 255, 255), 0.18f), th.knob));
		fill();
		strokeColor(active ? colour : Color(th.knob, Color(0, 0, 0), 0.4f));
		strokeWidth(active ? 2 : 1);
		stroke();
		beginPath();
		moveTo(cx + std::cos(a) * radius * 0.3f, cy + std::sin(a) * radius * 0.3f);
		lineTo(cx + std::cos(a) * radius * 0.88f, cy + std::sin(a) * radius * 0.88f);
		strokeColor(th.knobMark);
		strokeWidth(2.5f);
		lineCap(ROUND);
		stroke();
	}

	// A machine's page, ROM setup: its title, the ROM images it needs and where they came
	// from, a button to pick a file, and the answer to the last pick.
	void drawRoms(uint32_t m)
	{
		const Theme &th = theme();
		textAlign(ALIGN_LEFT | ALIGN_BASELINE);
		fontSize(22);
		fillColor(th.tab[m]);
		text(kRomX, kBody.y + 44, kMachineTitles[m], nullptr);
		fontSize(15);
		fillColor(th.ink);
		text(kRomX, kBody.y + 76, "ROM IMAGES", nullptr);
		if (romsComplete(m))
			drawButton(kRomToggle, "BACK TO THE SOUNDS", false);

		const RomRows &rr = kRomRows[m];
		if (rr.count == 0)
		{
			fontSize(13);
			fillColor(th.inkDim);
			text(kRomX, kRomY + 14, "No dumps of the TR-727's sound ROMs are known yet. The engine is prepared;", nullptr);
			text(kRomX, kRomY + 34, "it plays once someone dumps them.", nullptr);
			return;
		}
		bool all = true;
		for (uint32_t r = 0; r < rr.count; r ++)
		{
			const RomRow &row = rr.rows[r];
			std::string file;
			for (int k = 0; k < 4 && row.images[k] >= 0 && file.empty(); k ++)
				file = m_romFile[size_t(row.images[k])];
			all &= !file.empty();
			const float y = kRomY + float(r) * kRomRowH + 14;
			// A lamp: found or missing.
			beginPath();
			circle(kRomX + 6, y - 4, 5);
			fillColor(file.empty() ? th.bad : th.ok);
			fill();
			fontSize(14);
			fillColor(th.ink);
			text(kRomX + 22, y, row.chip, nullptr);
			fontSize(12);
			fillColor(th.inkDim);
			text(kRomX + 130, y, row.what, nullptr);
			fillColor(file.empty() ? th.warn : th.ink);
			text(kRomX + 640, y, file.empty() ? "missing" : file.c_str(), nullptr);
		}

		const Rect b = romButton(rr.count);
		const bool busy = m_romAsked != 0;
		drawButton(b, busy ? "LOOKING ..." : "CHOOSE A ROM FILE ...", !busy, !busy);

		textAlign(ALIGN_LEFT | ALIGN_BASELINE);
		fontSize(12);
		fillColor(th.inkDim);
		text(b.x + b.w + 20, b.y + 13, "Pick any image of the set, or a zip (MAME's). Its whole folder is searched; what is", nullptr);
		text(b.x + b.w + 20, b.y + 29, ("found is copied to " + m_romInstallDir + ", by content, whatever the names.").c_str(), nullptr);

		if (!m_romMsg.empty() && m_romMsgMachine == m)
		{
			fontSize(13);
			fillColor(all ? th.ok : th.warn);
			text(kRomX, b.y + b.h + 34, m_romMsg.c_str(), nullptr);
		}
		else if (all)
		{
			fontSize(13);
			fillColor(th.inkDim);
			text(kRomX, b.y + b.h + 34, "All there: it plays.", nullptr);
		}
	}

	void drawSettings()
	{
		const Theme &th = theme();
		drawChannels();

		textAlign(ALIGN_LEFT | ALIGN_BASELINE);
		fontSize(15);
		fillColor(th.ink);
		text(kChanX, kLookY - 22, "LOOK", nullptr);
		textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
		fontSize(12);
		fillColor(th.inkDim);
		text(kChanX, kLookY + 14, "For all instances", nullptr);
		drawButton(lookButton(kLookVintage), "VINTAGE", m_look == kLookVintage);
		drawButton(lookButton(kLookDark), "DARK", m_look == kLookDark);

		// What is still to come, to the right.
		const float rx = kChanX + kFlamKeyX;
		textAlign(ALIGN_LEFT | ALIGN_BASELINE);
		fontSize(15);
		fillColor(th.ink);
		text(rx, kLookY - 22, "TO COME", nullptr);
		textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
		fontSize(12);
		fillColor(th.inkDim);
		text(rx, kLookY + 14, "The assignment of the sounds to OUT 1-16.", nullptr);
	}

	// The note's name, middle C (60) as C4.
	static std::string noteName(int note)
	{
		static const char *const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
		return names[note % 12] + std::to_string(note / 12 - 1);
	}

	// A value with - and + either side.
	void drawStepper(const Rect &minus, const Rect &box, const Rect &plus, const char *value)
	{
		const Theme &th = theme();
		for (const Rect &b : { minus, plus })
		{
			beginPath();
			roundedRect(b.x, b.y, b.w, b.h, 3);
			fillColor(th.button);
			fill();
			strokeColor(th.edge);
			strokeWidth(1);
			stroke();
		}
		beginPath();
		roundedRect(box.x, box.y, box.w, box.h, 3);
		fillColor(th.field);
		fill();
		strokeColor(th.edge);
		strokeWidth(1);
		stroke();

		textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
		fontSize(17);
		fillColor(th.ink);
		text(minus.x + minus.w / 2, minus.y + minus.h / 2, "\xe2\x88\x92", nullptr);
		text(plus.x + plus.w / 2, plus.y + plus.h / 2, "+", nullptr);
		fontSize(15);
		text(box.x + box.w / 2, box.y + box.h / 2 + 1, value, nullptr);
	}

	void drawChannels()
	{
		const Theme &th = theme();
		textAlign(ALIGN_LEFT | ALIGN_BASELINE);
		fontSize(15);
		fillColor(th.ink);
		text(kChanX, kBody.y + 50, "MIDI CHANNELS", nullptr);
		fontSize(12);
		fillColor(th.inkDim);
		text(kChanX, kBody.y + 70, "Each machine listens on its own channel. Click - / + or use the mouse wheel.", nullptr);
		textAlign(ALIGN_CENTER | ALIGN_BASELINE);
		fontSize(12);
		fillColor(th.ink);
		text(kChanX + kFlamKeyX + 62, kBody.y + 80, "FLAM KEY", nullptr);
		text(kChanX + kFlamIntX + 62, kBody.y + 80, "FLAM INTERVAL", nullptr);

		for (uint32_t m = 0; m < kNumMachines; m ++)
		{
			const Rect minus = chanMinus(m), box = chanBox(m), plus = chanPlus(m);
			textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
			fontSize(15);
			fillColor(th.tab[m]);
			text(kChanX, box.y + box.h / 2, kMachineNames[m], nullptr);

			char value[16];
			std::snprintf(value, sizeof(value), "%d", m_channel[m]);
			drawStepper(minus, box, plus, value);

			// Flam: its key, its interval, and a lamp while it is on.
			const std::string key = noteName(int(std::lround(m_value[kParamFlamKey + m])));
			drawStepper(stepMinus(kFlamKeyX, m), stepBox(kFlamKeyX, m), stepPlus(kFlamKeyX, m), key.c_str());
			const int interval = std::max(1, std::min(4, int(std::lround(m_value[kParamFlamInterval + m]))));
			std::snprintf(value, sizeof(value), "%.0f ms", double(kFlamMs[interval - 1]));
			drawStepper(stepMinus(kFlamIntX, m), stepBox(kFlamIntX, m), stepPlus(kFlamIntX, m), value);
			beginPath();
			circle(kChanX + kFlamLampX, box.y + box.h / 2, 5);
			fillColor(m_value[kParamFlamOn + m] > 0.5f ? Color(255, 70, 50) : Color(th.edge, th.panel, 0.3f));
			fill();

			if (m == kTR727)
			{
				textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
				fontSize(12);
				fillColor(th.inkDim);
				text(plus.x + plus.w + 16, box.y + box.h / 2, "no ROMs yet", nullptr);
			}
			else
				drawButton(timingButton(m), "VINTAGE TIMING", m_value[kParamTiming + m] > 0.5f);
		}
		textAlign(ALIGN_LEFT | ALIGN_BASELINE);
		fontSize(12);
		fillColor(th.inkDim);
		text(kChanX, kChanY + 4 * kChanRowH + 8, "Vintage timing: each note as late and as uneven as on the unit: the MIDI line, the TR-707's", nullptr);
		text(kChanX, kChanY + 4 * kChanRowH + 24, "2.8 ms grid, the spread measured on the TR-505 and TR-626 (1.5-1.7 ms).", nullptr);
		text(kChanX + kFlamKeyX + 130, kChanY + 4 * kChanRowH + 8, "Flam is on while the key is held; the key plays nothing.", nullptr);
		text(kChanX + kFlamKeyX + 130, kChanY + 4 * kChanRowH + 24, "A softer hit goes first, the note follows.", nullptr);
	}

	void changeChannel(uint32_t m, int delta)
	{
		const int v = std::max(1, std::min(16, m_channel[m] + delta));
		if (v == m_channel[m])
			return;
		m_channel[m] = v;
		const uint32_t index = kParamChannel + m;
		m_value[index] = float(v);
		editParameter(index, true);
		setParameterValue(index, float(v));
		editParameter(index, false);
		repaint();
	}

	void drawFooter()
	{
		const Theme &th = theme();
		const float y = kH - kFooterH / 2;
		fontSize(12);
		textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
		fillColor(th.inkDim);
		text(kMargin, y, "Click a pad to hear it (Shift: softer). Drag a knob or use the wheel, Shift: fine; click twice: reset.", nullptr);
		textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
		text(kW - kMargin, y, "No Roland data included. Not affiliated with Roland.", nullptr);
	}

	// ---- the ROM setup -------------------------------------------------------------------------

	void browseRom(uint32_t m)
	{
		if (m_romAsked)
			return;
		const std::string title = std::string("Pick a ") + kMachineNames[m] + " ROM image or a zip -- the rest of its folder is searched too";
		FileBrowserOptions opts;
		opts.title = title.c_str();
		opts.startDir = m_romDir.empty() ? nullptr : m_romDir.c_str();
		m_browseMachine = int(m);
		if (!openFileBrowser(opts))
			m_browseMachine = -1;
	}

	// "romset" (OstrichShared.hpp): the files found, the install folder, the last answer.
	void setRomSet(const char *value)
	{
		std::string v(value ? value : "");
		size_t pos = 0;
		while (pos < v.size())
		{
			size_t eol = v.find('\n', pos);
			if (eol == std::string::npos)
				eol = v.size();
			std::vector<std::string> f;
			size_t a = pos;
			while (true)
			{
				const size_t t = v.find(kRomSetFieldSep, a);
				if (t == std::string::npos || t > eol)
				{
					f.push_back(v.substr(a, eol - a));
					break;
				}
				f.push_back(v.substr(a, t - a));
				a = t + 1;
			}
			if (f.size() >= 3 && f[0] == "img")
			{
				const int i = std::atoi(f[1].c_str());
				if (i >= 0 && i < kNumRomImagesUi)
					m_romFile[size_t(i)] = f[2];
			}
			else if (f.size() >= 2 && f[0] == "dir")
				m_romInstallDir = f[1];
			else if (f.size() >= 4 && f[0] == "msg")
			{
				const unsigned seq = unsigned(std::strtoul(f[2].c_str(), nullptr, 10));
				// Only the answer to this panel's own request (another one's is old news).
				if (seq != 0 && seq == m_romAsked)
				{
					m_romAsked = 0;
					m_romMsgMachine = uint32_t(std::atoi(f[1].c_str()));
					m_romMsg = f[3];
				}
			}
			pos = eol + 1;
		}
	}

	static std::string dirOf(const std::string &path)
	{
		const size_t cut = path.find_last_of("/\\");
		return cut == std::string::npos ? std::string() : path.substr(0, cut);
	}

	// ---- the window's size, as in Ostrich 220 ----------------------------------------------

	// The panel's size lives in the plugin's state, so the host saves it with the session.
	// Unscaled -- window pixels over the desktop scale factor -- so that it opens the same on
	// a screen with other scaling.
	void restoreSize(const char *value)
	{
		unsigned w = 0, h = 0;
		if (std::sscanf(value, "%u %u", &w, &h) != 2 || w < 100 || h < 20)
			return;
		if (std::fabs(double(w) / h - kW / kH) > 0.05)
			h = unsigned(std::lround(w * kH / kW));
		m_sizeSaved = value;
		const double scale = getWindow().getScaleFactor();
		const uint pw = uint(std::lround(w * scale)), ph = uint(std::lround(h * scale));
		if (std::abs(int(pw) - int(getWindow().getWidth())) > 2 || std::abs(int(ph) - int(getWindow().getHeight())) > 2)
			getWindow().setSize(pw, ph);
	}

	// Sends the size once it has stood still for a second -- not while the user drags, and
	// not in the first seconds, while the host still settles the window and the saved size
	// is on its way (restoreSize).
	void saveSizeWhenSettled()
	{
		const auto now = Clock::now();
		const uint pw = getWindow().getWidth(), ph = getWindow().getHeight();
		if (pw != m_sizeSeenW || ph != m_sizeSeenH)
		{
			m_sizeSeenW = pw;
			m_sizeSeenH = ph;
			m_sizeSeenSince = now;
			return;
		}
		if (now - m_created < std::chrono::seconds(3) || now - m_sizeSeenSince < std::chrono::seconds(1))
			return;
		const double scale = getWindow().getScaleFactor();
		char value[32];
		std::snprintf(value, sizeof(value), "%u %u", unsigned(std::lround(pw / scale)), unsigned(std::lround(ph / scale)));
		if (m_sizeSaved == value)
			return;
		unsigned sw = 0, sh = 0;
		if (std::sscanf(m_sizeSaved.c_str(), "%u %u", &sw, &sh) == 2
				&& std::abs(int(sw * scale) - int(pw)) <= 2 && std::abs(int(sh * scale) - int(ph)) <= 2)
			return;                     // the host's rounding, not a resize
		m_sizeSaved = value;
		setState("uisize", value);
	}

	// Cubase at 125 % tells a new view "scale 1.0" before it opens and sizes it for that,
	// while its own window is made for 125 %: the view sat in a black margin until reopened.
	// A view below its least size at the desktop's scale is that; it asks the host for the
	// least size, as a reopened view gets.  Once, after DPF's first idle.
	void fitHostFrame()
	{
		if (Clock::now() - m_created < std::chrono::milliseconds(200))
			return;
		m_frameFitted = true;
		bool keep = false;
		const Size<uint> least = getWindow().getGeometryConstraints(keep);
		const double scale = getWindow().getScaleFactor();
		const uint lw = uint(std::lround(least.getWidth() * scale)), lh = uint(std::lround(least.getHeight() * scale));
		if (getWindow().getWidth() + 2 < lw || getWindow().getHeight() + 2 < lh)
			getWindow().setSize(lw, lh);
	}

	// Mouse events come in the window's own pixels.  The panel is drawn in design units
	// scaled by getWidth() / kW on top of DGL's automatic scaling, so the full factor is the
	// window width over the design width.
	float mouseScale() const
	{
		return float(getWindow().getWidth()) / kW;
	}

	uint32_t m_tab = kTR505;
	int m_channel[kNumMachines];
	float m_value[kParamCount];
	int m_look = kLookVintage;

	// Knobs and pads.
	int m_drag = -1;                            // the parameter being dragged, or -1
	float m_dragY = 0, m_dragFrom = 0;
	int m_lastKnob = -1;                        // for a second click: reset
	Clock::time_point m_lastKnobAt {};
	Clock::time_point m_flash[kNumMachines][kMaxColumns * 3] {};
	uint8_t m_selected[kNumMachines][kMaxColumns] {};   // per column: the pad clicked last

	// The ROM setup.
	bool m_showRoms[kNumMachines] = {};         // the ROM page asked for, though complete
	std::vector<std::string> m_romFile;         // per RomImage: where it came from, or empty
	std::string m_romInstallDir, m_romDir, m_romMsg;
	int m_browseMachine = -1;                   // the file browser is open for this machine
	unsigned m_romSeq = 0, m_romAsked = 0;      // the request waiting for its answer, or 0
	uint32_t m_romAskedMachine = 0, m_romMsgMachine = 0;
	Clock::time_point m_romAskedAt {};

	const Clock::time_point m_created = Clock::now();
	std::string m_sizeSaved;                    // "<w> <h>" unscaled, as in the plugin state
	uint m_sizeSeenW = 0, m_sizeSeenH = 0;
	Clock::time_point m_sizeSeenSince {};
	bool m_frameFitted = false;                 // fitHostFrame

	DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OstrichUI)
};

UI *createUI()
{
	return new OstrichUI();
}

END_NAMESPACE_DISTRHO

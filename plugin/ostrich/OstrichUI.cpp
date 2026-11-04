// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ostrich 7765 -- the panel.  A header, a tab per machine and one for the settings, the
// machine's page, and a status line.  A machine's page has its ROM setup for now; the
// columns per sound (level, pads with their notes, the machine's own controls) follow in
// phase 4 (ROADMAP.md).

#include "DistrhoUI.hpp"
#include "OstrichShared.hpp"
#include "Version.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {

using namespace ostrich;

// Design units: the window's pixels at 100 %.
constexpr float kW = DISTRHO_UI_DEFAULT_WIDTH, kH = DISTRHO_UI_DEFAULT_HEIGHT;

struct Rect { float x, y, w, h; bool contains(float px, float py) const
	{ return px >= x && px < x + w && py >= y && py < y + h; } };

constexpr float kMargin = 16.0f;
constexpr float kHeaderH = 64.0f;
constexpr float kTabY = 72.0f, kTabH = 30.0f, kTabW = 132.0f, kTabGap = 6.0f;
constexpr float kBodyY = kTabY + kTabH;
constexpr float kFooterH = 30.0f;
const Rect kBody { kMargin, kBodyY, kW - 2 * kMargin, kH - kBodyY - kFooterH - 6.0f };

inline Rect tabRect(uint32_t t)
{
	return { kMargin + float(t) * (kTabW + kTabGap), kTabY, kTabW, kTabH };
}

// A colour per tab, as the stripes over the columns in the reference panel.
const Color kTabColour[kNumTabs] = {
	Color(232, 140, 40),    // TR-505
	Color(80, 160, 225),    // TR-626
	Color(215, 58, 48),     // TR-707
	Color(232, 196, 60),    // TR-727
	Color(170, 170, 176),   // Settings
};

const char *const kTabLabels[kNumTabs] = { "TR-505", "TR-626", "TR-707", "TR-727", "SETTINGS" };

// What each page says until it has its sounds.
const char *const kMachineTitles[kNumMachines] = {
	"TR-505  RHYTHM COMPOSER", "TR-626  RHYTHM COMPOSER", "TR-707  RHYTHM COMPOSER",
	"TR-727  RHYTHM COMPOSER",
};

// ---- the ROM setup on each machine's page ----------------------------------------------

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

// The settings page: a row per machine with its MIDI channel, - and + either side.
const float kChanX = kBody.x + 40, kChanY = kBody.y + 92;
constexpr float kChanRowH = 44;
constexpr float kChanBtn = 28, kChanBoxW = 56;
inline Rect chanMinus(uint32_t m) { return { kChanX + 150, kChanY + m * kChanRowH, kChanBtn, kChanBtn }; }
inline Rect chanBox(uint32_t m) { return { kChanX + 150 + kChanBtn + 6, kChanY + m * kChanRowH, kChanBoxW, kChanBtn }; }
inline Rect chanPlus(uint32_t m) { return { kChanX + 150 + kChanBtn + 12 + kChanBoxW, kChanY + m * kChanRowH, kChanBtn, kChanBtn }; }

const Color kBack(28, 28, 30), kPanel(36, 36, 40), kEdge(58, 58, 64);
const Color kInk(235, 235, 235), kInkDim(150, 150, 156), kOrange(232, 128, 40);

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
		m_romFile.resize(kNumRomImagesUi);
	}

protected:
	void parameterChanged(uint32_t index, float value) override
	{
		if (index >= kParamChannel && index < kParamChannel + kMachineCount)
		{
			m_channel[index - kParamChannel] = int(std::lround(value));
			repaint();
		}
	}

	bool onScroll(const ScrollEvent &ev) override
	{
		if (m_tab != kTabSettings)
			return false;
		const float s = mouseScale();
		const float x = float(ev.pos.getX()) / s, y = float(ev.pos.getY()) / s;
		for (uint32_t m = 0; m < kNumMachines; m ++)
			if (chanBox(m).contains(x, y) || chanMinus(m).contains(x, y) || chanPlus(m).contains(x, y))
			{
				if (ev.delta.getY() != 0)
					changeChannel(m, ev.delta.getY() > 0 ? 1 : -1);
				return true;
			}
		return false;
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
		if (ev.button != 1 || !ev.press)
			return false;
		const float s = mouseScale();
		const float x = float(ev.pos.getX()) / s, y = float(ev.pos.getY()) / s;
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
			for (uint32_t m = 0; m < kNumMachines; m ++)
			{
				if (chanMinus(m).contains(x, y))
					return changeChannel(m, -1), true;
				if (chanPlus(m).contains(x, y))
					return changeChannel(m, 1), true;
			}
		if (m_tab < kNumMachines && kRomRows[m_tab].count
		    && romButton(kRomRows[m_tab].count).contains(x, y))
			return browseRom(m_tab), true;
		return false;
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
		m_romAskedAt = std::chrono::steady_clock::now();
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
		if (!m_frameFitted && std::chrono::steady_clock::now() - m_created < std::chrono::seconds(2))
			fitHostFrame();
		saveSizeWhenSettled();
		// No answer from the DSP: say so rather than wait for ever.
		if (m_romAsked && std::chrono::steady_clock::now() - m_romAskedAt > std::chrono::seconds(20))
		{
			m_romAsked = 0;
			m_romMsgMachine = m_romAskedMachine;
			m_romMsg = "No answer from the plugin. Is it running (an active track)?";
			repaint();
		}
	}

private:
	void drawPanel()
	{
		const float s = getWidth() / kW;
		scale(s, s);

		beginPath();
		rect(0, 0, kW, kH);
		fillColor(kBack);
		fill();

		// The stripe along the top, in the colours of the machines.
		const float sw = kW / float(kNumMachines);
		for (uint32_t m = 0; m < kNumMachines; m ++)
		{
			beginPath();
			rect(float(m) * sw, 0, sw, 4);
			fillColor(kTabColour[m]);
			fill();
		}

		fontFace(NANOVG_DEJAVU_SANS_TTF);
		textAlign(ALIGN_LEFT | ALIGN_BASELINE);
		fillColor(kInk);
		fontSize(28);
		text(kMargin, 46, "OSTRICH 7765", nullptr);
		fontSize(12);
		fillColor(kOrange);
		text(kMargin + 218, 46, "TR-505  \xc2\xb7  TR-626  \xc2\xb7  TR-707  \xc2\xb7  TR-727", nullptr);

		textAlign(ALIGN_RIGHT | ALIGN_BASELINE);
		fontSize(12);
		fillColor(kInkDim);
		text(kW - kMargin, 46, "Ostrich 7765 " OSTRICH_VERSION_STRING "  \xe2\x80\x93  github.com/Helge666/ostrich7765", nullptr);

		drawTabs();
		drawBody();
		drawFooter();
	}

	void drawTabs()
	{
		for (uint32_t t = 0; t < kNumTabs; t ++)
		{
			const Rect r = tabRect(t);
			const bool on = t == m_tab;
			beginPath();
			roundedRect(r.x, r.y, r.w, r.h + (on ? 4 : -2), 4);
			fillColor(on ? kPanel : Color(30, 30, 33));
			fill();
			strokeColor(on ? kEdge : Color(44, 44, 48));
			strokeWidth(1);
			stroke();
			beginPath();
			rect(r.x + 10, r.y + 4, r.w - 20, 2);
			fillColor(on ? kTabColour[t] : Color(kTabColour[t], Color(30, 30, 33), 0.65f));
			fill();
			fontSize(13);
			textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
			fillColor(on ? kInk : kInkDim);
			text(r.x + r.w / 2, r.y + r.h / 2 + 2, kTabLabels[t], nullptr);
		}
	}

	void drawBody()
	{
		beginPath();
		roundedRect(kBody.x, kBody.y, kBody.w, kBody.h, 4);
		fillColor(kPanel);
		fill();
		strokeColor(kEdge);
		strokeWidth(1);
		stroke();
		// The open tab merges into the page.
		const Rect t = tabRect(m_tab);
		beginPath();
		rect(t.x + 1, kBody.y - 1, t.w - 2, 3);
		fillColor(kPanel);
		fill();

		if (m_tab < kNumMachines)
			return drawRoms(m_tab);
		drawChannels();

		// What is still to come, to the right.
		const float rx = kBody.x + kBody.w * 0.55f;
		textAlign(ALIGN_LEFT | ALIGN_BASELINE);
		fontSize(15);
		fillColor(kInk);
		text(rx, kBody.y + 50, "TO COME", nullptr);
		fontSize(13);
		fillColor(kInkDim);
		text(rx, kBody.y + 110, "The assignment of the sounds to OUT 1-16.", nullptr);
		text(rx, kBody.y + 132, "Vintage timing: the original CPU's MIDI grid and jitter.", nullptr);
	}

	// A machine's page: its title, the ROM images it needs and where they came from, a
	// button to pick a file, and the answer to the last pick.
	void drawRoms(uint32_t m)
	{
		textAlign(ALIGN_LEFT | ALIGN_BASELINE);
		fontSize(22);
		fillColor(kTabColour[m]);
		text(kRomX, kBody.y + 44, kMachineTitles[m], nullptr);
		fontSize(15);
		fillColor(kInk);
		text(kRomX, kBody.y + 76, "ROM IMAGES", nullptr);

		const RomRows &rr = kRomRows[m];
		if (rr.count == 0)
		{
			fontSize(13);
			fillColor(kInkDim);
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
			fillColor(file.empty() ? Color(150, 60, 52) : Color(90, 190, 100));
			fill();
			fontSize(14);
			fillColor(kInk);
			text(kRomX + 22, y, row.chip, nullptr);
			fontSize(12);
			fillColor(kInkDim);
			text(kRomX + 130, y, row.what, nullptr);
			fillColor(file.empty() ? Color(210, 120, 100) : kInk);
			text(kRomX + 640, y, file.empty() ? "missing" : file.c_str(), nullptr);
		}

		const Rect b = romButton(rr.count);
		const bool busy = m_romAsked != 0;
		beginPath();
		roundedRect(b.x, b.y, b.w, b.h, 4);
		fillColor(busy ? Color(40, 40, 44) : Color(48, 48, 54));
		fill();
		strokeColor(busy ? kEdge : Color(kTabColour[m], kEdge, 0.4f));
		strokeWidth(1);
		stroke();
		textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
		fontSize(13);
		fillColor(busy ? kInkDim : kInk);
		text(b.x + b.w / 2, b.y + b.h / 2 + 1, busy ? "LOOKING ..." : "CHOOSE A ROM FILE ...", nullptr);

		textAlign(ALIGN_LEFT | ALIGN_BASELINE);
		fontSize(12);
		fillColor(kInkDim);
		text(b.x + b.w + 20, b.y + 13, "Pick any image of the set, or a zip (MAME's). Its whole folder is searched; what is", nullptr);
		text(b.x + b.w + 20, b.y + 29, ("found is copied to " + m_romInstallDir + ", by content, whatever the names.").c_str(), nullptr);

		if (!m_romMsg.empty() && m_romMsgMachine == m)
		{
			fontSize(13);
			fillColor(all ? Color(140, 210, 140) : Color(230, 170, 120));
			text(kRomX, b.y + b.h + 34, m_romMsg.c_str(), nullptr);
		}
		else if (all)
		{
			fontSize(13);
			fillColor(kInkDim);
			text(kRomX, b.y + b.h + 34, "All there: it plays. Its sounds and controls follow on this page (ROADMAP.md, phase 4).", nullptr);
		}
	}

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

	void drawChannels()
	{
		textAlign(ALIGN_LEFT | ALIGN_BASELINE);
		fontSize(15);
		fillColor(kInk);
		text(kChanX, kBody.y + 50, "MIDI CHANNELS", nullptr);
		fontSize(12);
		fillColor(kInkDim);
		text(kChanX, kBody.y + 70, "Each machine listens on its own channel. Click - / + or use the mouse wheel.", nullptr);

		for (uint32_t m = 0; m < kNumMachines; m ++)
		{
			const Rect minus = chanMinus(m), box = chanBox(m), plus = chanPlus(m);
			textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
			fontSize(15);
			fillColor(kTabColour[m]);
			text(kChanX, box.y + box.h / 2, kMachineNames[m], nullptr);

			for (const Rect &b : { minus, plus })
			{
				beginPath();
				roundedRect(b.x, b.y, b.w, b.h, 3);
				fillColor(Color(48, 48, 54));
				fill();
				strokeColor(kEdge);
				strokeWidth(1);
				stroke();
			}
			beginPath();
			roundedRect(box.x, box.y, box.w, box.h, 3);
			fillColor(kBack);
			fill();
			strokeColor(kEdge);
			strokeWidth(1);
			stroke();

			textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
			fontSize(17);
			fillColor(kInk);
			text(minus.x + minus.w / 2, minus.y + minus.h / 2, "\xe2\x88\x92", nullptr);
			text(plus.x + plus.w / 2, plus.y + plus.h / 2, "+", nullptr);
			char value[8];
			std::snprintf(value, sizeof(value), "%d", m_channel[m]);
			fontSize(15);
			text(box.x + box.w / 2, box.y + box.h / 2 + 1, value, nullptr);

			if (m == kTR727)
			{
				textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
				fontSize(12);
				fillColor(kInkDim);
				text(plus.x + plus.w + 16, box.y + box.h / 2, "no ROMs yet", nullptr);
			}
		}
	}

	void changeChannel(uint32_t m, int delta)
	{
		const int v = std::max(1, std::min(16, m_channel[m] + delta));
		if (v == m_channel[m])
			return;
		m_channel[m] = v;
		const uint32_t index = kParamChannel + m;
		editParameter(index, true);
		setParameterValue(index, float(v));
		editParameter(index, false);
		repaint();
	}

	void drawFooter()
	{
		const float y = kH - kFooterH / 2;
		fontSize(12);
		textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
		fillColor(kInkDim);
		text(kMargin, y, "Plays from MIDI, one channel per machine (SETTINGS). Needs your own ROM images (each machine's page).", nullptr);
		textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
		text(kW - kMargin, y, "No Roland data included. Not affiliated with Roland.", nullptr);
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
		const auto now = std::chrono::steady_clock::now();
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
		if (std::chrono::steady_clock::now() - m_created < std::chrono::milliseconds(200))
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

	// The ROM setup.
	std::vector<std::string> m_romFile;         // per RomImage: where it came from, or empty
	std::string m_romInstallDir, m_romDir, m_romMsg;
	int m_browseMachine = -1;                   // the file browser is open for this machine
	unsigned m_romSeq = 0, m_romAsked = 0;      // the request waiting for its answer, or 0
	uint32_t m_romAskedMachine = 0, m_romMsgMachine = 0;
	std::chrono::steady_clock::time_point m_romAskedAt {};

	const std::chrono::steady_clock::time_point m_created = std::chrono::steady_clock::now();
	std::string m_sizeSaved;                    // "<w> <h>" unscaled, as in the plugin state
	uint m_sizeSeenW = 0, m_sizeSeenH = 0;
	std::chrono::steady_clock::time_point m_sizeSeenSince {};
	bool m_frameFitted = false;                 // fitHostFrame

	DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OstrichUI)
};

UI *createUI()
{
	return new OstrichUI();
}

END_NAMESPACE_DISTRHO

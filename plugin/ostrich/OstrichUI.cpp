// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ostrich 7765 -- the panel.  A header, a tab per machine and one for the settings, the
// machine's page, and a status line.  Phase 0 (ROADMAP.md): the pages are placeholders;
// the columns per sound (level, pads with their notes, the machine's own controls) follow
// in phase 4.

#include "DistrhoUI.hpp"
#include "OstrichShared.hpp"
#include "Version.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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
const char *const kMachineNotes[kNumMachines] = {
	"The sounds follow once the ROM analysis is done (ROADMAP.md, phases 1 and 2).",
	"The sounds follow once the ROM analysis is done (ROADMAP.md, phases 1 and 2).",
	"The sounds follow once the ROM analysis is done (ROADMAP.md, phases 1 and 2).",
	"Prepared. The TR-727's sound ROMs have not been dumped yet.",
};

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
	}

protected:
	void parameterChanged(uint32_t, float) override
	{
	}

	void stateChanged(const char *key, const char *value) override
	{
		if (std::strcmp(key, "uisize") == 0)
			restoreSize(value);
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
		return false;
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

		const float cx = kBody.x + kBody.w / 2, cy = kBody.y + kBody.h / 2;
		textAlign(ALIGN_CENTER | ALIGN_BASELINE);
		if (m_tab < kNumMachines)
		{
			fontSize(22);
			fillColor(kTabColour[m_tab]);
			text(cx, cy - 10, kMachineTitles[m_tab], nullptr);
			fontSize(13);
			fillColor(kInkDim);
			text(cx, cy + 20, kMachineNotes[m_tab], nullptr);
			return;
		}
		fontSize(22);
		fillColor(kInk);
		text(cx, cy - 40, "SETTINGS", nullptr);
		fontSize(13);
		fillColor(kInkDim);
		text(cx, cy - 10, "To come: a MIDI channel per machine, the assignment of the sounds to", nullptr);
		text(cx, cy + 10, "OUT 1-16, vintage timing (the original CPU's MIDI grid and jitter),", nullptr);
		text(cx, cy + 30, "and the ROM images found for each machine.", nullptr);
	}

	void drawFooter()
	{
		const float y = kH - kFooterH / 2;
		fontSize(12);
		textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
		fillColor(kInkDim);
		text(kMargin, y, "Plays from MIDI, one channel per machine. Nothing plays yet: this is the scaffold.", nullptr);
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

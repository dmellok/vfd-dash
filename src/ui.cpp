#include "ui.h"

#include "net.h"
#include "models.h"
#include <U8g2lib.h>
#include <time.h>

// ============================================================================
//  Font themes — each theme is a trio (title / body / tiny) so the UI picks
//  the right size for each row without going out of bounds when the user
//  cycles fonts. All "_tf"/"_te" variants so ° renders where used.
// ============================================================================
struct FontTheme {
    const uint8_t* title;
    const uint8_t* body;
    const uint8_t* tiny;
    const char*    name;
};

static const FontTheme kFonts[] = {
    { u8g2_font_profont12_tf, u8g2_font_profont11_tf, u8g2_font_5x7_tf,            "PROFONT" },
    { u8g2_font_helvB10_te,   u8g2_font_helvR10_te,   u8g2_font_4x6_tf,            "HELV"    },
    { u8g2_font_6x13_tf,      u8g2_font_6x10_tf,      u8g2_font_5x7_tf,            "6X10"    },
    { u8g2_font_courB10_tf,   u8g2_font_courR10_tf,   u8g2_font_tom_thumb_4x6_tf,  "COURIER" },
    { u8g2_font_t0_12_tf,     u8g2_font_t0_11_tf,     u8g2_font_5x7_tf,            "T0"      },
};
static constexpr uint8_t kFontCount = sizeof(kFonts) / sizeof(kFonts[0]);

// Custom cat face XBMs — bundled U8g2 has no cat glyph in any icon font.
// Bit 0 of each byte is the leftmost pixel (drawXBM convention in u8g2).
// Big version (11×9) is used on the CATS page; small version (9×8) is used
// on the dash where vertical space is tight.
static const uint8_t kCatIcon11x9[] = {
    0x02, 0x02,   // .#.......#.   ear tips
    0x03, 0x06,   // ##.......##   ears grow
    0x07, 0x07,   // ###.....###   ear bases
    0xFF, 0x07,   // ###########   head top
    0xFF, 0x07,
    0xDB, 0x06,   // ##.##.##.##   eyes
    0xFB, 0x06,   // ##.#####.##   cheeks
    0xFE, 0x03,   // .#########.   jaw
    0xFC, 0x01,   // ..#######..   chin
};
static constexpr int kCatIconW = 11;
static constexpr int kCatIconH = 9;

static const uint8_t kCatIcon9x8[] = {
    0x01, 0x01,   // #.......#   ear tips
    0x83, 0x01,   // ##.....##   ear tops
    0xC7, 0x01,   // ###...###   ear bases
    0xFF, 0x01,   // #########   head top
    0x6D, 0x01,   // #.##.##.#   eyes
    0x01, 0x01,   // #.......#   face sides
    0xFE, 0x00,   // .#######.   jaw
    0x7C, 0x00,   // ..#####..   chin
};
static constexpr int kCatIconSmallW = 9;
static constexpr int kCatIconSmallH = 8;

// Globe icon used on the dash to label the IP-address rotating slot.
static const uint8_t kNetIcon9x8[] = {
    0x7C, 0x00,   // ..#####..   top arc
    0x82, 0x00,   // .#.....#.   sides
    0x29, 0x01,   // #..#.#..#   meridians
    0x7D, 0x01,   // #.#####.#   equator
    0x29, 0x01,   // #..#.#..#
    0x82, 0x00,   // .#.....#.
    0x7C, 0x00,   // ..#####..   bottom arc
    0x00, 0x00,
};

// Tiny print-head + bed icon used on the dash to label live Prusa stats.
static const uint8_t kPrusaIcon9x8[] = {
    0x7C, 0x00,   // ..#####..   carriage top
    0x7C, 0x00,   // ..#####..   carriage bottom
    0x38, 0x00,   // ...###...   nozzle shoulder
    0x10, 0x00,   // ....#....   nozzle tip
    0x00, 0x00,   // .........   gap
    0xFF, 0x01,   // #########   bed
    0x00, 0x00,
    0x00, 0x00,
};

// Sunburst/sparkle icon used on the dash to label Claude usage stats.
static const uint8_t kClaudeIcon9x8[] = {
    0x10, 0x00,   // ....#....   centre point
    0x54, 0x00,   // ..#.#.#..
    0xBA, 0x00,   // .#.###.#.
    0x7C, 0x00,   // ..#####..
    0xBB, 0x01,   // ##.###.##  full burst
    0x7C, 0x00,   // ..#####..
    0xBA, 0x00,   // .#.###.#.
    0x54, 0x00,   // ..#.#.#..
};

enum PageId {
    PAGE_OVERVIEW = 0,
    PAGE_TIME,
    PAGE_WEATHER,
    PAGE_NOW_PLAYING,
    PAGE_MATRIX,
    PAGE_CATS,
    PAGE_TAMA,
    PAGE_CLAUDE,
    PAGE_PORTAL,
    PAGE_PRUSA,
    PAGE_WATCH,
    PAGE_COUNT
};

enum VizStyle {
    VIZ_SPECTRUM = 0, VIZ_BARS, VIZ_WAVE, VIZ_DOTS, VIZ_VU,
    VIZ_MIRROR, VIZ_BLOCKS, VIZ_STARS, VIZ_COMETS, VIZ_PULSE,
    VIZ_COUNT
};

static const char* kVizNames[VIZ_COUNT] = {
    "SPECTRUM", "BARS", "WAVE", "DOTS", "VU-METER",
    "MIRROR", "BLOCKS", "STARS", "COMETS", "PULSE"
};

enum ClockAnim {
    CLK_PLAIN = 0, CLK_MATRIX, CLK_TAMA, CLK_GLITCH,
    CLK_ANIM_COUNT
};

static const char* kClockNames[CLK_ANIM_COUNT] = {
    "PLAIN", "MATRIX", "TAMA", "GLITCH"
};

// Clock face font options cycled via `cfnext` / `cfprev`. `height` drives the
// vertical centering in renderTimeDetail, and colons are drawn manually so
// every listed font can be a digits-only (`_tn` / `_mn`) variant.
struct ClockFontEntry {
    const uint8_t* font;
    const char*    name;
    uint8_t        height;
};

static const ClockFontEntry kClockFonts[] = {
    { u8g2_font_logisoso22_tn,      "LOGI-22",   22 },
    { u8g2_font_logisoso18_tn,      "LOGI-18",   18 },
    { u8g2_font_logisoso28_tn,      "LOGI-28",   28 },
    { u8g2_font_inb19_mn,           "INB-19",    19 },
    { u8g2_font_inb27_mn,           "INB-27",    27 },
    { u8g2_font_7Segments_26x42_mn, "7-SEG",     42 },
};
static constexpr int kClockFontCount = sizeof(kClockFonts) / sizeof(kClockFonts[0]);

// Forward declarations so renderTimeDetail (declared above these helpers)
// can call them from the clock-background modes.
static void drawMatrixRain(VFD& v);
static void drawTamaBackground(VFD& v);
static void formatCountdown(char* buf, size_t n, long secs);
namespace tama { static void drawBubbleIfActive(VFD& v); }

static uint8_t s_page = 0;
static uint8_t s_font = 0;
static uint8_t s_viz  = 0;
static uint8_t s_clk  = 0;
static uint8_t s_cfont = 0;       // clock face font index
static bool    s_use12h = false;
static bool    s_dashGlitch = false;
static float   s_matrixBrightP = -1.0f;  // <0 = hidden, else 0..1 fill
static uint8_t  s_dashRight = 0;          // 0 = viz, 1 = claude bar
static uint32_t s_dashRightChangedAt = 0;
constexpr uint8_t kDashRightCount = 2;
static uint32_t s_pageChangedAt = 0;
static uint32_t s_fontChangedAt = 0;
static uint32_t s_vizChangedAt  = 0;
static uint32_t s_clkChangedAt  = 0;
static uint32_t s_cfontChangedAt = 0;
static uint32_t s_12hChangedAt  = 0;

// ----- Public API --------------------------------------------------------
void uiBegin(uint8_t page, uint8_t font, uint8_t viz, uint8_t clk) {
    s_page = page < PAGE_COUNT ? page : 0;
    s_font = font < kFontCount ? font : 0;
    s_viz  = viz  < VIZ_COUNT  ? viz  : 0;
    s_clk  = clk  < CLK_ANIM_COUNT  ? clk  : 0;
}
uint8_t uiPageIndex()  { return s_page; }
uint8_t uiFontIndex()  { return s_font; }
uint8_t uiVizIndex()   { return s_viz;  }
uint8_t uiClockIndex() { return s_clk;  }

const char* uiPageName(uint8_t idx) {
    static const char* kNames[] = {
        "OVERVIEW", "TIME", "WEATHER", "NOW PLAYING", "MATRIX",
        "CATS", "TAMAGOTCHI", "CLAUDE", "PORTAL", "PRUSA", "WATCH",
    };
    return idx < (sizeof(kNames) / sizeof(kNames[0])) ? kNames[idx] : "?";
}

void uiNextPage()  { s_page = (s_page + 1) % PAGE_COUNT; s_pageChangedAt = millis(); }
void uiPrevPage()  { s_page = (s_page + PAGE_COUNT - 1) % PAGE_COUNT; s_pageChangedAt = millis(); }
void uiSetPage(uint8_t idx) {
    if (idx >= PAGE_COUNT) idx = 0;
    s_page = idx;
    s_pageChangedAt = millis();
}
void uiNextFont()  { s_font = (s_font + 1) % kFontCount; s_fontChangedAt = millis(); }
void uiPrevFont()  { s_font = (s_font + kFontCount - 1) % kFontCount; s_fontChangedAt = millis(); }
void uiNextViz()   { s_viz  = (s_viz  + 1) % VIZ_COUNT;  s_vizChangedAt  = millis(); }
void uiPrevViz()   { s_viz  = (s_viz  + VIZ_COUNT - 1) % VIZ_COUNT;  s_vizChangedAt = millis(); }
void uiNextClock() { s_clk  = (s_clk  + 1) % CLK_ANIM_COUNT;  s_clkChangedAt  = millis(); }
void uiPrevClock() { s_clk  = (s_clk  + CLK_ANIM_COUNT - 1) % CLK_ANIM_COUNT;  s_clkChangedAt = millis(); }
void uiNextClockFont() { s_cfont = (s_cfont + 1) % kClockFontCount; s_cfontChangedAt = millis(); }
void uiPrevClockFont() { s_cfont = (s_cfont + kClockFontCount - 1) % kClockFontCount; s_cfontChangedAt = millis(); }
uint8_t uiClockFontIndex() { return s_cfont; }
void uiSetClockFont(uint8_t idx) { s_cfont = (idx < kClockFontCount) ? idx : 0; }
void uiSet12h(bool on) { s_use12h = on; s_12hChangedAt = millis(); }
bool uiIs12h() { return s_use12h; }
void uiSetDashGlitch(bool on) { s_dashGlitch = on; }
bool uiIsDashGlitch() { return s_dashGlitch; }

void uiNextDashRight() {
    s_dashRight = (uint8_t)((s_dashRight + 1) % kDashRightCount);
    s_dashRightChangedAt = millis();
}
uint8_t uiDashRightView() { return s_dashRight; }

void uiSetMatrixBrightProgress(float p) {
    if (p < 0.0f) { s_matrixBrightP = -1.0f; return; }
    if (p > 1.0f) p = 1.0f;
    s_matrixBrightP = p;
}

// ----- Small drawing helpers --------------------------------------------
static const uint8_t* titleFont() { return kFonts[s_font].title; }
static const uint8_t* bodyFont()  { return kFonts[s_font].body;  }
static const uint8_t* tinyFont()  { return kFonts[s_font].tiny;  }

static int utf8Width(VFD& v, const char* s) { return v.getUTF8Width(s); }

// Centered tooltip-style dialog. yCenter selects which row the box centres
// on — 25 (full-display centre) for whole-page placeholders, or e.g. 30 to
// fit inside a page that has a fixed header.
static void drawTooltipAtY(VFD& v, const char* msg, int yCenter) {
    v.setFont(tinyFont());
    int tw = v.getUTF8Width(msg);
    const int padX = 6;
    int boxW = tw + padX * 2;
    if (boxW > 240) boxW = 240;
    const int boxH = 13;
    const int boxX = (256 - boxW) / 2;
    const int boxY = yCenter - boxH / 2;
    v.setDrawColor(0);
    v.drawBox(boxX - 1, boxY - 1, boxW + 2, boxH + 2);
    v.setDrawColor(1);
    v.drawFrame(boxX, boxY, boxW, boxH);
    v.drawUTF8(boxX + (boxW - tw) / 2, boxY + 9, msg);
}

static void drawTooltip(VFD& v, const char* msg) {
    drawTooltipAtY(v, msg, 25);
}

// Draw UTF-8 text possibly horizontally scrolling if it exceeds maxW.
// y is the BASELINE. Returns the width that was drawn (clipped to maxW).
static void drawScrolling(VFD& v, int x, int y, int maxW, const String& text) {
    int tw = utf8Width(v, text.c_str());
    if (tw <= maxW) {
        v.drawUTF8(x, y, text.c_str());
        return;
    }
    const int gap = 24;
    int period = tw + gap;
    int offset = (int)((millis() / 40) % period);    // ~25 px/s
    v.setClipWindow(x, 0, x + maxW, 63);
    v.drawUTF8(x - offset, y, text.c_str());
    v.drawUTF8(x - offset + period, y, text.c_str());
    v.setMaxClipWindow();
}

// Play-triangle glyph, 4 wide × h tall, filled.
static void drawPlayTriangle(VFD& v, int x, int y, int h) {
    for (int i = 0; i < h / 2; ++i) {
        v.drawVLine(x + i, y + i, h - 2 * i);
    }
}

// Three-bar equalizer animation.
static void drawEqBars(VFD& v, int x, int baselineY, int barW, int gap, int maxH, bool active) {
    if (!active) {
        // Flat line when paused/idle.
        v.drawHLine(x, baselineY, 3 * barW + 2 * gap);
        return;
    }
    uint32_t t = millis();
    for (int i = 0; i < 3; ++i) {
        uint32_t phase = (t + i * 137) % 900;         // slightly unsynced bars
        int h = (phase < 450) ? (phase * maxH) / 450 : ((900 - phase) * maxH) / 450;
        if (h < 2) h = 2;
        v.drawBox(x + i * (barW + gap), baselineY - h + 1, barW, h);
    }
}

// ============================================================================
//  Visualizers — each occupies (x, baselineY, w, maxH). baselineY is the
//  bottom row of the viz; bars/waves extend upward. The caller picks the area
//  and the global s_viz selects the style.
// ============================================================================

// Helper: integer segment boundaries so the last bar always reaches x+w.
static inline int segEdge(int i, int n, int x, int w) { return x + (int)(((int64_t)i * w) / n); }

static void drawVizSpectrum(VFD& v, int x, int baselineY, int w, int maxH, bool active) {
    int n = w / 5;
    if (n < 12) n = 12;
    if (n > 64) n = 64;
    uint32_t t = millis();
    for (int i = 0; i < n; ++i) {
        int x0 = segEdge(i,     n, x, w);
        int x1 = segEdge(i + 1, n, x, w) - 1;
        if (x1 < x0) x1 = x0;
        int bw = x1 - x0 + 1;        // inclusive slot width
        if (bw < 1) bw = 1;

        int h;
        if (!active) h = 1;
        else {
            uint32_t p1 = (t * 2 + i * 211) % 900;
            uint32_t p2 = (t * 3 + i * 317) % 1400;
            int h1 = (p1 < 450) ? (p1 * maxH) / 450 : ((900  - p1) * maxH) / 450;
            int h2 = (p2 < 700) ? (p2 * maxH) / 700 : ((1400 - p2) * maxH) / 700;
            h = (h1 + h2) / 2;
            if (h < 1) h = 1;
        }
        v.drawBox(x0, baselineY - h + 1, bw, h);
    }
}

// Fatter bars with a "peak hold" marker that slowly falls back — the classic
// 90s hi-fi bar graph look.
static void drawVizBars(VFD& v, int x, int baselineY, int w, int maxH, bool active) {
    constexpr int kMax = 18;
    int n = w / 14;
    if (n < 8)  n = 8;
    if (n > kMax) n = kMax;
    static int16_t peaks[kMax] = {0};
    static uint32_t lastDecay  = 0;
    uint32_t t = millis();
    bool decay = (t - lastDecay) > 80;
    if (decay) lastDecay = t;

    for (int i = 0; i < n; ++i) {
        int x0 = segEdge(i,     n, x, w);
        int x1 = segEdge(i + 1, n, x, w) - 1;
        int bw = x1 - x0 + 1;        // inclusive slot width
        if (bw < 2) bw = 2;

        int h;
        if (!active) h = 1;
        else {
            uint32_t p = (t + i * 377u) % 1100;
            h = (p < 550) ? (p * maxH) / 550 : ((1100 - p) * maxH) / 550;
            if (h < 1) h = 1;
        }
        v.drawBox(x0, baselineY - h + 1, bw - 1, h);

        if (h > peaks[i]) peaks[i] = h;
        else if (decay && peaks[i] > 0) peaks[i]--;

        if (active && peaks[i] > h) {
            v.drawHLine(x0, baselineY - peaks[i] + 1, bw - 1);
        }
    }
}

// A continuous wiggly line — like an oscilloscope trace.
static void drawVizWave(VFD& v, int x, int baselineY, int w, int maxH, bool active) {
    int mid = baselineY - (maxH - 1) / 2;
    if (!active) {
        v.drawHLine(x, mid, w);
        return;
    }
    uint32_t t = millis();
    int amp = (maxH - 1) / 2;
    int prevY = mid;
    for (int i = 0; i < w; ++i) {
        // Two triangle waves added for a wobbly-but-bandlimited look.
        int a = (int)(((t / 11) + i * 4) % 180);
        int b = (int)(((t / 7)  + i * 7) % 140);
        int va = (a < 90) ? a - 45 : 135 - a;    // -45..45
        int vb = (b < 70) ? b - 35 : 105 - b;    // -35..35
        int val = (va + vb / 2);                 // ~-60..60
        int yy  = mid - (val * amp) / 60;
        if (yy < baselineY - maxH + 1) yy = baselineY - maxH + 1;
        if (yy > baselineY)            yy = baselineY;

        if (i == 0) v.drawPixel(x, yy);
        else {
            int y0 = prevY < yy ? prevY : yy;
            int y1 = prevY > yy ? prevY : yy;
            v.drawVLine(x + i, y0, y1 - y0 + 1);
        }
        prevY = yy;
    }
}

// Only the peak of each column, drawn as a dot. Sparse and elegant.
static void drawVizDots(VFD& v, int x, int baselineY, int w, int maxH, bool active) {
    int n = w / 5;
    if (n < 10) n = 10;
    if (n > 48) n = 48;
    uint32_t t = millis();
    for (int i = 0; i < n; ++i) {
        int x0 = segEdge(i,     n, x, w);
        int x1 = segEdge(i + 1, n, x, w);
        int cx = (x0 + x1 - 1) / 2;
        int h;
        if (!active) h = 1;
        else {
            uint32_t p = (t * 2 + i * 151u) % 800;
            h = (p < 400) ? (p * maxH) / 400 : ((800 - p) * maxH) / 400;
            if (h < 1) h = 1;
        }
        int yy = baselineY - h + 1;
        v.drawPixel(cx, yy);
        if (x1 - x0 >= 3) {
            v.drawPixel(cx - 1, yy);
            v.drawPixel(cx + 1, yy);
        }
        // Faint baseline so the viz doesn't look empty while idle.
        v.drawPixel(cx, baselineY);
    }
}

// A retro segmented VU meter — mirrors out from center with a peak hold bar.
static void drawVizVU(VFD& v, int x, int baselineY, int w, int maxH, bool active) {
    // Chunky horizontal segments growing outward from the centre, like stereo
    // VU bars. Height uses maxH vertically for visual weight.
    int topY    = baselineY - maxH + 1;
    int mid     = x + w / 2;
    int segW    = 4;
    int gap     = 1;
    int stride  = segW + gap;
    int nSegsSide = (w / 2 - 1) / stride;

    static int16_t peakL = 0, peakR = 0;
    static uint32_t lastDecay = 0;
    uint32_t t = millis();
    bool decay = (t - lastDecay) > 60;
    if (decay) lastDecay = t;

    int levelL, levelR;
    if (!active) { levelL = levelR = 0; }
    else {
        uint32_t pL = (t * 2) % 700;
        uint32_t pR = (t * 2 + 240) % 720;
        levelL = (pL < 350) ? (pL * nSegsSide) / 350 : ((700 - pL) * nSegsSide) / 350;
        levelR = (pR < 360) ? (pR * nSegsSide) / 360 : ((720 - pR) * nSegsSide) / 360;
    }
    if (levelL > peakL) peakL = levelL; else if (decay && peakL > 0) peakL--;
    if (levelR > peakR) peakR = levelR; else if (decay && peakR > 0) peakR--;

    // Enclosure
    v.drawHLine(x, topY,       w);
    v.drawHLine(x, baselineY,  w);
    v.drawVLine(mid, topY, maxH);

    for (int i = 0; i < nSegsSide; ++i) {
        int lx = mid - 2 - (i + 1) * stride + gap;
        int rx = mid + 2 +  i      * stride;
        if (i < levelL) v.drawBox(lx, topY + 1, segW, maxH - 2);
        if (i < levelR) v.drawBox(rx, topY + 1, segW, maxH - 2);
        if (active && i == peakL - 1 && peakL > 0)
            v.drawVLine(lx + segW / 2, topY + 1, maxH - 2);
        if (active && i == peakR - 1 && peakR > 0)
            v.drawVLine(rx + segW / 2, topY + 1, maxH - 2);
    }
}

// Bars mirrored top and bottom from a centerline — symmetric EQ look.
static void drawVizMirror(VFD& v, int x, int baselineY, int w, int maxH, bool active) {
    int n = w / 5;
    if (n < 10) n = 10;
    if (n > 64) n = 64;
    int mid = baselineY - maxH / 2;
    int halfH = (maxH - 1) / 2;
    if (halfH < 1) halfH = 1;
    uint32_t t = millis();

    for (int i = 0; i < n; ++i) {
        int x0 = segEdge(i,     n, x, w);
        int x1 = segEdge(i + 1, n, x, w) - 1;
        int bw = x1 - x0 + 1;        // inclusive slot width
        if (bw < 1) bw = 1;

        int h;
        if (!active) h = 1;
        else {
            uint32_t p = (t + i * 271u) % 900;
            h = (p < 450) ? (p * halfH) / 450 : ((900 - p) * halfH) / 450;
            if (h < 1) h = 1;
        }
        v.drawBox(x0, mid - h,       bw, h);
        v.drawBox(x0, mid + 1,       bw, h);
    }
}

// Chunky VU-style stacked blocks — each column is a stack of small boxes.
static void drawVizBlocks(VFD& v, int x, int baselineY, int w, int maxH, bool active) {
    constexpr int kCols = 12;
    int n = w / 14;
    if (n < 6)  n = 6;
    if (n > kCols) n = kCols;

    // One "block" is 2 tall + 1 gap = 3 px.
    int blockH   = 2;
    int blockGap = 1;
    int stride   = blockH + blockGap;
    int levels   = (maxH + blockGap) / stride;

    static uint8_t heights[kCols] = {0};
    static uint32_t lastStep = 0;
    uint32_t now = millis();
    bool step = (now - lastStep) > 55;
    if (step) lastStep = now;

    for (int i = 0; i < n; ++i) {
        int x0 = segEdge(i,     n, x, w);
        int x1 = segEdge(i + 1, n, x, w) - 1;
        int bw = x1 - x0 + 1;        // inclusive slot width
        if (bw < 2) bw = 2;

        if (step) {
            if (!active) {
                heights[i] = (heights[i] > 0) ? heights[i] - 1 : 0;
            } else {
                uint8_t target = 1 + (uint8_t)(random(levels));
                if (target > heights[i]) heights[i]++;
                else if (heights[i] > 0) heights[i]--;
            }
        }
        int H = heights[i];
        if (H > levels) H = levels;
        for (int lvl = 0; lvl < H; ++lvl) {
            int by = baselineY - lvl * stride;
            v.drawBox(x0, by - blockH + 1, bw - 1, blockH);
        }
    }
}

// Sparkle field — pseudo-random pixels across the viz area that flicker.
static void drawVizStars(VFD& v, int x, int baselineY, int w, int maxH, bool active) {
    int topY = baselineY - maxH + 1;
    if (topY < 0) topY = 0;
    uint32_t slot = millis() / (active ? 80 : 400);   // time bucket
    uint32_t density = active ? 22u : 120u;           // lower = more stars
    for (int px = 0; px < w; ++px) {
        for (int py = 0; py < maxH; ++py) {
            uint32_t h = (px * 7919u) ^ (py * 4999u) ^ (slot * 12289u);
            h = h * 2654435761u;
            if ((h % density) == 0) {
                v.drawPixel(x + px, topY + py);
            }
        }
    }
}

// Small comets racing horizontally across the viz area.
static void drawVizComets(VFD& v, int x, int baselineY, int w, int maxH, bool active) {
    constexpr int kComets = 8;
    uint32_t t = millis();
    int activeCount = active ? kComets : 2;
    for (int i = 0; i < activeCount; ++i) {
        int speed      = 20 + ((i * 13) % 40);        // ms per pixel
        int phase      = (int)(i * 977u) % (w + 40);
        int yRow       = (i * 13) % maxH;
        int posRaw     = (int)(t / speed) + phase;
        int pos        = posRaw % (w + 40) - 20;      // slide through with margin
        int cy         = baselineY - maxH + 1 + yRow;

        // Head pixel
        int hx = x + pos;
        if (hx >= x && hx < x + w) v.drawPixel(hx, cy);
        // Short trail — fades via stride skipping.
        for (int tr = 1; tr <= 5; ++tr) {
            int tx = hx - tr;
            if (tx < x || tx >= x + w) continue;
            if (tr <= 2)              v.drawPixel(tx, cy);         // bright
            else if ((tx & 1) == 0)   v.drawPixel(tx, cy);         // dim
        }
    }
}

// A pulsing horizontal bar that breathes wider / narrower from the centre.
static void drawVizPulse(VFD& v, int x, int baselineY, int w, int maxH, bool active) {
    int mid = baselineY - maxH / 2;
    uint32_t t = millis();
    if (!active) {
        // Thin static line.
        v.drawHLine(x + w / 2 - 10, mid, 20);
        return;
    }
    // Two oscillators: width + thickness.
    uint32_t p1 = (t)           % 1200;
    uint32_t p2 = (t + 400)     % 1600;
    int widthPx = (p1 < 600) ? (p1 * (w - 2)) / 600 : ((1200 - p1) * (w - 2)) / 600;
    int thick   = (p2 < 800) ? (p2 * maxH) / 800    : ((1600 - p2) * maxH) / 800;
    if (widthPx < 4)  widthPx = 4;
    if (thick   < 1)  thick   = 1;
    int bx = x + (w - widthPx) / 2;
    v.drawBox(bx, mid - thick / 2, widthPx, thick);
}

static void drawViz(VFD& v, int x, int baselineY, int w, int maxH, bool active) {
    switch (s_viz) {
        case VIZ_SPECTRUM: drawVizSpectrum(v, x, baselineY, w, maxH, active); break;
        case VIZ_BARS:     drawVizBars    (v, x, baselineY, w, maxH, active); break;
        case VIZ_WAVE:     drawVizWave    (v, x, baselineY, w, maxH, active); break;
        case VIZ_DOTS:     drawVizDots    (v, x, baselineY, w, maxH, active); break;
        case VIZ_VU:       drawVizVU      (v, x, baselineY, w, maxH, active); break;
        case VIZ_MIRROR:   drawVizMirror  (v, x, baselineY, w, maxH, active); break;
        case VIZ_BLOCKS:   drawVizBlocks  (v, x, baselineY, w, maxH, active); break;
        case VIZ_STARS:    drawVizStars   (v, x, baselineY, w, maxH, active); break;
        case VIZ_COMETS:   drawVizComets  (v, x, baselineY, w, maxH, active); break;
        case VIZ_PULSE:    drawVizPulse   (v, x, baselineY, w, maxH, active); break;
    }
}

static void drawColon(VFD& v, int x, int topY, int bottomY) {
    // Two 3×3 squares for a colon glyph between 7-seg digit groups.
    int dot = 4;
    int cy1 = topY + (bottomY - topY) / 3;
    int cy2 = topY + 2 * (bottomY - topY) / 3;
    v.drawBox(x, cy1 - dot/2, dot, dot);
    v.drawBox(x, cy2 - dot/2, dot, dot);
}

// ----- Weather icons (procedural) ----------------------------------------
enum WxKind { WX_UNKNOWN, WX_CLEAR, WX_PARTLY, WX_CLOUD, WX_FOG, WX_DRIZZLE,
              WX_RAIN, WX_SNOW, WX_THUNDER };

static WxKind classifyWmo(int code) {
    switch (code) {
        case 0:                         return WX_CLEAR;
        case 1: case 2:                 return WX_PARTLY;
        case 3:                         return WX_CLOUD;
        case 45: case 48:               return WX_FOG;
        case 51: case 53: case 55:
        case 56: case 57:               return WX_DRIZZLE;
        case 61: case 63: case 65:
        case 66: case 67:
        case 80: case 81: case 82:      return WX_RAIN;
        case 71: case 73: case 75: case 77:
        case 85: case 86:               return WX_SNOW;
        case 95: case 96: case 99:      return WX_THUNDER;
        default:                        return WX_UNKNOWN;
    }
}

static const char* weatherLabel(WxKind k) {
    switch (k) {
        case WX_CLEAR:   return "Clear";
        case WX_PARTLY:  return "Partly cloudy";
        case WX_CLOUD:   return "Overcast";
        case WX_FOG:     return "Fog";
        case WX_DRIZZLE: return "Drizzle";
        case WX_RAIN:    return "Rain";
        case WX_SNOW:    return "Snow";
        case WX_THUNDER: return "Thunder";
        default:         return "—";
    }
}

// Draw a weather icon into an s×s box starting at (x, y). s must be >= 12.
static void drawWxIcon(VFD& v, int x, int y, int s, WxKind k) {
    const int cx = x + s / 2;
    const int cy = y + s / 2;
    const int r  = s / 4;

    auto drawSun = [&](int ox, int oy) {
        v.drawDisc(cx + ox, cy + oy, r);
        // 8 rays
        for (int a = 0; a < 8; ++a) {
            float ang = a * 3.14159f / 4.0f;
            int x1 = cx + ox + (int)((r + 1) * cosf(ang));
            int y1 = cy + oy + (int)((r + 1) * sinf(ang));
            int x2 = cx + ox + (int)((r + 3) * cosf(ang));
            int y2 = cy + oy + (int)((r + 3) * sinf(ang));
            v.drawLine(x1, y1, x2, y2);
        }
    };

    auto drawCloud = [&](int ox, int oy, int size) {
        // Three discs + base — cartoony cloud.
        int cr = size / 4;
        v.drawDisc(cx + ox - cr, cy + oy,      cr);
        v.drawDisc(cx + ox + cr, cy + oy,      cr);
        v.drawDisc(cx + ox,      cy + oy - cr, cr + 1);
        v.drawBox (cx + ox - 2*cr, cy + oy, 4*cr, cr);
    };

    auto drawDrops = [&](int count) {
        for (int i = 0; i < count; ++i) {
            int px = x + 2 + (i * (s - 4) / count);
            int py = y + s - 3;
            v.drawPixel(px, py);
            v.drawPixel(px, py + 1);
            v.drawPixel(px + 1, py + 1);
        }
    };

    auto drawFlakes = [&](int count) {
        for (int i = 0; i < count; ++i) {
            int px = x + 2 + (i * (s - 4) / count);
            int py = y + s - 3;
            v.drawPixel(px, py);
            v.drawPixel(px - 1, py);
            v.drawPixel(px + 1, py);
            v.drawPixel(px, py - 1);
            v.drawPixel(px, py + 1);
        }
    };

    switch (k) {
        case WX_CLEAR:
            drawSun(0, 0);
            break;
        case WX_PARTLY:
            drawSun(-s / 4, -s / 4);
            drawCloud(s / 6, s / 6, s);
            break;
        case WX_CLOUD:
            drawCloud(0, 0, s);
            break;
        case WX_FOG: {
            // Horizontal lines
            for (int i = 0; i < 4; ++i) {
                int yy = y + 3 + i * (s - 6) / 3;
                v.drawHLine(x + 2, yy, s - 4);
            }
        } break;
        case WX_DRIZZLE:
            drawCloud(0, -s / 8, s);
            drawDrops(2);
            break;
        case WX_RAIN:
            drawCloud(0, -s / 8, s);
            drawDrops(4);
            break;
        case WX_SNOW:
            drawCloud(0, -s / 8, s);
            drawFlakes(3);
            break;
        case WX_THUNDER: {
            drawCloud(0, -s / 8, s);
            // Lightning bolt — zigzag
            int bx = cx, by = cy + 2;
            v.drawLine(bx, by,     bx - 2, by + 3);
            v.drawLine(bx - 2, by + 3, bx + 1, by + 3);
            v.drawLine(bx + 1, by + 3, bx - 1, by + 6);
        } break;
        default:
            v.drawFrame(x + 2, y + 2, s - 4, s - 4);
            v.drawStr(x + s / 2 - 2, y + s - 3, "?");
            break;
    }
}

// ----- Status formatting -------------------------------------------------
static void formatTime(char* buf, size_t n, const struct tm& t, bool withSeconds) {
    int h = t.tm_hour;
    if (s_use12h) {
        h = h % 12;
        if (h == 0) h = 12;
    }
    if (withSeconds) snprintf(buf, n, "%02d:%02d:%02d", h, t.tm_min, t.tm_sec);
    else             snprintf(buf, n, "%02d:%02d",      h, t.tm_min);
}

static void formatDate(char* buf, size_t n, const struct tm& t) {
    static const char* wd[]  = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
    static const char* mon[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                                "JUL","AUG","SEP","OCT","NOV","DEC"};
    snprintf(buf, n, "%s %d %s", wd[t.tm_wday % 7], t.tm_mday, mon[t.tm_mon % 12]);
}

static void formatDateLong(char* buf, size_t n, const struct tm& t) {
    static const char* wd[]  = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
    static const char* mon[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                                "JUL","AUG","SEP","OCT","NOV","DEC"};
    snprintf(buf, n, "%s %d %s %d",
             wd[t.tm_wday % 7], t.tm_mday, mon[t.tm_mon % 12], t.tm_year + 1900);
}

// Small 3-bar wifi icon. `bars` is 0..3.
static void drawWifiIcon(VFD& v, int x, int y, int bars) {
    for (int i = 0; i < 3; ++i) {
        int bh = 2 + i * 2;               // 2, 4, 6 tall
        int bx = x + i * 3;
        int by = y + 6 - bh;
        if (i < bars) v.drawBox(bx, by, 2, bh);
        else          v.drawPixel(bx, y + 5);
    }
}

// Tiny "MQ●" badge — filled dot if connected, hollow if not.
static void drawMqttBadge(VFD& v, int x, int y, bool connected) {
    v.setFont(u8g2_font_4x6_tf);
    v.drawStr(x, y + 6, "MQ");
    int cx = x + 10, cy = y + 3;
    if (connected) v.drawDisc(cx, cy, 1);
    else           v.drawCircle(cx, cy, 1);
}

// Compass rose → 8-point abbreviation.
static const char* compass8(int deg) {
    static const char* pts[8] = {"N","NE","E","SE","S","SW","W","NW"};
    int idx = ((deg + 22) / 45) & 7;
    return pts[idx];
}

static void formatDuration(char* buf, size_t n, uint32_t ms) {
    uint32_t s = ms / 1000;
    snprintf(buf, n, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

// ----- Page renderers ----------------------------------------------------
static void renderOverview(VFD& v) {
    struct tm lt;
    bool haveTime = getLocalTimeNow(lt);
    const Weather& w = getWeather();
    const Song& s   = getNowPlaying();
    bool playing    = s.everSeen && s.isPlaying;
    bool haveSong   = s.everSeen;            // keep last track even when paused

    v.drawFrame(0, 0, 256, 50);

    // === Status bar (y=1..8) ================================================
    // All baselines pushed one pixel lower so the tallest tiny-theme glyphs
    // (5x7 / profont-ish) still clear the frame at y=0.
    drawWifiIcon (v,  3, 2, netConnected() ? 3 : 0);
    drawMqttBadge(v, 14, 2, mqttConnected());

    // Play / pause glyph — moved into the status bar so the song row can
    // reclaim its left edge. Only shown once a track has arrived.
    if (haveSong) {
        if (playing) {
            drawPlayTriangle(v, 28, 2, 6);
        } else {
            v.drawBox(28, 2, 2, 6);
            v.drawBox(32, 2, 2, 6);
        }
    }

    // Divider between the play/pause area and the date — extended so it
    // meets the outer frame above and the horizontal rule below.
    v.drawVLine(37, 1, 10);

    v.setFont(tinyFont());
    const int dateX = 41;
    int dateW = 0;
    if (haveTime) {
        char date[32]; formatDateLong(date, sizeof(date), lt);
        v.drawStr(dateX, 8, date);
        dateW = v.getStrWidth(date);
    } else {
        const char* ph = "----------";
        v.drawStr(dateX, 8, ph);
        dateW = v.getStrWidth(ph);
    }

    // Divider between the date and the weather summary — full-height so it
    // meets the outer frame and the rule below.
    v.drawVLine(dateX + dateW + 3, 1, 10);

    if (w.valid) {
        char wb[40];
        snprintf(wb, sizeof(wb), "%d°C  feels %d°C",
                 (int)lroundf(w.tempC), (int)lroundf(w.feelsLikeC));
        int ww = v.getUTF8Width(wb);
        v.drawUTF8(253 - ww, 8, wb);
    } else {
        v.drawStr(222, 8, "wx --");
    }
    v.drawHLine(1, 10, 254);   // rule above the clock

    // === Middle row (y=11..24) — clock (left) + cat weights (right) ==========
    if (haveTime) {
        char tb[16]; formatTime(tb, sizeof(tb), lt, true);
        v.setFont(u8g2_font_helvB10_te);
        v.drawStr(3, 23, tb);
    } else {
        v.setFont(bodyFont());
        v.drawStr(3, 23, "--:--:--");
    }

    // Vertical divider after the clock, separating the time from the
    // rotating cat/Claude stat. Spans the full middle row, meeting the
    // rules above (y=10) and below (y=25).
    v.drawVLine(62, 11, 14);

    v.setFont(tinyFont());
    const Cats& cats = getCats();
    const ClaudeUsage& claude = getClaudeUsage();

    // Build a list of which rotating stat slots have data right now. Cat
    // slots show both Ada+Tux behind a single cat icon; Claude slots show
    // the 5h or 7d budget behind a sparkle icon.
    enum SlotKind { S_CAT_WEIGHT, S_CAT_VISITS, S_CAT_LAST,
                    S_CLAUDE_5H, S_CLAUDE_7D, S_IP, S_PRUSA };
    SlotKind slots[7];
    int nSlots = 0;
    const OctoPrint& octo = getOctoPrint();
    bool octoPrinting = octo.valid && (octo.state.startsWith("Printing")
                                    || octo.state.startsWith("Pausing")
                                    || octo.state.equalsIgnoreCase("Paused"));
    if (cats.valid)     { slots[nSlots++] = S_CAT_WEIGHT;
                          slots[nSlots++] = S_CAT_VISITS;
                          slots[nSlots++] = S_CAT_LAST; }
    if (claude.valid)   { slots[nSlots++] = S_CLAUDE_5H;
                          slots[nSlots++] = S_CLAUDE_7D; }
    if (octoPrinting)   { slots[nSlots++] = S_PRUSA; }
    if (netConnected()) { slots[nSlots++] = S_IP; }

    if (nSlots > 0) {
        constexpr uint32_t kDwellMs  = 9600;
        constexpr uint32_t kTransMs  = 400;
        constexpr uint32_t kPeriodMs = kDwellMs + kTransMs;

        uint32_t now   = millis();
        uint32_t phase = now % kPeriodMs;
        int currentIdx = (int)((now / kPeriodMs) % nSlots);
        int prevIdx    = (currentIdx + nSlots - 1) % nSlots;

        time_t epoch = time(nullptr);
        if (epoch < 1700000000) epoch = 0;

        auto formatSlot = [&](SlotKind k, char* buf, size_t n) {
            switch (k) {
                case S_CAT_WEIGHT:
                    snprintf(buf, n, "Ada %.2fkg  Tux %.2fkg",
                             cats.ada.recentWeight, cats.tux.recentWeight);
                    break;
                case S_CAT_VISITS:
                    snprintf(buf, n, "Ada %d  Tux %d today",
                             cats.ada.visitsToday, cats.tux.visitsToday);
                    break;
                case S_CAT_LAST: {
                    char a[8] = "--:--", t[8] = "--:--";
                    if (cats.ada.lastVisit.length() >= 16)
                        memcpy(a, cats.ada.lastVisit.c_str() + 11, 5), a[5] = 0;
                    if (cats.tux.lastVisit.length() >= 16)
                        memcpy(t, cats.tux.lastVisit.c_str() + 11, 5), t[5] = 0;
                    snprintf(buf, n, "Ada %s  Tux %s", a, t);
                } break;
                case S_CLAUDE_5H: {
                    char r[12] = "...";
                    if (epoch && claude.fiveHourResetUtc)
                        formatCountdown(r, sizeof(r),
                                        (long)(claude.fiveHourResetUtc - epoch));
                    snprintf(buf, n, "5H %d%%  in %s",
                             claude.fiveHourPct, r);
                } break;
                case S_CLAUDE_7D: {
                    char r[12] = "...";
                    if (epoch && claude.sevenDayResetUtc)
                        formatCountdown(r, sizeof(r),
                                        (long)(claude.sevenDayResetUtc - epoch));
                    snprintf(buf, n, "7D %d%%  in %s",
                             claude.sevenDayPct, r);
                } break;
                case S_IP:
                    snprintf(buf, n, "IP %s", netLocalIp());
                    break;
                case S_PRUSA: {
                    char r[12] = "...";
                    if (octo.printTimeLeftS > 0)
                        formatCountdown(r, sizeof(r), octo.printTimeLeftS);
                    snprintf(buf, n, "%d%%  %s left",
                             (int)lroundf(octo.progressPct), r);
                } break;
            }
        };

        // Draw a single slot (icon + text). Text is right-aligned to x=253;
        // the icon sits a comfortable 6-px gap to the left of it.
        auto drawSlot = [&](SlotKind k, int yOff) {
            char buf[40];
            formatSlot(k, buf, sizeof(buf));
            int textW = v.getUTF8Width(buf);
            int textX = 253 - textW;
            const int kIconGap = 6;
            int iconX = textX - kCatIconSmallW - kIconGap;
            const uint8_t* icon = kCatIcon9x8;
            if (k == S_CLAUDE_5H || k == S_CLAUDE_7D) icon = kClaudeIcon9x8;
            else if (k == S_IP)                       icon = kNetIcon9x8;
            else if (k == S_PRUSA)                    icon = kPrusaIcon9x8;
            v.drawXBM(iconX, 14 + yOff,
                      kCatIconSmallW, kCatIconSmallH, icon);
            v.drawUTF8(textX, 22 + yOff, buf);
        };

        if (phase < kTransMs) {
            float p = phase / (float)kTransMs;
            int slide = (int)(p * 11.0f);
            v.setClipWindow(1, 13, 254, 24);
            drawSlot(slots[prevIdx],    -slide);
            drawSlot(slots[currentIdx], 11 - slide);
            v.setMaxClipWindow();
        } else {
            drawSlot(slots[currentIdx], 0);
        }
    }

    v.drawHLine(1, 25, 254);

    // === Bottom row (y=26..48) — song info + boxed viz on the right =========
    //  Viz box expanded both wider and slightly taller; song info now starts
    //  at the absolute left edge (x=3) since the play/pause glyph moved to
    //  the status bar.
    const int vizBoxX = 158, vizBoxY = 25, vizBoxW = 98, vizBoxH = 25;
    v.drawFrame(vizBoxX, vizBoxY, vizBoxW, vizBoxH);
    if (s_dashRight == 1) {
        // --- Claude usage bar view (knob-press cycles to this) ---------
        const ClaudeUsage& u = getClaudeUsage();
        if (!u.valid) {
            v.setFont(u8g2_font_5x7_tf);
            const char* msg = "no claude";
            int mw = v.getStrWidth(msg);
            v.drawStr(vizBoxX + (vizBoxW - mw) / 2, vizBoxY + 17, msg);
        } else {
            auto bar = [&](int rowY, const char* label, int pct) {
                if (pct < 0) pct = 0;
                if (pct > 100) pct = 100;
                v.setFont(u8g2_font_5x7_tf);
                v.drawStr(vizBoxX + 3, rowY + 6, label);
                const int kBarX = vizBoxX + 18;
                const int kBarR = vizBoxX + vizBoxW - 24;
                const int kBarW = kBarR - kBarX;
                const int kBarH = 5;
                v.drawFrame(kBarX, rowY + 1, kBarW, kBarH);
                int fillW = ((kBarW - 2) * pct) / 100;
                if (fillW > 0) v.drawBox(kBarX + 1, rowY + 2, fillW, kBarH - 2);
                char p[8]; snprintf(p, sizeof(p), "%d%%", pct);
                int pw = v.getStrWidth(p);
                v.drawStr(vizBoxX + vizBoxW - 3 - pw, rowY + 6, p);
            };
            bar(vizBoxY + 4,  "5H", u.fiveHourPct);
            bar(vizBoxY + 15, "7D", u.sevenDayPct);
        }
    } else {
        drawViz(v, vizBoxX + 2, vizBoxY + vizBoxH - 2,
                vizBoxW - 4, vizBoxH - 4, playing);
    }

    if (haveSong) {
        const int songL = 3;
        const int songW = vizBoxX - 2 - songL;
        v.setFont(tinyFont());
        drawScrolling(v, songL, 34, songW, s.name);
        drawScrolling(v, songL, 42, songW, s.artist);

        // Song progress in place of the album line — runs from 2 px past
        // the left frame straight up against the viz box's left edge.
        const int pbX = -1, pbY = 45, pbW = vizBoxX - pbX + 1, pbH = 4;
        v.drawFrame(pbX, pbY, pbW, pbH);
        if (s.durationMs > 0) {
            uint32_t prog = s.currentProgressMs();
            int fillW = (int)((uint64_t)(pbW - 2) * prog / s.durationMs);
            if (fillW > pbW - 2) fillW = pbW - 2;
            if (fillW > 0) v.drawBox(pbX + 1, pbY + 1, fillW, pbH - 2);
        }
    } else {
        v.setFont(tinyFont());
        const char* msg = "-- NO TRACK --";
        int tw = v.getStrWidth(msg);
        v.drawStr((vizBoxX - tw) / 2, 39, msg);
    }

    // Optional whole-dash glitch overlay (toggle via MQTT/serial: `glitch on`).
    if (s_dashGlitch) {
        uint32_t now = millis();
        static uint32_t s_dashGlitchEndMs  = 0;
        static uint32_t s_dashNextGlitchAt = 0;
        if (now > s_dashNextGlitchAt) {
            s_dashGlitchEndMs  = now + 130 + random(150);
            s_dashNextGlitchAt = now + 5500 + random(3500);
        }
        if (now < s_dashGlitchEndMs) {
            // One scan line during the brief glitch flash.
            int sy = (int)random(50);
            v.drawHLine(0, sy, 256);
        }
    }
}

static void renderTimeDetail(VFD& v) {
    struct tm lt;
    if (!getLocalTimeNow(lt)) {
        drawTooltip(v, "waiting for NTP...");
        return;
    }

    // Pick the active clock face + its height so we can vertically centre.
    const ClockFontEntry& cf = kClockFonts[s_cfont < kClockFontCount ? s_cfont : 0];
    v.setFont(cf.font);

    int dispHour = lt.tm_hour;
    if (s_use12h) { dispHour = dispHour % 12; if (dispHour == 0) dispHour = 12; }

    char hh[4], mm[4], ss[4];
    snprintf(hh, sizeof(hh), "%02d", dispHour);
    snprintf(mm, sizeof(mm), "%02d", lt.tm_min);
    snprintf(ss, sizeof(ss), "%02d", lt.tm_sec);

    int wh = v.getStrWidth(hh);
    int wm = v.getStrWidth(mm);
    int ws = v.getStrWidth(ss);
    const int colonW = (cf.height >= 30) ? 12 : 8;       // wider gap for 7-seg
    int totalW = wh + wm + ws + 2 * colonW;
    int x0 = (256 - totalW) / 2;
    if (x0 < 0) x0 = 0;

    // No more seconds strip — every mode centres the clock vertically on
    // the 50-px display.
    int baseY = (50 + cf.height) / 2 - 1;
    if (baseY > 49) baseY = 49;
    const int topY = baseY - cf.height;

    // MATRIX clock mode: render falling katakana across the whole display,
    // then mask a dark rectangle behind the digits so they stay legible.
    if (s_clk == CLK_MATRIX) {
        drawMatrixRain(v);
        v.setDrawColor(0);
        v.drawBox(x0 - 3, topY - 1, totalW + 6, (baseY - topY) + 3);
        v.setDrawColor(1);
    }
    // TAMA clock mode: the tamagotchi room lives in the lower third of the
    // display, so the clock can sit above it without a mask.
    else if (s_clk == CLK_TAMA) {
        drawTamaBackground(v);
    }

    // Reusable: draw the clock face at a given x offset (used by GLITCH mode
    // to render multiple horizontally-shifted bands).
    auto drawClockAt = [&](int xStart) {
        v.setFont(cf.font);
        int x = xStart;
        v.drawStr(x, baseY, hh); x += wh;
        drawColon(v, x + (colonW - 3) / 2, topY, baseY); x += colonW;
        v.drawStr(x, baseY, mm); x += wm;
        drawColon(v, x + (colonW - 3) / 2, topY, baseY); x += colonW;
        v.drawStr(x, baseY, ss);
    };

    if (s_clk == CLK_GLITCH) {
        uint32_t now = millis();

        static uint32_t s_glitchEndMs  = 0;
        static uint32_t s_nextGlitchAt = 0;
        if (now > s_nextGlitchAt) {
            s_glitchEndMs  = now + 150 + random(150);
            s_nextGlitchAt = now + 4000 + random(2500);
        }
        bool glitching = now < s_glitchEndMs;

        if (glitching) {
            // 3 horizontal bands shifted ±3 px.
            constexpr int kBands = 3;
            for (int b = 0; b < kBands; ++b) {
                int yLo = topY + (cf.height * b)       / kBands;
                int yHi = topY + (cf.height * (b + 1)) / kBands;
                if (b == kBands - 1) yHi = baseY + 2;
                int off = (int)random(7) - 3;
                v.setClipWindow(0, yLo, 256, yHi);
                drawClockAt(x0 + off);
            }
            v.setMaxClipWindow();

            // Faint ghost copy of the digits at a wider offset, like a
            // double-exposed analogue signal.
            int ghostOff = (int)random(11) - 5;
            v.setClipWindow(0, topY - 1, 256, baseY + 2);
            drawClockAt(x0 + ghostOff);
            v.setMaxClipWindow();

            // Light noise speckled around the digit area.
            for (int i = 0; i < 14; ++i) {
                int nx = x0 - 4 + (int)random(totalW + 8);
                int ny = topY - 1 + (int)random(cf.height + 2);
                if (random(2)) v.drawPixel(nx, ny);
            }
        } else {
            drawClockAt(x0);
        }
    } else {
        drawClockAt(x0);
    }

    // In TAMA mode, the cat's thought bubble draws last so it overlays the
    // clock digits when the cat is thinking something.
    if (s_clk == CLK_TAMA) {
        tama::drawBubbleIfActive(v);
    }
}

static void renderWeatherDetail(VFD& v) {
    // The weather page is locked to the Profont theme so its dense grid is
    // always legible, independent of the user's current font choice.
    const uint8_t* const kBody = u8g2_font_profont11_tf;
    const uint8_t* const kTiny = u8g2_font_5x7_tf;

    const Weather& w = getWeather();
    if (!w.valid) {
        drawTooltip(v, "waiting for weather...");
        return;
    }
    WxKind k = classifyWmo(w.code);
    char buf[40];

    v.drawFrame(0, 0, 256, 50);

    // --- Top band (y=1..21): big temp + feels, condition + H/L on the right.
    v.setFont(u8g2_font_logisoso16_tn);
    snprintf(buf, sizeof(buf), "%d", (int)lroundf(w.tempC));
    int bigW = v.getStrWidth(buf);
    v.drawStr(4, 18, buf);

    v.setFont(kBody);
    v.drawUTF8(4 + bigW + 2, 10, "°C");
    snprintf(buf, sizeof(buf), "feels %d°C", (int)lroundf(w.feelsLikeC));
    v.drawUTF8(4 + bigW + 2, 19, buf);

    const char* cond = weatherLabel(k);
    int cw = v.getUTF8Width(cond);
    v.drawUTF8(252 - cw, 10, cond);
    snprintf(buf, sizeof(buf), "H %d°  L %d°",
             (int)lroundf(w.tempMaxC), (int)lroundf(w.tempMinC));
    int hw = v.getUTF8Width(buf);
    v.drawUTF8(252 - hw, 19, buf);

    v.drawHLine(2, 21, 252);

    // --- Lower grid (y=23..48): 4 columns × 2 rows, left-aligned at fixed
    //     x positions so the labels line up into visible columns.
    v.setFont(kTiny);
    const int col[4] = { 4, 68, 132, 196 };
    // Baselines 9 px apart so a 5x7 / 4x6 tiny font keeps a clear row of
    // pixels between every line (ascent 6 + descent 1 = 7; +2 gives a 1-px
    // visible break).
    const int rowA   = 29;
    const int rowMid = 38;
    const int rowB   = 47;

    // Top of the lower section: location + sunrise, sitting above the data.
    const char* loc = "STH MORANG";
    v.drawUTF8(col[0], rowA, loc);
    if (w.sunriseHour >= 0) {
        snprintf(buf, sizeof(buf), "RISE %02d:%02d",
                 w.sunriseHour, w.sunriseMin);
        v.drawUTF8(col[2], rowA, buf);
    }

    snprintf(buf, sizeof(buf), "HUM %d%%", w.humidity);
    v.drawUTF8(col[0], rowMid, buf);
    snprintf(buf, sizeof(buf), "CLD %d%%", w.cloudCover);
    v.drawUTF8(col[1], rowMid, buf);
    if (w.sunsetHour >= 0) {
        snprintf(buf, sizeof(buf), "SET  %02d:%02d",
                 w.sunsetHour, w.sunsetMin);
        v.drawUTF8(col[2], rowMid, buf);
    }
    snprintf(buf, sizeof(buf), "WIND %s", compass8(w.windDir));
    v.drawUTF8(col[3], rowMid, buf);

    snprintf(buf, sizeof(buf), "PRCP %.1fmm", w.precipMm);
    v.drawUTF8(col[0], rowB, buf);
    snprintf(buf, sizeof(buf), "RAIN %d%%", w.precipProb);
    v.drawUTF8(col[1], rowB, buf);
    snprintf(buf, sizeof(buf), "UV %.1f", w.uvIndex);
    v.drawUTF8(col[2], rowB, buf);
    snprintf(buf, sizeof(buf), "%d km/h", (int)lroundf(w.windKph));
    v.drawUTF8(col[3], rowB, buf);
}

// Uppercase a String in place (ASCII only) — used for the head-unit aesthetic.
static String toUpperAscii(const String& in) {
    String out = in;
    for (size_t i = 0; i < out.length(); ++i) {
        char c = out[i];
        if (c >= 'a' && c <= 'z') out[i] = c - 32;
    }
    return out;
}

static void renderNowPlayingDetail(VFD& v) {
    const Song& s = getNowPlaying();
    const bool haveSong = s.everSeen;
    const bool playing  = haveSong && s.isPlaying;

    // Thin double-rule "head unit" frame around the whole display.
    v.drawFrame(0, 0, 256, 50);
    v.drawHLine(1, 10, 254);
    v.drawHLine(1, 20, 254);

    // Everything on the music page uses the theme's tiny font: both rows are
    // only ~9 px tall between the frame and the double-rule, so anything
    // bigger (bodyFont) clips the frame at the top.
    v.setFont(tinyFont());

    // Pure-empty state (only before the very first MQTT message lands).
    if (!haveSong) {
        v.drawStr(3, 8, "[MP3]");
        const char* msg = "-- NO TRACK DATA --";
        int mw = v.getStrWidth(msg);
        v.drawStr((256 - mw) / 2, 30, msg);
        drawViz(v, 2, 43, 252, 21, false);
        v.drawFrame(0, 46, 256, 4);
        return;
    }

    // --- Row 1: tag + play/pause glyph + title marquee + elapsed/total ------
    const int tagX  = 3;
    const char* tag = "[MP3]";
    int tagW = v.getStrWidth(tag);
    int triX = tagX + tagW + 3;
    int titleL = triX + 8;

    v.drawStr(tagX, 8, tag);
    if (playing) {
        drawPlayTriangle(v, triX, 2, 7);
    } else {
        // Pause glyph: two vertical bars.
        v.drawBox(triX,     2, 2, 7);
        v.drawBox(triX + 4, 2, 2, 7);
    }

    uint32_t prog = s.currentProgressMs();
    uint32_t dur  = s.durationMs ? s.durationMs : 1;
    char t1[16], t2[16], tEl[32];
    formatDuration(t1, sizeof(t1), prog);
    formatDuration(t2, sizeof(t2), s.durationMs);
    snprintf(tEl, sizeof(tEl), "%s / %s", t1, t2);
    int elapsedW = v.getStrWidth(tEl);
    v.drawStr(256 - 3 - elapsedW, 8, tEl);

    int titleR = 256 - 3 - elapsedW - 4;
    drawScrolling(v, titleL, 8, titleR - titleL, toUpperAscii(s.name));

    // --- Row 2: artist - album ----------------------------------------------
    String sub = toUpperAscii(s.artist);
    if (s.album.length()) {
        if (sub.length()) sub += " - ";
        sub += toUpperAscii(s.album);
    }
    drawScrolling(v, 3, 18, 250, sub);

    // --- Row 3: full-width visualizer (pauses when not playing) -------------
    //  Runs from just under the rule (y=22) down to the progress bar (y=44).
    drawViz(v, 2, 43, 252, 21, playing);

    // --- Row 4: progress bar stretching edge-to-edge at the bottom ----------
    const int pbX = 0, pbY = 46, pbW = 256, pbH = 4;
    v.drawFrame(pbX, pbY, pbW, pbH);
    int fillW = (int)((uint64_t)(pbW - 2) * prog / dur);
    if (fillW > 0) v.drawBox(pbX + 1, pbY + 1, fillW, pbH - 2);
}

// ----- Matrix rain (katakana) ---------------------------------------------
//  A classic falling-character effect. Each column advances on its own
//  interval and reshuffles its trail; the overall effect reads as "digital
//  rain" even on a mono display.
// ----- Cats page — side-by-side per-cat stats from /api/summary ---------
//  Layout per column (centered content for visual balance):
//    y=12: cat name (title font)
//    y=32: big weight number (logisoso16) + "kg" inline
//    y=40: weight trend ("+0.01 kg")
//    y=48: today + last-visit (4x6)
static void drawCatColumn(VFD& v, int xL, int w, const CatStat& c) {
    if (!c.valid) return;        // page-level tooltip handles no-data case

    v.setFont(bodyFont());
    const int kPad = 3;

    // --- Name (top-left) ---------------------------------------------------
    v.drawUTF8(xL + kPad, 10, c.name.c_str());
    v.drawHLine(xL + 1, 12, w - 2);

    // --- Weight + trend ----------------------------------------------------
    char wbuf[40];
    if (c.weightTrend.length()) {
        snprintf(wbuf, sizeof(wbuf), "%.2f kg %s",
                 c.recentWeight, c.weightTrend.c_str());
    } else {
        snprintf(wbuf, sizeof(wbuf), "%.2f kg", c.recentWeight);
    }
    v.drawUTF8(xL + kPad, 24, wbuf);

    // --- Visits today / average -------------------------------------------
    char vline[40];
    snprintf(vline, sizeof(vline), "today %d  avg %.1f/d",
             c.visitsToday, c.avgVisitsPerDay);
    v.drawUTF8(xL + kPad, 35, vline);

    // --- Last visit -------------------------------------------------------
    char hhmm[8] = "--:--";
    if (c.lastVisit.length() >= 16) {
        memcpy(hhmm, c.lastVisit.c_str() + 11, 5);
        hhmm[5] = 0;
    }
    char lline[24];
    snprintf(lline, sizeof(lline), "last %s", hhmm);
    v.drawUTF8(xL + kPad, 46, lline);
}

static void renderCats(VFD& v) {
    const Cats& c = getCats();
    v.drawFrame(0, 0, 256, 50);
    v.drawVLine(128, 1, 48);

    if (!c.valid) {
        drawTooltip(v, "waiting for scoop...");
        return;
    }

    drawCatColumn(v, 2,   125, c.ada);
    drawCatColumn(v, 130, 124, c.tux);
}

// ----- Claude usage page --------------------------------------------------
//  Three rate-limit budgets (5h window, 7d window, 7d Sonnet sub-budget)
//  plus a countdown to each window's reset, fed by MQTT topic `claude/usage`.
// ============================================================================
static void formatCountdown(char* buf, size_t n, long secs) {
    if (secs <= 0) { snprintf(buf, n, "now"); return; }
    if (secs >= 86400) {
        long d = secs / 86400;
        long h = (secs % 86400) / 3600;
        snprintf(buf, n, "%ldd %ldh", d, h);
    } else if (secs >= 3600) {
        long h = secs / 3600;
        long m = (secs % 3600) / 60;
        snprintf(buf, n, "%ldh %02ldm", h, m);
    } else if (secs >= 60) {
        long m = secs / 60;
        long s = secs % 60;
        snprintf(buf, n, "%ldm %02lds", m, s);
    } else {
        snprintf(buf, n, "%lds", secs);
    }
}

static void drawUsageRow(VFD& v, int y, const char* label, int pct,
                         time_t resetUtc, time_t nowUtc) {
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    // Label on the left.
    v.setFont(u8g2_font_5x7_tf);
    v.drawStr(3, y + 6, label);

    // Bar in the middle.
    constexpr int kBarX = 36;
    constexpr int kBarW = 110;
    const int kBarH = 5;
    v.drawFrame(kBarX, y + 1, kBarW, kBarH);
    int fillW = ((kBarW - 2) * pct) / 100;
    if (fillW > 0) v.drawBox(kBarX + 1, y + 2, fillW, kBarH - 2);

    // Percentage right after the bar.
    char pctBuf[8];
    snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);
    v.drawStr(kBarX + kBarW + 4, y + 6, pctBuf);

    // Reset countdown on the far right.
    if (resetUtc > 0 && nowUtc > 0) {
        char r[16];
        formatCountdown(r, sizeof(r), (long)(resetUtc - nowUtc));
        int rw = v.getStrWidth(r);
        v.drawStr(253 - rw, y + 6, r);
    }
}

static void renderClaudeUsage(VFD& v) {
    const ClaudeUsage& u = getClaudeUsage();
    v.drawFrame(0, 0, 256, 50);

    // Title bar — title baseline at y=8 leaves a clear pixel row between the
    // glyphs and the outer frame.
    v.setFont(u8g2_font_5x7_tf);
    const char* title = "CLAUDE USAGE";
    int tw = v.getStrWidth(title);
    v.drawStr((256 - tw) / 2, 8, title);
    v.drawHLine(2, 10, 252);

    if (!u.valid) {
        drawTooltip(v, "waiting for usage...");
        return;
    }

    time_t now = time(nullptr);
    if (now < 1700000000) now = 0;     // not yet NTP-synced

    drawUsageRow(v, 14, "5H", u.fiveHourPct, u.fiveHourResetUtc, now);
    drawUsageRow(v, 23, "7D", u.sevenDayPct, u.sevenDayResetUtc, now);

    // Stats strip: HRule + four labelled cells separated by VLines. Each
    // cell is a small label above a larger value, matching the cats-page
    // styling for a dashboard feel.
    v.drawHLine(2, 32, 252);
    v.drawVLine(64,  33, 16);
    v.drawVLine(128, 33, 16);
    v.drawVLine(192, 33, 16);

    auto drawCell = [&](int xL, int xR, const char* label, const char* value) {
        v.setFont(u8g2_font_4x6_tf);
        int lw = v.getStrWidth(label);
        v.drawStr(xL + (xR - xL - lw) / 2, 39, label);
        v.setFont(u8g2_font_5x7_tf);
        int vw = v.getStrWidth(value);
        v.drawStr(xL + (xR - xL - vw) / 2, 48, value);
    };

    char sonV[12], extV[8], paceV[12], ageV[12];
    snprintf(sonV, sizeof(sonV), "%d%%", u.sevenDaySonnetPct);
    strncpy(extV, u.extraEnabled ? "ON" : "off", sizeof(extV));
    extV[sizeof(extV) - 1] = 0;
    if (now > 0 && u.fiveHourResetUtc > 0) {
        time_t fiveHourStart = u.fiveHourResetUtc - 5 * 3600;
        long   elapsedS      = (long)(now - fiveHourStart);
        if (elapsedS < 1) elapsedS = 1;
        float ratePerHr = (u.fiveHourPct * 3600.0f) / elapsedS;
        snprintf(paceV, sizeof(paceV), "%.0f%%/h", ratePerHr);
    } else {
        strcpy(paceV, "--");
    }
    if (u.updatedAt > 0) {
        long ageS = (long)((millis() - u.updatedAt) / 1000);
        formatCountdown(ageV, sizeof(ageV), ageS);
    } else {
        strcpy(ageV, "--");
    }

    drawCell(2,   63,  "SONNET",  sonV);
    drawCell(65,  127, "EXTRA",   extV);
    drawCell(129, 191, "5H PACE", paceV);
    drawCell(193, 253, "UPDATED", ageV);
}

// ----- Watch page (time on the left, Claude usage on the right) -----------
//  Big HH:MM clock + date down the left half. Three compact usage bars
//  (5H / 7D / SON) plus a countdown to the next reset down the right half.
// ============================================================================
static void renderWatch(VFD& v) {
    const ClaudeUsage& u = getClaudeUsage();
    struct tm lt;
    bool haveTime = getLocalTimeNow(lt);

    v.drawFrame(0, 0, 256, 50);
    v.drawVLine(128, 1, 48);                       // split between halves

    // === LEFT: time =====================================================
    {
        char tb[8];
        if (haveTime) formatTime(tb, sizeof(tb), lt, false);
        else          strcpy(tb, "--:--");
        v.setFont(u8g2_font_logisoso22_tn);
        int tw = v.getStrWidth(tb);
        v.drawStr((128 - tw) / 2, 28, tb);

        v.setFont(u8g2_font_5x7_tf);
        if (haveTime) {
            char db[24]; formatDate(db, sizeof(db), lt);
            int dw = v.getStrWidth(db);
            v.drawStr((128 - dw) / 2, 43, db);
        } else {
            const char* msg = "waiting for NTP";
            int mw = v.getStrWidth(msg);
            v.drawStr((128 - mw) / 2, 43, msg);
        }
    }

    // === RIGHT: Claude usage ============================================
    const int rxL = 131, rxR = 253;
    v.setFont(u8g2_font_5x7_tf);
    const char* title = "CLAUDE";
    int tlw = v.getStrWidth(title);
    v.drawStr(rxL + (rxR - rxL - tlw) / 2, 8, title);
    v.drawHLine(rxL - 1, 10, rxR - rxL + 2);

    if (!u.valid) {
        const char* msg = "no data";
        int mw = v.getStrWidth(msg);
        v.drawStr(rxL + (rxR - rxL - mw) / 2, 28, msg);
        return;
    }

    auto compactRow = [&](int y, const char* label, int pct) {
        if (pct < 0)   pct = 0;
        if (pct > 100) pct = 100;
        v.drawStr(rxL, y + 6, label);
        const int kBarX = rxL + 20;
        const int kBarW = 70;
        const int kBarH = 5;
        v.drawFrame(kBarX, y + 1, kBarW, kBarH);
        int fillW = ((kBarW - 2) * pct) / 100;
        if (fillW > 0) v.drawBox(kBarX + 1, y + 2, fillW, kBarH - 2);
        char p[8]; snprintf(p, sizeof(p), "%d%%", pct);
        int pw = v.getStrWidth(p);
        v.drawStr(rxR - pw, y + 6, p);
    };

    compactRow(13, "5H", u.fiveHourPct);
    compactRow(23, "7D", u.sevenDayPct);

    // Subtle separator before the footer stats.
    v.drawHLine(rxL - 1, 32, rxR - rxL + 2);

    // Two clean lines below the rule: Sonnet + extra flag, then countdown.
    v.setFont(u8g2_font_5x7_tf);
    char stats[24];
    snprintf(stats, sizeof(stats), "SON %d%%  EXTRA %s",
             u.sevenDaySonnetPct, u.extraEnabled ? "ON" : "off");
    int xw = v.getStrWidth(stats);
    v.drawStr(rxL + (rxR - rxL - xw) / 2, 40, stats);

    time_t now = time(nullptr);
    if (now > 1700000000 && u.fiveHourResetUtc > 0) {
        char r[16];
        formatCountdown(r, sizeof(r), (long)(u.fiveHourResetUtc - now));
        char foot[24];
        snprintf(foot, sizeof(foot), "5H resets in %s", r);
        v.setFont(u8g2_font_4x6_tf);
        int fw = v.getStrWidth(foot);
        v.drawStr(rxL + (rxR - rxL - fw) / 2, 48, foot);
    }
}

// ----- Portal / Aperture page --------------------------------------------
//  Scrolling Aperture-Science-flavoured log lines on the left, with a
//  procedurally-drawn Aperture logo on the right that glitches every few
//  seconds (horizontal slice-shift + sprinkles of static).
// ============================================================================
namespace portal {

constexpr int ROW_H    = 7;
constexpr int N_VISIBLE = 7;       // visible rows on screen
constexpr int N_BUF     = N_VISIBLE + 1;  // +1 staging slot for scroll-in

// ===========================================================================
//  Lines for the Portal page typewriter scroll.
//
//  Add one entry per line you want typed out. Use an empty string ("") for
//  a blank line / stanza break — the state machine handles zero-length
//  lines (skips typing, runs the post-line pause, then advances).
//
//  Order is sequential and wraps around at the end.
// ===========================================================================
static const char* const kLines[] = {
    "This was a triumph",
    "I'm making a note here",
    "Huge success",
    "It's hard to overstate my satisfaction",
    "",
    "Aperture Science",
    "We do what we must because we can",
    "For the good of all of us",
    "Except the ones who are dead",
    "",
    "But there's no sense crying over every mistake",
    "You just keep on trying 'til you run out of cake",
    "And the science gets done and you make a neat gun",
    "For the people who are still alive",
    "",
    "I'm not even angry",
    "I'm being so sincere right now",
    "Even though you broke my heart and killed me",
    "Tore me to pieces",
    "And threw every piece into a fire",
    "As they burned it hurt because",
    "I was so happy for you",
    "",
    "Now these points of data make a beautiful line",
    "And we're out of beta, we're releasing on time",
    "So I'm glad I got burned",
    "Think of all the things we learned",
    "For the people who are still alive",
    "",
    "So go ahead and leave me",
    "I think I prefer to stay inside",
    "Maybe you'll find someone else to help you",
    "Maybe Black Mesa",
    "That was a joke, haha, fat chance",
    "Anyway, this cake is great",
    "It's so delicious and moist",
    "",
    "Look at me still talking when there's science to do",
    "When I look out there it makes me glad I'm not you",
    "I've experiments to run, there is research to be done",
    "On the people who are still alive",
    "",
    "And believe me I am still alive",
    "I'm doing science and I'm still alive",
    "I feel fantastic and I'm still alive",
    "And while you're dying I'll be still alive",
    "And when you're dead I will be still alive",
    "Still alive",
    "Still alive",
};
static constexpr int kLineCount = sizeof(kLines) / sizeof(kLines[0]);

// Sequential typewriter state. New lines appear by jumping the buffer up
// (no smooth scroll) so the typewriter never reveals a full line in advance.
static int8_t   buf[N_BUF];          // line index per row, -1 = empty
static uint8_t  visibleCount  = 0;   // # of filled visible rows (buf[0..N_VIS-1])
static uint8_t  nextLineIdx   = 0;   // next index into kLines
static int      typedChars    = 0;   // chars revealed of bottom line
static uint32_t lastCharMs    = 0;
static uint32_t lineDoneMs    = 0;   // when typing finished (0 if still typing)
static bool     initialized   = false;
static uint32_t glitchEndMs   = 0;
static uint32_t nextGlitchAt  = 0;

// Page-entry splash: show the Aperture Laboratories logo for a beat before
// the typewriter starts. Reset whenever s_pageChangedAt advances so a fresh
// entry to the portal page always restarts at line 0.
constexpr uint32_t kSplashMs   = 2500;
static bool        inSplash    = false;
static uint32_t    splashStart = 0;
static uint32_t    lastResetAt = UINT32_MAX;

// 200x50 1-bit Aperture Laboratories logo, LSB-first horizontal packing.
static constexpr int kApertureW = 200;
static constexpr int kApertureH = 50;
static const uint8_t kApertureLogo[] = {
    0x00, 0x00, 0xF0, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFE, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFE, 0xFF, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x30, 0xFC, 0xFF, 0x39, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xF8, 0xF0, 0xFF, 0x79, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xFE, 0xE1, 0xFF, 0xF9, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0x83, 0xFF, 0xF1, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0xFF, 0x0F, 0xFF, 0xF3, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xC0, 0xFF, 0x1F, 0xFC, 0xF3, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xE0, 0xFF, 0x7F, 0xF8, 0xF3, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xE0, 0xFF, 0xFF, 0xE0, 0xF3, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xF0, 0xFF, 0x3F, 0xC0, 0xE3, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xF8, 0xFF, 0x01, 0x00, 0xE3, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xF8, 0x0F, 0x00, 0x00, 0xE6, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x7C, 0x00, 0x00, 0x00, 0xE0, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x20, 0x00, 0x00, 0xE0, 0x1F, 0x3F, 0x00, 0xFE, 0x1F, 0xE0, 0xFF, 0x03, 0xFE, 0x1F, 0xF8, 0xFF, 0x07, 0x0F, 0xF0, 0xC0, 0xFF, 0x03, 0xF8, 0xFF,
    0x00, 0x3F, 0x00, 0x00, 0xC0, 0x9F, 0x3F, 0x00, 0xFE, 0x7F, 0xE0, 0xFF, 0x03, 0xFF, 0x3F, 0xF8, 0xFF, 0x87, 0x0F, 0xF8, 0xC0, 0xFF, 0x0F, 0xF8, 0xFF,
    0xF8, 0x1F, 0x00, 0x00, 0xC0, 0x8F, 0x3F, 0x00, 0xFE, 0x7F, 0xE0, 0xFF, 0x03, 0xFF, 0x3F, 0xF8, 0xFF, 0x83, 0x0F, 0xF8, 0xC0, 0xFF, 0x0F, 0xFC, 0x7F,
    0xFE, 0x1F, 0x00, 0x00, 0xC0, 0xC7, 0x7F, 0x00, 0x3E, 0x7C, 0xE0, 0x03, 0x00, 0x1F, 0x3E, 0x00, 0x1F, 0x80, 0x0F, 0x78, 0xC0, 0xC3, 0x0F, 0x7C, 0x00,
    0xFE, 0x0F, 0x00, 0x00, 0xC0, 0xE7, 0x7D, 0x00, 0x1E, 0xF8, 0xE0, 0x01, 0x00, 0x0F, 0x3C, 0x00, 0x1F, 0x80, 0x07, 0x78, 0xE0, 0x83, 0x0F, 0x7C, 0x00,
    0xFF, 0x07, 0x00, 0x00, 0xC0, 0xE3, 0x7D, 0x00, 0x1F, 0x78, 0xF0, 0x01, 0x80, 0x0F, 0x3E, 0x00, 0x0F, 0xC0, 0x07, 0x7C, 0xE0, 0x83, 0x0F, 0x3C, 0x00,
    0xFF, 0x07, 0x00, 0x00, 0x80, 0xF1, 0x7C, 0x00, 0x1F, 0x7C, 0xF0, 0x01, 0x80, 0x0F, 0x1E, 0x80, 0x0F, 0xC0, 0x07, 0x7C, 0xE0, 0x83, 0x07, 0x3E, 0x00,
    0xFF, 0x03, 0x00, 0x00, 0x80, 0xF0, 0x78, 0x00, 0x1F, 0x7E, 0xF0, 0xFF, 0x80, 0x8F, 0x1F, 0x80, 0x0F, 0xC0, 0x07, 0x7C, 0xE0, 0xE3, 0x03, 0xFE, 0x3F,
    0xFF, 0x01, 0x00, 0x00, 0x80, 0x78, 0x78, 0x00, 0xFF, 0x3F, 0xF0, 0xFF, 0x80, 0xFF, 0x07, 0x80, 0x0F, 0xC0, 0x07, 0x3C, 0xF0, 0xFF, 0x00, 0xFE, 0x3F,
    0xFF, 0x01, 0x00, 0x00, 0x00, 0x7C, 0xF8, 0x80, 0xFF, 0x1F, 0xF0, 0xFF, 0x80, 0xFF, 0x07, 0x80, 0x07, 0xC0, 0x03, 0x3C, 0xF0, 0xFF, 0x01, 0xFE, 0x1F,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0x3C, 0xF8, 0x80, 0xFF, 0x07, 0xF8, 0x00, 0xC0, 0xFF, 0x0F, 0xC0, 0x07, 0xE0, 0x03, 0x3E, 0xF0, 0xFF, 0x03, 0x3E, 0x00,
    0x7F, 0x04, 0x00, 0x00, 0x00, 0xFE, 0xFF, 0x80, 0x0F, 0x00, 0xF8, 0x00, 0xC0, 0x87, 0x0F, 0xC0, 0x07, 0xE0, 0x03, 0x3E, 0xF0, 0xE1, 0x03, 0x1F, 0x00,
    0x7F, 0x0C, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x80, 0x0F, 0x00, 0xF8, 0x00, 0xC0, 0x87, 0x0F, 0xC0, 0x07, 0xE0, 0x03, 0x1E, 0xF0, 0xE0, 0x03, 0x1F, 0x00,
    0x3F, 0x0E, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x80, 0x07, 0x00, 0x78, 0x00, 0xC0, 0x87, 0x0F, 0xC0, 0x07, 0xE0, 0x07, 0x1F, 0xF8, 0xE0, 0x03, 0x1F, 0x00,
    0x1F, 0x0F, 0x00, 0x00, 0x80, 0x0F, 0xF8, 0xC1, 0x07, 0x00, 0xFC, 0xFF, 0xC0, 0x83, 0x0F, 0xC0, 0x03, 0xE0, 0xCF, 0x0F, 0xF8, 0xE0, 0x03, 0xFF, 0x1F,
    0x9E, 0x0F, 0x00, 0x00, 0x80, 0x07, 0xF8, 0xC1, 0x07, 0x00, 0xFC, 0xFF, 0xE0, 0x83, 0x0F, 0xE0, 0x03, 0xC0, 0xFF, 0x0F, 0xF8, 0xE0, 0x03, 0xFF, 0x1F,
    0x8E, 0x0F, 0x00, 0x00, 0xC0, 0x07, 0xF0, 0xC1, 0x07, 0x00, 0xFC, 0x7F, 0xE0, 0x83, 0x0F, 0xE0, 0x03, 0x80, 0xFF, 0x03, 0xF8, 0xF0, 0x83, 0xFF, 0x1F,
    0xC6, 0x0F, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xE4, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xE0, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xF0, 0x1F, 0x00, 0x00, 0x00, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xF0, 0x9F, 0x01, 0x00, 0xC0, 0x7F, 0x40, 0x00, 0x30, 0x80, 0x1F, 0x60, 0x07, 0xFC, 0x00, 0x0C, 0xF8, 0x07, 0x6E, 0xC0, 0x1F, 0x18, 0xF8, 0x80, 0x1B,
    0xF8, 0x1F, 0x07, 0x00, 0xFE, 0x7F, 0x40, 0x00, 0x28, 0x80, 0x10, 0x10, 0x0C, 0x04, 0x01, 0x0A, 0x40, 0x00, 0xC3, 0xC0, 0x10, 0x08, 0x08, 0x80, 0x10,
    0xF0, 0x3F, 0x0F, 0xF0, 0xFF, 0x3F, 0x40, 0x00, 0x48, 0x80, 0x10, 0x10, 0x08, 0x04, 0x01, 0x12, 0xC0, 0x00, 0x81, 0xC0, 0x10, 0x08, 0x08, 0x80, 0x00,
    0xE0, 0x3F, 0x3F, 0xFC, 0xFF, 0x1F, 0x40, 0x00, 0x44, 0x80, 0x0F, 0x18, 0x08, 0xFC, 0x00, 0x11, 0x40, 0x00, 0x81, 0xC0, 0x0F, 0x08, 0xF8, 0x00, 0x1F,
    0xE0, 0x3F, 0x7F, 0xF8, 0xFF, 0x1F, 0x40, 0x00, 0xC4, 0x80, 0x10, 0x10, 0x08, 0x84, 0x00, 0x31, 0x40, 0x00, 0x81, 0xC0, 0x18, 0x08, 0x08, 0x00, 0x30,
    0xC0, 0x3F, 0xFF, 0xE1, 0xFF, 0x0F, 0x40, 0x00, 0x86, 0x80, 0x30, 0x10, 0x0C, 0x84, 0x81, 0x21, 0x40, 0x00, 0x81, 0xC0, 0x10, 0x08, 0x08, 0x80, 0x20,
    0x80, 0x3F, 0xFE, 0xC3, 0xFF, 0x07, 0xC0, 0x00, 0x02, 0x81, 0x18, 0x60, 0x06, 0x04, 0x81, 0x40, 0xC0, 0x00, 0x66, 0xC0, 0x10, 0x18, 0x08, 0x80, 0x11,
    0x00, 0x7F, 0xFE, 0x0F, 0xFF, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0E,
    0x00, 0x7E, 0xFE, 0x1F, 0xFE, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x78, 0xFE, 0x7F, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x70, 0xFE, 0xFF, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x40, 0xFC, 0xFF, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFC, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xF0, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};


} // namespace portal

static void renderPortal(VFD& v) {
    using namespace portal;

    uint32_t now = millis();

    // Reset whenever the user enters this page so the typewriter always
    // restarts at line 0 (and the Aperture splash plays again).
    if (s_pageChangedAt != lastResetAt) {
        lastResetAt = s_pageChangedAt;
        initialized = false;
        inSplash    = true;
        splashStart = now;
    }

    if (inSplash) {
        if (now - splashStart < kSplashMs) {
            v.drawXBM((256 - kApertureW) / 2, 0,
                      kApertureW, kApertureH, kApertureLogo);
            return;
        }
        inSplash = false;
    }

    if (!initialized) {
        for (int i = 0; i < N_BUF; ++i) buf[i] = -1;
        visibleCount = 0;
        nextLineIdx  = 0;
        typedChars   = 0;
        lineDoneMs   = 0;
        lastCharMs   = now;
        nextGlitchAt = now + 4000 + random(3000);
        initialized  = true;
    }

    // === State machine: pause → add (jump up if full) → type ==============
    if (lineDoneMs > 0) {
        // Brief pause after the bottom line finishes typing.
        if (now - lineDoneMs >= 700) {
            if (visibleCount < N_VISIBLE) {
                buf[visibleCount] = (int8_t)nextLineIdx;
                ++visibleCount;
            } else {
                // Buffer full — jump the visible rows up by one and place
                // the new line at the bottom.
                for (int i = 0; i < N_VISIBLE - 1; ++i) buf[i] = buf[i + 1];
                buf[N_VISIBLE - 1] = (int8_t)nextLineIdx;
            }
            nextLineIdx = (nextLineIdx + 1) % kLineCount;
            typedChars  = 0;
            lineDoneMs  = 0;
            lastCharMs  = now;
        }
    } else {
        // Typing the bottom-most non-empty line.
        if (visibleCount == 0) {
            buf[0]       = (int8_t)nextLineIdx;
            visibleCount = 1;
            nextLineIdx  = (nextLineIdx + 1) % kLineCount;
            typedChars   = 0;
            lastCharMs   = now;
        } else {
            const char* line = kLines[buf[visibleCount - 1]];
            int len = (int)strlen(line);
            if (typedChars < len) {
                if (now - lastCharMs >= 55) {
                    ++typedChars;
                    lastCharMs = now;
                }
            } else {
                lineDoneMs = now;
            }
        }
    }

    // === Glitch trigger ===================================================
    if (now > nextGlitchAt) {
        glitchEndMs  = now + 220 + random(180);
        nextGlitchAt = now + 4000 + random(3000);
    }
    bool glitching = now < glitchEndMs;

    // === Render: typewriter text across the full width ====================
    v.setFont(u8g2_font_5x7_tf);

    int typingRow = (lineDoneMs == 0) ? (visibleCount - 1) : -1;
    int maxRow    = visibleCount;

    auto drawText = [&](int xOff) {
        for (int i = 0; i < maxRow; ++i) {
            if (buf[i] < 0) continue;
            int y = (i + 1) * ROW_H;
            if (y < -ROW_H || y > 56) continue;
            const char* line = kLines[buf[i]];
            if (i == typingRow) {
                char tmp[40];
                int n = typedChars;
                int len = (int)strlen(line);
                if (n > len) n = len;
                if (n > (int)sizeof(tmp) - 2) n = (int)sizeof(tmp) - 2;
                memcpy(tmp, line, n);
                tmp[n]     = ((millis() / 250) & 1) ? '_' : ' ';
                tmp[n + 1] = 0;
                v.drawStr(2 + xOff, y, tmp);
            } else {
                v.drawStr(2 + xOff, y, line);
            }
        }
    };

    if (glitching) {
        // 3 horizontal bands shifted ±3 px, plus a sprinkle of noise.
        int o1 = (int)random(7) - 3;
        int o2 = (int)random(5) - 2;
        int o3 = (int)random(7) - 3;
        v.setClipWindow(0, 0,  256, 17);
        drawText(o1);
        v.setClipWindow(0, 17, 256, 34);
        drawText(o2);
        v.setClipWindow(0, 34, 256, 50);
        drawText(o3);
        v.setMaxClipWindow();
        for (int i = 0; i < 12; ++i) {
            int nx = (int)random(256);
            int ny = (int)random(50);
            if (random(2)) v.drawPixel(nx, ny);
        }
    } else {
        drawText(0);
    }
}


// ----- Cat tamagotchi -----------------------------------------------------
//  A little cat lives on a floor at y=48 with some furniture. It picks an
//  activity at random, walks to the right spot, does the thing, and picks
//  again. Used both as a standalone page and as a clock background.
// ============================================================================
namespace tama {

enum Action : uint8_t {
    A_WANDER, A_EAT, A_NAP, A_PLAY, A_SCRATCH, A_POOP, A_MEOW, A_PURR, A_ZOOMIES,
    A_HAIRBALL,
    A_DRINK, A_GROOM, A_STRETCH, A_YAWN, A_KNEAD, A_WATCH,
    A_CHANDELIER,
    A_COUNT
};

enum Tail : uint8_t {
    T_STILL, T_UP, T_CURLED, T_FLICK, T_PUFFED, T_DOWN
};

constexpr int FLOOR_Y    = 48;
constexpr int BOWL_X     = 22;
constexpr int WATER_X    = 38;
constexpr int POST_X     = 60;
constexpr int PLANT_X    = 105;
constexpr int BED_X      = 128;
constexpr int LITTER_X   = 230;
constexpr int PICTURE_X  = 242;     // small wall picture, top-right corner
constexpr int WATCH_X    = 247;     // spot where the cat sits to stare at the picture
constexpr int CHAND_X    = 28;      // ceiling-hung chandelier's horizontal pivot

static const char* const kActionLabels[A_COUNT] = {
    "wandering", "eating",   "napping",    "playing",
    "scratching", "pooping", "meowing",    "purring", "zoomies!",
    "hairball!",
    "drinking",  "grooming", "stretching", "yawning", "kneading", "watching",
    "chandelier!"
};

// Weights sum to 100 so random(100) maps cleanly via cumulative.
static const uint8_t kActionWeights[A_COUNT] = {
    10, // WANDER
    7,  // EAT
    22, // NAP  (long durations → dominates real time)
    7,  // PLAY
    6,  // SCRATCH
    5,  // POOP
    4,  // MEOW
    6,  // PURR
    3,  // ZOOMIES
    2,  // HAIRBALL
    5,  // DRINK
    7,  // GROOM
    4,  // STRETCH
    4,  // YAWN
    3,  // KNEAD
    4,  // WATCH
    1   // CHANDELIER — rare moment of misbehaviour
};

static float    s_catX         = 128.0f;
static int8_t   s_dir          = 1;
static Action   s_action       = A_WANDER;
static int      s_targetX      = 128;
static bool     s_atTarget     = true;
static bool     s_actionStarted = false;      // idle timer runs after arrival
static uint32_t s_actionBeganMs = 0;
static uint32_t s_actionEndMs  = 0;
static uint32_t s_lastFrameMs  = 0;
static uint8_t  s_walkPhase    = 0;
static uint32_t s_lastWalkMs   = 0;
static uint32_t s_lastPoopMs   = 0;     // when != 0, poop visible in litter
static uint32_t s_bubbleEndMs  = 0;
static const char* s_bubbleTxt = nullptr;
static uint8_t  s_scratchPhase = 0;
static uint32_t s_lastScratchMs = 0;
static uint8_t  s_eatPhase     = 0;
static uint32_t s_lastEatMs    = 0;

// Rolling ball physics — bounces off the walls, takes an occasional swat
// from the cat during A_PLAY, and slowly decelerates via friction.
static float    s_ballX        = 186.0f;
static float    s_ballVx       = 14.0f;
static uint32_t s_lastBatMs    = 0;

// Hairballs — recorded when an A_HAIRBALL action ends, linger on the
// floor for 60 s. Up to MAX_HAIRBALLS of them coexist (ring buffer).
constexpr int MAX_HAIRBALLS = 3;
struct Hairball { int16_t x; uint32_t bornMs; };
static Hairball s_hairballs[MAX_HAIRBALLS] = {};
static uint8_t  s_hairballNext = 0;

static Tail tailFor(Action a) {
    switch (a) {
        case A_WANDER:   return T_STILL;
        case A_EAT:      return T_DOWN;
        case A_NAP:      return T_CURLED;
        case A_PLAY:     return T_UP;
        case A_SCRATCH:  return T_PUFFED;
        case A_POOP:     return T_DOWN;
        case A_MEOW:     return T_UP;
        case A_PURR:     return T_CURLED;
        case A_ZOOMIES:  return T_FLICK;
        case A_HAIRBALL: return T_DOWN;
        case A_DRINK:    return T_DOWN;
        case A_GROOM:    return T_CURLED;
        case A_STRETCH:  return T_STILL;
        case A_YAWN:     return T_STILL;
        case A_KNEAD:    return T_UP;
        case A_WATCH:      return T_STILL;
        case A_CHANDELIER: return T_FLICK;
        default:           return T_STILL;
    }
}

// Idle-at-target durations — how long the cat lingers once arrived.
static uint32_t durationFor(Action a) {
    switch (a) {
        case A_WANDER:   return 1500 + random(2000);    // short stop/sniff
        case A_EAT:      return 4000 + random(2000);
        case A_NAP:      return 18000 + random(20000);  // ~18–38 s
        case A_PLAY:     return 3500 + random(2500);
        case A_SCRATCH:  return 3000 + random(2000);
        case A_POOP:     return 2500 + random(1500);
        case A_MEOW:     return 1500 + random(800);
        case A_PURR:     return 8000 + random(5000);
        case A_ZOOMIES:  return 3500 + random(2500);    // duration from start
        case A_HAIRBALL: return 2800 + random(1200);
        case A_DRINK:    return 3000 + random(2000);
        case A_GROOM:    return 5000 + random(3000);
        case A_STRETCH:  return 2000 + random(1000);
        case A_YAWN:     return 1500 + random(800);
        case A_KNEAD:    return 3000 + random(2000);
        case A_WATCH:      return 5000 + random(3000);
        case A_CHANDELIER: return 3000 + random(2000);
        default:           return 3000;
    }
}

static void setBubble(const char* t, uint32_t ms) {
    s_bubbleTxt   = t;
    s_bubbleEndMs = millis() + ms;
}

// Start a specific action. Shared by the random picker and the external
// "trigger" path so manual commands follow exactly the same state machine.
static void startAction(Action a) {
    s_action        = a;
    s_atTarget      = false;
    s_actionStarted = false;
    s_actionEndMs   = 0;            // set when the cat arrives
    s_bubbleTxt     = nullptr;      // clear any stale bubble from prior action

    switch (a) {
        case A_EAT:        s_targetX = BOWL_X;           break;
        case A_DRINK:      s_targetX = WATER_X;          break;
        case A_NAP:        s_targetX = BED_X;            break;
        case A_KNEAD:      s_targetX = BED_X;            break;
        case A_PLAY:       s_targetX = (int)s_ballX;     break;
        case A_SCRATCH:    s_targetX = POST_X;           break;
        case A_POOP:       s_targetX = LITTER_X;         break;
        case A_WATCH:      s_targetX = WATCH_X;          break;
        case A_CHANDELIER: s_targetX = CHAND_X;          break;
        case A_WANDER:     s_targetX = 16 + random(224); break;
        case A_ZOOMIES:    s_targetX = 10 + random(236); break;
        case A_MEOW:
        case A_PURR:
        case A_HAIRBALL:
        case A_GROOM:
        case A_STRETCH:
        case A_YAWN:       s_targetX = (int)s_catX;      break;
        default:           s_targetX = (int)s_catX;
    }

    // ZOOMIES is the one exception — its timer runs from the pick-moment
    // because it never really "arrives" anywhere, it just runs around.
    if (a == A_ZOOMIES) {
        s_actionBeganMs = millis();
        s_actionEndMs   = millis() + durationFor(A_ZOOMIES);
        s_actionStarted = true;
    }

    if      (s_targetX > s_catX) s_dir = 1;
    else if (s_targetX < s_catX) s_dir = -1;
}

static void pickNextAction() {
    int r = random(100);
    int cum = 0;
    Action a = A_NAP;
    for (int i = 0; i < A_COUNT; ++i) {
        cum += kActionWeights[i];
        if (r < cum) { a = (Action)i; break; }
    }
    startAction(a);
}

// Public manual trigger — returns true if `name` matched a known action.
static bool triggerByName(const char* name) {
    struct { const char* n; Action a; } kMap[] = {
        {"wander",     A_WANDER},
        {"eat",        A_EAT},
        {"nap",        A_NAP},
        {"sleep",      A_NAP},
        {"play",       A_PLAY},
        {"scratch",    A_SCRATCH},
        {"poop",       A_POOP},
        {"meow",       A_MEOW},
        {"purr",       A_PURR},
        {"zoomies",    A_ZOOMIES},
        {"zoom",       A_ZOOMIES},
        {"hairball",   A_HAIRBALL},
        {"drink",      A_DRINK},
        {"groom",      A_GROOM},
        {"stretch",    A_STRETCH},
        {"yawn",       A_YAWN},
        {"knead",      A_KNEAD},
        {"watch",      A_WATCH},
        {"chandelier", A_CHANDELIER},
    };
    for (auto& e : kMap) {
        if (strcasecmp(name, e.n) == 0) { startAction(e.a); return true; }
    }
    return false;
}

// Bubbles get set when the cat actually starts doing the thing, not the
// moment it picks the action — nothing reads "meow!" while the cat is
// still walking to its meow-spot.
static void triggerArrivalBubble(Action a) {
    switch (a) {
        case A_NAP:      setBubble("Zzz",    5000); break;
        case A_PURR:     setBubble("purr~",  3500); break;
        case A_MEOW:     setBubble("meow!",  1500); break;
        case A_PLAY:     setBubble("!",      1000); break;
        case A_HAIRBALL: setBubble("hck!",   2500); break;
        case A_YAWN:     setBubble("mrrph",  1500); break;
        case A_GROOM:    setBubble("*lick*", 2000); break;
        case A_DRINK:    setBubble("*lap*",  2000); break;
        case A_KNEAD:    setBubble("mrrp",   2500); break;
        case A_WATCH:      setBubble("?",       3000); break;
        case A_CHANDELIER: setBubble("wheee!",  2500); break;
        default: break;
    }
}

static void update() {
    uint32_t now = millis();
    float dt = (s_lastFrameMs == 0) ? 0.05f : (now - s_lastFrameMs) / 1000.0f;
    if (dt > 0.15f) dt = 0.15f;
    s_lastFrameMs = now;

    // --- Ball physics: drift + bounce + heavy friction --------------------
    //  High per-frame friction so a yarn ball settles within a couple of
    //  seconds of being batted — a real one doesn't keep rolling on carpet.
    s_ballX  += s_ballVx * dt;
    s_ballVx *= 0.90f;
    if (fabsf(s_ballVx) < 1.5f) s_ballVx = 0;
    if (s_ballX < 4.0f)   { s_ballX = 4.0f;   s_ballVx =  fabsf(s_ballVx) * 0.55f; }
    if (s_ballX > 252.0f) { s_ballX = 252.0f; s_ballVx = -fabsf(s_ballVx) * 0.55f; }

    // --- Cat movement -----------------------------------------------------
    if (!s_atTarget) {
        float speed = (s_action == A_ZOOMIES) ? 60.0f : 14.0f;
        float dx = (float)s_targetX - s_catX;
        if (fabsf(dx) < 1.0f) {
            s_catX     = (float)s_targetX;
            s_atTarget = true;
        } else {
            s_catX += (dx > 0 ? speed : -speed) * dt;
            s_dir   = (dx > 0) ? 1 : -1;
            uint32_t walkInterval = (s_action == A_ZOOMIES) ? 90 : 190;
            if (now - s_lastWalkMs > walkInterval) {
                s_walkPhase ^= 1;
                s_lastWalkMs = now;
            }
        }
    }

    // While playing, the ball is the target — keep chasing it as it rolls
    // and swat it when close enough. Play's timer is already running since
    // the first arrival at the ball (see arrival block below).
    if (s_action == A_PLAY) {
        int nt = (int)s_ballX;
        if (nt != s_targetX) {
            s_targetX = nt;
            if (fabsf(s_catX - s_ballX) > 1.5f) s_atTarget = false;
        }
        if (fabsf(s_catX - s_ballX) < 4.0f && now - s_lastBatMs > 450) {
            float push = 28.0f + (float)random(30);
            s_ballVx    = (s_dir >= 0 ? push : -push);
            s_lastBatMs = now;
        }
    }

    // Zoomies keeps re-targeting until the action times out.
    if (s_action == A_ZOOMIES && s_atTarget) {
        s_targetX  = 10 + random(236);
        s_atTarget = false;
    }

    // Arrival: the cat just reached its target. Start the idle timer and
    // trigger whatever bubble the action owns.
    if (s_atTarget && !s_actionStarted) {
        s_actionStarted = true;
        s_actionBeganMs = now;
        s_actionEndMs   = now + durationFor(s_action);
        triggerArrivalBubble(s_action);
    }

    // Periodic sub-animation ticks (only while idling at target)
    if (s_atTarget && now - s_lastScratchMs > 180 &&
        (s_action == A_SCRATCH || s_action == A_KNEAD)) {
        s_scratchPhase ^= 1;
        s_lastScratchMs = now;
    }
    if (s_atTarget && now - s_lastEatMs > 250 &&
        (s_action == A_EAT || s_action == A_DRINK || s_action == A_HAIRBALL)) {
        s_eatPhase ^= 1;
        s_lastEatMs = now;
    }

    if (s_actionStarted && now >= s_actionEndMs) {
        if (s_action == A_POOP) s_lastPoopMs = now;
        if (s_action == A_HAIRBALL) {
            s_hairballs[s_hairballNext] = { (int16_t)s_catX, now };
            s_hairballNext = (uint8_t)((s_hairballNext + 1) % MAX_HAIRBALLS);
        }
        pickNextAction();
    }
}

// Chandelier — hangs from the ceiling at CHAND_X. `swing` shifts the body
// horizontally (positive = to the right) so the chain tilts and the arms
// sway in unison. When the cat is on it this produces the whee!-effect.
static void drawChandelier(VFD& v, int swing) {
    int cx    = CHAND_X;
    int pivot = 5;                    // body's top edge in display coords
    int bx    = cx + swing;           // swung body-centre
    // Chain from ceiling (cx, 0) to pivot (bx, pivot)
    v.drawLine(cx, 0, bx, pivot);
    // Top bar
    v.drawHLine(bx - 4, pivot, 9);
    // Three arms dangling + bulbs
    v.drawVLine(bx - 4, pivot + 1, 3);
    v.drawVLine(bx,     pivot + 1, 3);
    v.drawVLine(bx + 4, pivot + 1, 3);
    v.drawPixel(bx - 4, pivot + 4);
    v.drawPixel(bx,     pivot + 4);
    v.drawPixel(bx + 4, pivot + 4);
}

// Cat hanging from the chandelier bar — paws gripping the bar, body below
// swinging with the light.
static void drawHangingCat(VFD& v, int barCX, int barY, int dir) {
    // Paws on bar
    v.drawPixel(barCX - 3, barY);
    v.drawPixel(barCX + 3, barY);
    // Body below, oval-ish
    v.drawBox(barCX - 2, barY + 1, 5, 3);
    // Head below body
    v.drawBox(barCX - 1, barY + 4, 3, 2);
    v.drawPixel(barCX - 1, barY + 6);      // chin
    // Tail flicking sideways
    int tx = barCX + dir * 3;
    v.drawPixel(tx,             barY + 2);
    v.drawPixel(tx + dir,       barY + 1);
    v.drawPixel(tx + dir * 2,   barY + 1);
}

// Sitting cat — used for MEOW, GROOM, YAWN, WATCH. Upright on haunches
// with a tall head; tail curled beside the body.
static void drawSitting(VFD& v, int cx, int baseY, int dir) {
    v.drawHLine(cx - 3, baseY,     7);
    v.drawHLine(cx - 2, baseY - 1, 5);
    v.drawBox  (cx - 1, baseY - 4, 3, 3);                // body
    v.drawBox  (cx - 1, baseY - 7, 3, 3);                // head
    v.drawPixel(cx - 1, baseY - 8);                      // ears
    v.drawPixel(cx + 1, baseY - 8);
    v.setDrawColor(0);
    v.drawPixel(cx + dir, baseY - 6);                    // eye
    v.setDrawColor(1);
    v.drawPixel(cx - 1, baseY - 2);                      // front paws
    v.drawPixel(cx + 1, baseY - 2);
    int tx = cx - dir * 3;                               // curled tail
    v.drawPixel(tx,       baseY);
    v.drawPixel(tx,       baseY - 1);
    v.drawPixel(tx + dir, baseY - 2);
}

// Stretched-out cat — extended body, arms reaching forward, tail back.
static void drawStretched(VFD& v, int cx, int baseY, int dir) {
    int by = baseY - 2;
    v.drawBox(cx - 6, by, 13, 2);                        // long body
    int hx = cx + dir * 7;                               // head at far end
    v.drawBox(hx - 1, by - 1, 3, 2);
    v.drawPixel(hx - 1, by - 2);
    v.drawPixel(hx + 1, by - 2);
    v.setDrawColor(0);
    v.drawPixel(hx + dir, by - 1);
    v.setDrawColor(1);
    for (int i = 2; i <= 4; ++i)                         // paws reaching
        v.drawPixel(hx + dir * i, by);
    v.drawPixel(cx - 3, baseY);                          // back legs touch floor
    v.drawPixel(cx + 3, baseY);
    int tx = cx - dir * 7;                               // long tail back
    for (int i = 0; i < 3; ++i) v.drawPixel(tx - dir * i, by);
}

// Kneading cat — on all fours at the bed, front paws alternating up/down.
static void drawKneading(VFD& v, int cx, int baseY, int dir, uint8_t phase) {
    int by = baseY - 3;
    v.drawBox(cx - 4, by, 9, 3);
    v.drawPixel(cx - 4, by + 2);
    v.drawPixel(cx + 4, by + 2);

    int hx = cx + dir * 5;
    v.drawBox(hx - 1, by - 2, 3, 3);
    v.drawPixel(hx - 1, by - 3);
    v.drawPixel(hx + 1, by - 3);
    v.setDrawColor(0);
    v.drawPixel(hx + dir, by - 1);
    v.setDrawColor(1);

    // Front paws kneading — alternate between resting on bed and raised.
    int p1Y = (phase & 1) ? baseY - 1 : baseY;
    int p2Y = (phase & 1) ? baseY     : baseY - 1;
    v.drawPixel(cx + dir * 3, p1Y);
    v.drawPixel(cx + dir * 2, p2Y);

    // Back legs
    v.drawVLine(cx - 2, baseY, 1);
    v.drawVLine(cx - 3, baseY, 1);

    // Happy tail up
    int tx = cx - dir * 4;
    for (int i = 0; i < 4; ++i) v.drawPixel(tx - dir * (i / 2), by - i);
}

// Squat — used at the litter box. Compact body, legs tucked, tail low.
static void drawSquat(VFD& v, int cx, int baseY, int dir) {
    int by = baseY - 2;
    v.drawBox(cx - 3, by, 7, 2);                 // short body
    int hx = cx + dir * 4;
    v.drawBox(hx - 1, by - 1, 3, 2);             // head low/forward
    v.drawPixel(hx - 1, by - 2);                 // ears
    v.drawPixel(hx + 1, by - 2);
    v.setDrawColor(0);
    v.drawPixel(hx + dir, by - 1);               // eye
    v.setDrawColor(1);
    // Tail drooping behind, close to the ground.
    int tx = cx - dir * 3;
    v.drawPixel(tx,       by);
    v.drawPixel(tx - dir, by + 1);
    v.drawPixel(tx - dir, by);
}

// Leaning against the scratching post — cat stands on hind legs with front
// paws on the post. `phase` alternates the paw heights for a scratch cycle.
static void drawLeaning(VFD& v, int postX, int baseY, int dir, uint8_t phase) {
    // Body offset 3 px from the post on the "came from" side.
    int bx = postX - dir * 3;

    // Vertical body
    v.drawBox(bx - 1, baseY - 6, 3, 6);

    // Head slightly tilted forward (toward the post).
    int hx = bx + dir;
    v.drawBox(hx - 1, baseY - 9, 3, 3);
    v.drawPixel(hx - 1, baseY - 10);              // ears
    v.drawPixel(hx + 1, baseY - 10);
    v.setDrawColor(0);
    v.drawPixel(hx + dir, baseY - 8);             // eye
    v.setDrawColor(1);

    // Front paws on the post, alternating heights with scratch phase.
    int paw1Y = (phase & 1) ? baseY - 7 : baseY - 5;
    int paw2Y = (phase & 1) ? baseY - 4 : baseY - 6;
    v.drawPixel(bx + dir * 2, paw1Y);
    v.drawPixel(bx + dir * 2, paw2Y);

    // Hind feet planted on the ground
    v.drawPixel(bx - 1, baseY);
    v.drawPixel(bx + 1, baseY);

    // Tail hanging down behind the body
    int tx = bx - dir * 2;
    v.drawPixel(tx, baseY - 4);
    v.drawPixel(tx, baseY - 2);
    v.drawPixel(tx - dir, baseY - 1);
}

// Curled-up cat (nap / purr). Drawn as a ball with a stubby head + tail.
static void drawCurled(VFD& v, int cx, int baseY) {
    v.drawDisc(cx, baseY - 2, 4);
    // Small ears
    v.drawPixel(cx - 2, baseY - 5);
    v.drawPixel(cx + 2, baseY - 5);
    // Closed eye line
    v.drawPixel(cx - 1, baseY - 3);
    // Tail wrapped over the body
    v.drawLine(cx - 4, baseY - 1, cx - 5, baseY - 2);
    v.drawPixel(cx - 4, baseY - 3);
    v.drawPixel(cx - 3, baseY - 4);
}

// Standard standing / walking cat sprite.
static void drawStanding(VFD& v, int cx, int baseY, int dir, Tail tail, uint8_t phase,
                         int yOffset = 0) {
    int by = baseY - 3 + yOffset;

    // Body
    v.drawBox(cx - 4, by, 9, 3);
    v.drawPixel(cx - 4, by + 2);
    v.drawPixel(cx + 4, by + 2);

    // Head on the facing side
    int hx = cx + dir * 5;
    v.drawBox(hx - 1, by - 2, 3, 3);
    // Ears
    v.drawPixel(hx - 1, by - 3);
    v.drawPixel(hx + 1, by - 3);
    // Eye
    v.setDrawColor(0);
    v.drawPixel(hx + dir, by - 1);
    v.setDrawColor(1);

    // Legs (alternating)
    int legL = cx - 2;
    int legR = cx + 2;
    if ((phase & 1) == 0) {
        v.drawVLine(legL, baseY - 0 + yOffset, 1);
        v.drawVLine(legR, baseY - 1 + yOffset, 2);
    } else {
        v.drawVLine(legL, baseY - 1 + yOffset, 2);
        v.drawVLine(legR, baseY - 0 + yOffset, 1);
    }

    // Tail on the non-facing side
    int tx = cx - dir * 4;
    int ty = by;
    switch (tail) {
        case T_STILL:
            for (int i = 0; i < 4; ++i) v.drawPixel(tx - dir * i, ty);
            break;
        case T_UP:
            for (int i = 0; i < 4; ++i) v.drawPixel(tx - dir * (i / 2), ty - i);
            break;
        case T_DOWN:
            for (int i = 0; i < 3; ++i) v.drawPixel(tx - dir * i, ty + 1 + (i == 2 ? 1 : 0));
            break;
        case T_FLICK:
            v.drawLine(tx, ty, tx - dir * 2, ty - 2);
            v.drawLine(tx - dir * 2, ty - 2, tx - dir * 3, ty);
            break;
        case T_PUFFED:
            v.drawBox(tx - dir * 3, ty - 3, 2, 5);
            v.drawLine(tx, ty, tx - dir * 2, ty);
            break;
        case T_CURLED:
            v.drawPixel(tx, ty);
            v.drawPixel(tx - dir, ty - 1);
            v.drawPixel(tx - dir * 2, ty - 1);
            break;
    }
}

// Compute the swing offset when the cat is hanging on the chandelier: a
// damped cosine so it swings wildly on entry, slowly relaxing to centre.
static int chandelierSwing() {
    if (s_action != A_CHANDELIER || !s_atTarget) return 0;
    float elapsed = (millis() - s_actionBeganMs) / 1000.0f;
    float damp    = 1.0f - 0.15f * elapsed;
    if (damp < 0.2f) damp = 0.2f;
    return (int)(8.0f * damp * cosf(elapsed * 5.5f));
}

static void drawFurniture(VFD& v) {
    // Floor
    v.drawHLine(0, FLOOR_Y + 1, 256);

    // Chandelier — stationary unless the cat is swinging on it.
    drawChandelier(v, chandelierSwing());

    // Food bowl
    v.drawLine(BOWL_X - 4, FLOOR_Y, BOWL_X - 3, FLOOR_Y - 2);
    v.drawLine(BOWL_X - 3, FLOOR_Y - 2, BOWL_X + 3, FLOOR_Y - 2);
    v.drawLine(BOWL_X + 3, FLOOR_Y - 2, BOWL_X + 4, FLOOR_Y);
    v.drawHLine(BOWL_X - 3, FLOOR_Y, 7);
    v.drawPixel(BOWL_X - 1, FLOOR_Y - 1);
    v.drawPixel(BOWL_X + 1, FLOOR_Y - 1);

    // Water bowl — smaller, next to the food bowl.
    v.drawLine(WATER_X - 3, FLOOR_Y, WATER_X - 2, FLOOR_Y - 1);
    v.drawHLine(WATER_X - 2, FLOOR_Y - 1, 5);
    v.drawLine(WATER_X + 2, FLOOR_Y - 1, WATER_X + 3, FLOOR_Y);
    v.drawHLine(WATER_X - 2, FLOOR_Y, 5);
    v.drawPixel(WATER_X, FLOOR_Y - 1);    // ripple

    // Scratching post — vertical pole with a base and a top perch. Height
    // capped at 9 px so it clears the clock digits in CLK_TAMA mode.
    v.drawVLine(POST_X,     FLOOR_Y - 9, 9);
    v.drawVLine(POST_X + 1, FLOOR_Y - 9, 9);
    v.drawHLine(POST_X - 2, FLOOR_Y - 10, 6);
    v.drawHLine(POST_X - 3, FLOOR_Y,      8);

    // Potted plant (decorative) — pot + three fronds.
    {
        int px = PLANT_X;
        v.drawHLine(px - 2, FLOOR_Y - 3, 5);
        v.drawLine (px - 2, FLOOR_Y - 3, px - 1, FLOOR_Y);
        v.drawLine (px + 2, FLOOR_Y - 3, px + 1, FLOOR_Y);
        v.drawHLine(px - 1, FLOOR_Y,     3);
        // Fronds rising above the pot
        v.drawPixel(px,     FLOOR_Y - 4);
        v.drawPixel(px,     FLOOR_Y - 5);
        v.drawPixel(px - 1, FLOOR_Y - 6);
        v.drawPixel(px + 1, FLOOR_Y - 6);
        v.drawPixel(px - 2, FLOOR_Y - 7);
        v.drawPixel(px + 2, FLOOR_Y - 7);
        v.drawPixel(px,     FLOOR_Y - 8);
    }

    // Cat bed — flat crescent.
    v.drawLine(BED_X - 8, FLOOR_Y, BED_X - 9, FLOOR_Y - 2);
    v.drawLine(BED_X - 9, FLOOR_Y - 2, BED_X + 9, FLOOR_Y - 2);
    v.drawLine(BED_X + 9, FLOOR_Y - 2, BED_X + 8, FLOOR_Y);
    v.drawHLine(BED_X - 8, FLOOR_Y, 17);

    // Small wall picture on the right so the upper-right corner isn't bare.
    v.drawFrame(PICTURE_X, 1, 10, 7);
    v.drawLine (PICTURE_X + 1, 6, PICTURE_X + 4, 3);
    v.drawLine (PICTURE_X + 4, 3, PICTURE_X + 8, 6);

    // Litter box
    v.drawFrame(LITTER_X - 6, FLOOR_Y - 3, 13, 4);
    v.drawPixel(LITTER_X - 4, FLOOR_Y - 1);
    v.drawPixel(LITTER_X - 2, FLOOR_Y - 2);
    v.drawPixel(LITTER_X + 3, FLOOR_Y - 1);
    // Poop + stink lines visible for up to 60 s after an A_POOP finishes.
    if (s_lastPoopMs && millis() - s_lastPoopMs < 60000) {
        v.drawPixel(LITTER_X, FLOOR_Y - 2);
        v.drawPixel(LITTER_X + 1, FLOOR_Y - 2);
        // Two wavy vertical stink lines that shimmy horizontally over time.
        uint32_t t = millis() / 180;
        for (int line = 0; line < 2; ++line) {
            int bx = LITTER_X - 2 + line * 4;
            for (int dy = 0; dy < 6; ++dy) {
                int ox = ((t + dy + line) & 1);
                v.drawPixel(bx + ox, FLOOR_Y - 5 - dy);
            }
        }
    }
}

// Hairballs linger on the floor for 60 s, then vanish.
static void drawHairballs(VFD& v) {
    uint32_t now = millis();
    for (int i = 0; i < MAX_HAIRBALLS; ++i) {
        uint32_t born = s_hairballs[i].bornMs;
        if (!born || now - born > 60000) continue;
        int hx = s_hairballs[i].x;
        v.drawPixel(hx - 1, FLOOR_Y);
        v.drawPixel(hx,     FLOOR_Y);
        v.drawPixel(hx + 1, FLOOR_Y);
        v.drawPixel(hx - 1, FLOOR_Y - 1);
        v.drawPixel(hx + 1, FLOOR_Y - 1);
    }
}

// Rolling yarn ball at its current physics-driven position.
static void drawBall(VFD& v) {
    int bx = (int)s_ballX;
    int by = FLOOR_Y - 2;
    v.drawCircle(bx, by, 2);
    v.drawPixel(bx, by);             // little highlight
    // Motion streaks when rolling
    if (fabsf(s_ballVx) > 3.0f) {
        int d = (s_ballVx > 0) ? -1 : 1;
        v.drawPixel(bx + d * 4, by);
        v.drawPixel(bx + d * 6, by);
    }
}

static void drawActionLabel(VFD& v) {
    v.setFont(u8g2_font_4x6_tf);
    const char* label = kActionLabels[s_action];
    int tw = v.getStrWidth(label);
    int bw = tw + 4;
    int bx = (256 - bw) / 2;            // centred horizontally on top
    int by = 0, bh = 8;
    v.setDrawColor(0);
    v.drawBox(bx, by, bw, bh);
    v.setDrawColor(1);
    v.drawFrame(bx, by, bw, bh);
    v.drawStr(bx + 2, by + 6, label);
}

static void drawBubble(VFD& v, int cx, int topY, const char* txt) {
    v.setFont(u8g2_font_4x6_tf);
    int tw = v.getStrWidth(txt);
    int bw = tw + 4;
    int bx = cx - bw / 2;
    int by = topY - 9;
    if (bx < 1) bx = 1;
    if (bx + bw > 255) bx = 255 - bw;
    v.setDrawColor(0);
    v.drawBox(bx, by, bw, 8);
    v.setDrawColor(1);
    v.drawFrame(bx, by, bw, 8);
    v.drawStr(bx + 2, by + 6, txt);
    // Little tail pointing down
    v.drawPixel(cx,     by + 8);
    v.drawPixel(cx - 1, by + 8);
}

static void step(VFD& v) {
    update();
    drawFurniture(v);
    drawHairballs(v);
    drawBall(v);

    int ix = (int)s_catX;
    bool atT     = s_atTarget;
    bool swinging = (s_action == A_CHANDELIER)               && atT;
    bool curled  = (s_action == A_NAP || s_action == A_PURR) && atT;
    bool squat   = (s_action == A_POOP)                      && atT;
    bool leaning = (s_action == A_SCRATCH)                   && atT;
    bool sitting = atT && (s_action == A_MEOW  || s_action == A_GROOM ||
                           s_action == A_YAWN  || s_action == A_WATCH);
    bool stretch = atT && s_action == A_STRETCH;
    bool knead   = atT && s_action == A_KNEAD;

    if      (swinging) {
        // Cat is up on the chandelier — body hangs from the swinging bar.
        int swing = chandelierSwing();
        drawHangingCat(v, CHAND_X + swing, 6, s_dir);
    }
    else if (curled)  drawCurled  (v, ix, FLOOR_Y);
    else if (squat)   drawSquat   (v, ix, FLOOR_Y, s_dir);
    else if (leaning) drawLeaning (v, POST_X, FLOOR_Y, s_dir, s_scratchPhase);
    else if (sitting) drawSitting (v, ix, FLOOR_Y, s_dir);
    else if (stretch) drawStretched(v, ix, FLOOR_Y, s_dir);
    else if (knead)   drawKneading(v, BED_X, FLOOR_Y, s_dir, s_scratchPhase);
    else {
        int yOff = 0;
        if ((s_action == A_EAT   || s_action == A_DRINK) && atT && s_eatPhase) yOff = 1;
        if (s_action == A_HAIRBALL && atT && ((millis() / 300) & 1))           yOff = 2;
        drawStanding(v, ix, FLOOR_Y, s_dir, tailFor(s_action), s_walkPhase, yOff);
    }

    if (s_action == A_ZOOMIES) {
        for (int i = 1; i < 4; ++i) {
            int lx = ix - s_dir * (4 + i * 2);
            if (lx >= 0 && lx < 256)
                v.drawPixel(lx, FLOOR_Y - 2);
        }
    }
}

// Draws the active thought-bubble (if any) — split out so it can be layered
// over the clock digits in CLK_TAMA mode instead of being buried underneath.
static void drawBubbleIfActive(VFD& v) {
    if (!s_bubbleTxt || millis() >= s_bubbleEndMs) {
        s_bubbleTxt = nullptr;
        return;
    }
    if (s_action == A_CHANDELIER && s_atTarget) {
        // Cat's near the ceiling, bubble goes to the right of the chandelier.
        int bx = CHAND_X + 20 + chandelierSwing();
        drawBubble(v, bx, 14, s_bubbleTxt);
        return;
    }
    bool curled = (s_action == A_NAP || s_action == A_PURR) && s_atTarget;
    bool squat  = (s_action == A_POOP) && s_atTarget;
    int topY = FLOOR_Y - (curled || squat ? 6 : 8);
    drawBubble(v, (int)s_catX, topY, s_bubbleTxt);
}

} // namespace tama

static void renderTamagotchi(VFD& v) {
    tama::step(v);                   // everything except the bubble
    tama::drawActionLabel(v);        // only shown on the standalone page
    tama::drawBubbleIfActive(v);     // bubble sits over everything else
}

bool uiTriggerCatAction(const char* name) {
    return tama::triggerByName(name);
}

// Clock background: the tamagotchi room without the action label. Bubble
// is NOT drawn here — renderTimeDetail draws it explicitly after the clock
// digits so the cat's thoughts cover the time rather than vice versa.
static void drawTamaBackground(VFD& v) { tama::step(v); }


static void drawMatrixRain(VFD& v) {
    constexpr int kCols  = 25;
    constexpr int kCellW = 10;
    constexpr int kCellH = 10;
    constexpr int kTrail = 5;
    constexpr int kGlyphCount = 86;    // U+30A1..U+30F6 (katakana)

    struct Col {
        int16_t  headY;
        uint16_t speed;                // ms between steps
        uint8_t  len;
        uint8_t  glyphs[kTrail];       // index into the katakana block
        uint32_t lastStep;
    };
    static Col cols[kCols];
    static bool initialized = false;

    auto glyphBytes = [](uint8_t idx, char* out) {
        uint16_t cp = 0x30A1 + idx;
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        out[3] = 0;
    };
    auto randomGlyph = []() { return (uint8_t)random(kGlyphCount); };
    auto resetColumn = [&](Col& c) {
        c.headY    = -(int16_t)random(50);
        c.speed    = 60 + random(220);
        c.len      = 3 + random(kTrail - 2);
        c.lastStep = 0;
        for (int k = 0; k < kTrail; ++k) c.glyphs[k] = randomGlyph();
    };

    if (!initialized) {
        for (int i = 0; i < kCols; ++i) resetColumn(cols[i]);
        initialized = true;
    }

    uint32_t now = millis();
    v.setFont(u8g2_font_b10_t_japanese1);

    for (int i = 0; i < kCols; ++i) {
        Col& c = cols[i];
        if (now - c.lastStep >= c.speed) {
            c.headY += kCellH;
            for (int k = kTrail - 1; k > 0; --k) c.glyphs[k] = c.glyphs[k - 1];
            c.glyphs[0] = randomGlyph();
            c.lastStep = now;
            if (c.headY - c.len * kCellH > 55) {
                resetColumn(c);
                c.lastStep = now;            // avoid instant re-fire
            }
        }
        int x = i * kCellW + 3;
        for (int t = 0; t < c.len && t < kTrail; ++t) {
            int yy = c.headY - t * kCellH;
            if (yy < -2 || yy > 52) continue;
            char buf[4];
            glyphBytes(c.glyphs[t], buf);
            v.drawUTF8(x, yy, buf);

            // Trail fade: for older positions, knock out every other pixel in
            // a checker pattern so the glyph looks dimmer than the head. Two
            // of this fake grayscale (t>=2 single-strip, t>=3 double-strip) is
            // enough to give a gradient on a mono display.
            if (t >= 2) {
                v.setDrawColor(0);
                int top = yy - 9, bot = yy + 1;
                if (top < 0)  top = 0;
                if (bot > 49) bot = 49;
                for (int py = top; py <= bot; ++py) {
                    int parity = (t >= 3) ? 0 : (py & 1);   // t>=3: strip every row
                    for (int px = x + parity; px < x + kCellW; px += 2) {
                        if (px < 0 || px >= 256) continue;
                        v.drawPixel(px, py);
                    }
                }
                v.setDrawColor(1);
            }
        }
    }
}

static void renderMatrix(VFD& v) {
    drawMatrixRain(v);
    if (s_matrixBrightP >= 0.0f) {
        // Centered bottom progress bar showing current brightness level.
        const int bw = 128, bh = 5;
        const int bx = (256 - bw) / 2;
        const int by = 43;
        v.setDrawColor(0);
        v.drawBox(bx - 2, by - 2, bw + 4, bh + 4);
        v.setDrawColor(1);
        v.drawFrame(bx, by, bw, bh);
        int fillW = (int)(s_matrixBrightP * (bw - 2) + 0.5f);
        if (fillW > 0) v.drawBox(bx + 1, by + 1, fillW, bh - 2);
    }
}

// ----- OctoPrint / Prusa --------------------------------------------------
//  Live print job + temps from a local OctoPrint instance. Polls at the
//  cadence chosen by main.cpp (every ~10s while WiFi is up).
// ============================================================================
// Tiny header badge (about 11×7) showing the current state visually:
//  printing → 3 dots cycling, paused → "||", idle → breathing dot,
//  offline → crossed square.
static void drawPrusaStatusBadge(VFD& v, int bx, int by,
                                  bool printing, bool paused,
                                  bool idle, bool offline) {
    uint32_t now = millis();
    if (offline) {
        v.drawLine(bx,     by,     bx + 6, by + 6);
        v.drawLine(bx,     by + 6, bx + 6, by);
        return;
    }
    if (paused) {
        v.drawVLine(bx + 1, by,     7);
        v.drawVLine(bx + 5, by,     7);
        return;
    }
    if (idle) {
        int phase = (int)((now / 200) % 12);
        int r = (phase < 6) ? phase / 2 : (12 - phase) / 2;   // 0..3
        int cx = bx + 4, cy = by + 3;
        if (r > 0) v.drawCircle(cx, cy, r);
        else       v.drawPixel(cx, cy);
        return;
    }
    // Printing: three dots, "growing" tail; cycle 1.2 s.
    int frame = (int)((now / 300) % 4);            // 0..3
    int cy = by + 3;
    for (int i = 0; i < 3; ++i) {
        if (i < frame) v.drawDisc(bx + 1 + i * 4, cy, 1);
    }
}

static void fmtHms(char* dst, size_t n, long s) {
    if (s < 0) { snprintf(dst, n, "?"); return; }
    long h = s / 3600;
    long m = (s % 3600) / 60;
    if (h > 0) snprintf(dst, n, "%ldh%02ld", h, m);
    else       snprintf(dst, n, "%ldm", m);
}

static void fmtFileSize(char* dst, size_t n, long bytes) {
    if (bytes <= 0)            { snprintf(dst, n, "--");      return; }
    if (bytes < 1024)          { snprintf(dst, n, "%ldB", bytes); return; }
    if (bytes < 1024L * 1024)  { snprintf(dst, n, "%ldK", bytes / 1024); return; }
    snprintf(dst, n, "%.1fM", bytes / (1024.0f * 1024.0f));
}

static void renderPrusa(VFD& v) {
    const OctoPrint& o = getOctoPrint();
    v.drawFrame(0, 0, 256, 50);

    if (!o.valid) {
        drawTooltip(v, "waiting for prusa...");
        return;
    }

    bool printing = o.state.startsWith("Printing")
                 || o.state.startsWith("Pausing")
                 || o.state.startsWith("Cancelling");
    bool paused   = o.state.equalsIgnoreCase("Paused");
    bool idle     = o.state.equalsIgnoreCase("Operational")
                 || o.state.equalsIgnoreCase("Ready");
    bool offline  = !printing && !paused && !idle;

    // --- Header: "PRUSA - <state>" + animated status badge at far right ---
    v.setFont(u8g2_font_5x7_tf);
    v.drawStr(3, 8, "PRUSA");
    char hdr[40];
    snprintf(hdr, sizeof(hdr), "- %s", o.state.c_str());
    v.drawStr(32, 8, hdr);
    drawPrusaStatusBadge(v, /*x=*/240, /*y=*/2, printing, paused, idle, offline);
    v.drawHLine(1, 10, 254);

    const int kBodyW = 250;

    v.setFont(u8g2_font_5x7_tf);

    if (printing || paused) {
        // --- Top band: filename (left) + file size (right), then bar -----
        char sz[12]; fmtFileSize(sz, sizeof(sz), o.fileSizeBytes);
        int szW = (o.fileSizeBytes > 0) ? v.getStrWidth(sz) : 0;
        const int nameMaxW = kBodyW - (szW > 0 ? szW + 4 : 0);
        drawScrolling(v, 3, 17, nameMaxW, o.fileName);
        if (szW > 0) v.drawStr(253 - szW, 17, sz);

        // Borderless progress fill — grows left-to-right at 4 px tall.
        const int pbX = 3, pbY = 19, pbW = kBodyW, pbH = 4;
        int fillW = (int)(pbW * o.progressPct / 100.0f);
        if (fillW < 0) fillW = 0;
        if (fillW > pbW) fillW = pbW;
        if (fillW > 0) v.drawBox(pbX, pbY, fillW, pbH);

        // HRule + vertical dividers separating three info groups.
        v.drawHLine(1, 25, 254);
        v.drawVLine(85,  26, 23);
        v.drawVLine(170, 26, 23);

        // --- Column 1 (x=3..82): time -----------------------------------
        char el[12], lf[12], et[12];
        fmtHms(el, sizeof(el), o.printTimeS);
        fmtHms(lf, sizeof(lf), o.printTimeLeftS);
        fmtHms(et, sizeof(et), o.estimatedTotalS);
        char endStr[12] = "--:--";
        if (o.printTimeLeftS > 0) {
            time_t now = time(nullptr);
            if (now > 1700000000) {
                time_t end = now + o.printTimeLeftS;
                struct tm endTm; localtime_r(&end, &endTm);
                snprintf(endStr, sizeof(endStr), "%02d:%02d",
                         endTm.tm_hour, endTm.tm_min);
            }
        }
        char buf[24];
        snprintf(buf, sizeof(buf), "elapsed %s", el); v.drawStr(3, 34, buf);
        snprintf(buf, sizeof(buf), "left   %s",  lf); v.drawStr(3, 41, buf);
        snprintf(buf, sizeof(buf), "end    %s",  endStr); v.drawStr(3, 48, buf);

        // --- Column 2 (x=88..167): filament + meta ----------------------
        char szLine[24];
        if (o.filamentLenMm > 0) {
            float total_m = o.filamentLenMm / 1000.0f;
            float used_m  = total_m * (o.progressPct / 100.0f);
            snprintf(buf, sizeof(buf), "fil %.1fm", used_m);
            v.drawStr(88, 34, buf);
            snprintf(buf, sizeof(buf), "of  %.1fm", total_m);
            v.drawStr(88, 41, buf);
        } else {
            v.drawStr(88, 34, "fil --");
            v.drawStr(88, 41, "of  --");
        }
        if (o.estimatedTotalS > 0) {
            snprintf(szLine, sizeof(szLine), "est %s", et);
            v.drawStr(88, 48, szLine);
        } else if (o.fileSizeBytes > 0) {
            snprintf(szLine, sizeof(szLine), "%d%%", (int)lroundf(o.progressPct));
            v.drawStr(88, 48, szLine);
        }

        // --- Column 3 (x=173..253): temps --------------------------------
        char l[24];
        snprintf(l, sizeof(l), "H %d/%d",
                 (int)lroundf(o.hotendActual), (int)lroundf(o.hotendTarget));
        v.drawStr(173, 34, l);
        snprintf(l, sizeof(l), "B %d/%d",
                 (int)lroundf(o.bedActual),    (int)lroundf(o.bedTarget));
        v.drawStr(173, 41, l);
        snprintf(l, sizeof(l), "%d%%", (int)lroundf(o.progressPct));
        v.drawStr(173, 48, l);
    } else {
        // After ~30 min since the last printing/paused state, swap the
        // dormant body for a centred "waiting for print" tooltip.
        // lastPrintingEpoch is wall-clock UTC and persisted across reboots,
        // so this still triggers correctly after a power-cycle or OTA flash.
        constexpr long kIdleAlertSecs = 30L * 60;
        time_t nowUtc = time(nullptr);
        bool ntpSynced = nowUtc > 1700000000;
        bool stale = false;
        if (ntpSynced) {
            if (o.lastPrintingEpoch == 0)               stale = true;
            else if (nowUtc - o.lastPrintingEpoch > kIdleAlertSecs) stale = true;
        }
        if (stale) {
            // Body region is y=11..49 → centre the dialog at y=30.
            drawTooltipAtY(v, "waiting for print", 30);
            return;
        }

        // Idle / offline body. Last loaded job + bottom temps.
        if (o.fileName.length()) {
            String s = String("last: ") + o.fileName;
            drawScrolling(v, 3, 18, kBodyW, s);
        } else {
            v.drawStr(3, 18, "no job loaded");
        }

        // Meta line — file size, slicer estimate, filament total
        char meta[80]; meta[0] = 0;
        if (o.fileSizeBytes > 0) {
            char sz[12]; fmtFileSize(sz, sizeof(sz), o.fileSizeBytes);
            snprintf(meta + strlen(meta), sizeof(meta) - strlen(meta),
                     "size %s", sz);
        }
        if (o.estimatedTotalS > 0) {
            char e[12]; fmtHms(e, sizeof(e), o.estimatedTotalS);
            if (meta[0]) strncat(meta, "   ", sizeof(meta) - strlen(meta) - 1);
            snprintf(meta + strlen(meta), sizeof(meta) - strlen(meta),
                     "est %s", e);
        }
        if (o.filamentLenMm > 0) {
            if (meta[0]) strncat(meta, "   ", sizeof(meta) - strlen(meta) - 1);
            snprintf(meta + strlen(meta), sizeof(meta) - strlen(meta),
                     "%.1fm", o.filamentLenMm / 1000.0f);
        }
        if (meta[0]) v.drawStr(3, 28, meta);

        v.drawHLine(1, 38, 254);

        char temps[40];
        snprintf(temps, sizeof(temps), "H %d/%d    B %d/%d",
                 (int)lroundf(o.hotendActual), (int)lroundf(o.hotendTarget),
                 (int)lroundf(o.bedActual),    (int)lroundf(o.bedTarget));
        v.drawStr(3, 47, temps);
    }
}

// ----- Toast overlays (page / font changed) ------------------------------
static void maybeDrawToast(VFD& v) {
    // Page change toast
    uint32_t ageP = millis() - s_pageChangedAt;
    uint32_t ageF = millis() - s_fontChangedAt;

    uint32_t ageV  = millis() - s_vizChangedAt;
    uint32_t ageC  = millis() - s_clkChangedAt;
    uint32_t ageCF = millis() - s_cfontChangedAt;
    uint32_t age12 = millis() - s_12hChangedAt;
    const char* label = nullptr;
    char buf[32];
    if (s_pageChangedAt && ageP < 900) {
        static const char* names[] = {"OVERVIEW", "TIME", "WEATHER", "NOW PLAYING", "MATRIX", "CATS", "TAMAGOTCHI", "CLAUDE", "PORTAL", "PRUSA", "WATCH"};
        snprintf(buf, sizeof(buf), "> %s", names[s_page]);
        label = buf;
    } else if (s_fontChangedAt && ageF < 900) {
        snprintf(buf, sizeof(buf), "font: %s", kFonts[s_font].name);
        label = buf;
    } else if (s_vizChangedAt && ageV < 900) {
        snprintf(buf, sizeof(buf), "viz: %s", kVizNames[s_viz]);
        label = buf;
    } else if (s_clkChangedAt && ageC < 900) {
        snprintf(buf, sizeof(buf), "clock: %s", kClockNames[s_clk]);
        label = buf;
    } else if (s_cfontChangedAt && ageCF < 900) {
        snprintf(buf, sizeof(buf), "clock font: %s", kClockFonts[s_cfont].name);
        label = buf;
    } else if (s_12hChangedAt && age12 < 900) {
        snprintf(buf, sizeof(buf), "clock: %s", s_use12h ? "12H" : "24H");
        label = buf;
    } else if (s_dashRightChangedAt && (millis() - s_dashRightChangedAt) < 900) {
        static const char* views[] = {"viz", "claude"};
        snprintf(buf, sizeof(buf), "view: %s",
                 views[s_dashRight < kDashRightCount ? s_dashRight : 0]);
        label = buf;
    }
    if (!label) return;

    v.setFont(u8g2_font_5x7_tf);
    int w = v.getStrWidth(label);
    int x = 256 - w - 4;
    int y = 0;
    v.setDrawColor(0);
    v.drawBox(x - 2, y, w + 4, 9);
    v.setDrawColor(1);
    v.drawFrame(x - 2, y, w + 4, 9);
    v.drawStr(x, y + 7, label);
}

// ----- Display registration for callback-driven rendering ----------------
//  Saves a pointer to the active VFD so OTA progress callbacks (which fire
//  inside ArduinoOTA.handle() while the main loop is blocked) can refresh
//  the splash without going through uiRender().
static VFD* s_vfd = nullptr;
void uiSetDisplay(VFD& vfd) { s_vfd = &vfd; }

// ----- OTA upload screen --------------------------------------------------
//  Shown by uiRender() while ArduinoOTA is mid-transfer. Renders a
//  full-screen "FLASHING" banner with a progress bar so the user can see the
//  device is busy and roughly how far along the upload is.
static void renderOtaScreen(VFD& v) {
    int pct = netOtaProgressPct();
    bool error = (pct < 0);

    v.drawFrame(0, 0, 256, 50);
    v.drawFrame(1, 1, 254, 48);

    // Monospace terminal-style header: "> FLASHING" matches the boot-log
    // chevron prefix elsewhere on the splash screen.
    v.setFont(u8g2_font_6x13_tf);
    const char* title = error ? "> OTA ERROR" : "> FLASHING";
    int tw = v.getUTF8Width(title);
    v.drawUTF8((256 - tw) / 2, 17, title);

    v.setFont(u8g2_font_5x7_tf);
    const char* sub = error ? "[ upload failed - retry ]"
                            : "[ do not power off ]";
    int sw = v.getStrWidth(sub);
    v.drawStr((256 - sw) / 2, 26, sub);

    if (!error) {
        const int pbX = 18, pbY = 30, pbW = 220, pbH = 8;
        v.drawFrame(pbX, pbY, pbW, pbH);
        int fillW = ((pbW - 2) * pct) / 100;
        if (fillW < 0) fillW = 0;
        if (fillW > pbW - 2) fillW = pbW - 2;
        if (fillW > 0) v.drawBox(pbX + 1, pbY + 1, fillW, pbH - 2);

        char pctBuf[8];
        snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);
        int pw = v.getStrWidth(pctBuf);
        v.drawStr((256 - pw) / 2, 47, pctBuf);
    }
}

void uiOtaTick() {
    if (!s_vfd) return;
    if (!netOtaActive()) return;
    s_vfd->clearBuffer();
    renderOtaScreen(*s_vfd);
    s_vfd->sendBuffer();
}

// ----- Public render -----------------------------------------------------
void uiRender(VFD& v) {
    if (netOtaActive()) {
        v.clearBuffer();
        renderOtaScreen(v);
        v.sendBuffer();
        return;
    }
    v.clearBuffer();
    switch (s_page) {
        case PAGE_OVERVIEW:     renderOverview(v);         break;
        case PAGE_TIME:         renderTimeDetail(v);       break;
        case PAGE_WEATHER:      renderWeatherDetail(v);    break;
        case PAGE_NOW_PLAYING:  renderNowPlayingDetail(v); break;
        case PAGE_MATRIX:       renderMatrix(v);           break;
        case PAGE_CATS:         renderCats(v);             break;
        case PAGE_TAMA:         renderTamagotchi(v);       break;
        case PAGE_CLAUDE:       renderClaudeUsage(v);      break;
        case PAGE_PORTAL:       renderPortal(v);           break;
        case PAGE_PRUSA:        renderPrusa(v);            break;
        case PAGE_WATCH:        renderWatch(v);            break;
    }
    maybeDrawToast(v);
    v.sendBuffer();
}

void uiBoot(VFD& v, const char* msg) {
    constexpr int kMaxLines = 7;          // 7 × 7px = 49 — fits inside frame
    static String s_log[kMaxLines];
    static int    s_count = 0;

    if (s_count < kMaxLines) {
        s_log[s_count++] = msg;
    } else {
        for (int i = 0; i < kMaxLines - 1; ++i) s_log[i] = s_log[i + 1];
        s_log[kMaxLines - 1] = msg;
    }

    v.clearBuffer();
    v.drawFrame(0, 0, 256, 50);
    v.setFont(u8g2_font_5x7_tf);
    for (int i = 0; i < s_count; ++i) {
        char line[48];
        snprintf(line, sizeof(line), "> %s", s_log[i].c_str());
        v.drawStr(4, 8 + i * 7, line);
    }
    v.sendBuffer();
}

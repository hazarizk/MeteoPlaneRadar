// =============================================================================
//  MeteoPlaneRadar
//  Screen: the planets. See ScreenPlanets.h.
//
//  Author:  Petr / chiptron.cz   (vyvoj / development: chiptron.cz)
//  Board:   Waveshare ESP32-S3-Touch-LCD-2.1 (round 480x480 display, ST7701)
// =============================================================================
#include "ScreenPlanets.h"
#include "Astro.h"
#include "AstroGlyphs.h"
#include "Settings.h"
#include "Outside.h"
#include "Layout.h"
#include "Lang.h"
#include "UI.h"
#include "Display_ST7701.h"
#include "Canvas16.h"
#include "Config.h"
#include "TimeZone.h"
#include "TimeUtil.h"

#include <time.h>
#include <math.h>
#include <string.h>

#define CX (LCD_WIDTH / 2)
#define CY (LCD_HEIGHT / 2)

// --- Geometry ---------------------------------------------------------------
// The zodiac ring sits where the price dial sits, for the same reason: the
// screen dots and the clock line at the top are drawn by code that knows
// nothing about this screen, so the ring stays below them (top edge at y=52).
#define R_ZOUT   188
#define R_ZIN    152
#define R_ZLABEL 170        // sign names, centred in the band

// Planets just inside the ring. Two in the same place would sit on top of
// each other, so a planet that would collide with one already placed drops
// one step inwards - the way a chart stacks a conjunction.
#define R_PLANET  128
#define R_STEP     28
#define R_LEVELS    3
#define PLANET_R   12       // half the glyph box (24 px glyphs, AstroGlyphs.h)
#define R_DISC     14       // the shaded disc under each glyph
#define MIN_SEP_DEG 12.0f   // closer than this at the same radius = stack

// The stars: how many in the planet area (between the aspect circle and the
// ring) and how many outside the ring, out to the glass.
#define STARS_INNER 150
#define STARS_OUTER  80

// Aspect lines run between points on this inner circle; the middle inside it
// is for the Moon's phase and the ascendant. 70 rather than something
// smaller: at 58 the few lines crossed in a knot the size of a fingernail.
#define R_ASPECT  70

// Only aspects this close to exact are drawn. Textbook orbs (six to eight
// degrees) put fifteen lines in the middle of a one-centimetre circle on
// this panel and nobody could tell what was what; two degrees leaves the
// handful that an astrologer would actually call tight.
#define ASPECT_ORB_DEG 2.0f

// How long the detail panel stays up by itself. Longer than the price
// screen's hour readout: there are seven lines to read.
#define DETAIL_HOLD_MS 60000UL

// Recompute the chart this often. The Moon moves half a degree an hour - a
// pixel and a half - so once a minute is already generous; what actually
// moves on the screen between recomputes is the ascendant, one degree every
// four minutes.
#define RECOMPUTE_MS 60000UL

// --- Time travel ------------------------------------------------------------
// The orbital elements in Astro.* are Schlyter's: a few arc minutes over
// 1900-2100, a degree for Pluto over 1800-2100, and drifting beyond that. On
// a wheel where a degree is three pixels 1800-2100 is the honest range, so
// the arrows stop there. UTC epochs; a 64-bit time_t (ESP32 core 3) reaches.
#define T_MIN ((time_t)-5364662400LL)     // 1800-01-01 00:00 UTC
#define T_MAX ((time_t) 4133980740LL)     // 2100-12-31 23:59 UTC
static_assert(sizeof(time_t) >= 8, "time travel to 1800 needs a 64-bit time_t");

#define CHIPS_HOLD_MS   60000UL   // the chips hide this long after the last touch
#define BROWSE_IDLE_MS 600000UL   // ten minutes alone and the wheel is back at now
#define HOLD_START_MS     500UL   // a finger on an arrow this long starts it running
#define HOLD_STEP_MS       80UL   // ...and then it steps this often - the draw cap

// The chips: < NOW > in a row just under the ring (its outer edge is at
// y=428), the step unit in a smaller chip under them, in the sliver of glass
// at the foot that nothing else uses. The row's ends sit inside the chord of
// the circle there; the unit chip's rounded corners clear the glass by a
// couple of pixels, which is as low as anything can go.
#define CHIP_Y    440
#define CHIP_H     28
#define CHIP_ARW   44
#define CHIP_NOW   76
#define CHIP_DX    68             // centre of each arrow chip from CX
#define UNIT_Y    466
#define UNIT_H     18
#define UNIT_W     64

// --- Colours ----------------------------------------------------------------
// The bodies. Chosen to be told apart at arm's length on black, and roughly
// the colours the tradition gives them where that does not fight legibility.
static const uint16_t C_BODY[AB_COUNT] = {
  0xFEA0,   // Sun      - gold
  0xDEFB,   // Moon     - silver
  0xFCC0,   // Mercury  - orange
  0x9FE6,   // Venus    - light green
  0xF945,   // Mars     - red
  0xBD5F,   // Jupiter  - violet
  0xA5B9,   // Saturn   - steel grey-blue
  0x07FF,   // Uranus   - cyan
  0x3C7F,   // Neptune  - blue
  0xC9B3,   // Pluto    - rose
  0xA514,   // Node     - grey
};

// The four elements, dark for the sector and bright for its name. Fire red,
// earth green, air yellow, water blue: the convention every chart uses.
static const uint16_t C_ELEM_FILL[4] = { 0x5000, 0x0222, 0x4A20, 0x00EB };
static const uint16_t C_ELEM_TEXT[4] = { 0xFBCB, 0x7F2F, 0xFF2F, 0x7DBF };

// Aspects: opposition, square, trine, sextile. Hard aspects warm, soft ones
// cool, dim enough to sit under the planets rather than compete with them.
#define C_OPPOSITION 0xB124
#define C_SQUARE     0xCBC0
#define C_TRINE      0x2B3B
#define C_SEXTILE    0x2D4F
#define C_AXIS       0x4208
#define C_EARTH      0x3DBC

// Under half the brightness, same hue: a body below the horizon. Dim enough
// that the eye sorts the wheel into "up" and "down" at a glance, bright
// enough that the symbol can still be read and tapped.
static inline uint16_t dim565(uint16_t c) {
  const int r = ((c >> 11) & 0x1F) * 2 / 5, g = ((c >> 5) & 0x3F) * 2 / 5, b = (c & 0x1F) * 2 / 5;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// --- Blending ---------------------------------------------------------------
// The canvas is a plain RGB565 framebuffer in PSRAM (Canvas16), so a pixel can
// be read back and a colour laid over it with some coverage. That is what
// makes the glyph edges smooth, and lets a glyph sit on a shaded disc, on a
// sector of the ring or among the stars without a box around it.
static inline uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t a) {   // a = 0..15
  if (a >= 15) return fg;
  const uint16_t ia = 15 - a;
  const uint16_t r = (((bg >> 11) & 31) * ia + ((fg >> 11) & 31) * a) / 15;
  const uint16_t g = (((bg >> 5) & 63) * ia + ((fg >> 5) & 63) * a) / 15;
  const uint16_t b = ((bg & 31) * ia + (fg & 31) * a) / 15;
  return (uint16_t)((r << 11) | (g << 5) | b);
}
static inline void blendPixel(int x, int y, uint16_t col, uint8_t a) {
  if (a == 0 || x < 0 || y < 0 || x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
  uint16_t* fb = static_cast<Canvas16*>(gfx)->getFramebuffer();
  if (!fb) return;
  uint16_t& p = fb[(int32_t)y * LCD_WIDTH + x];
  p = blend565(p, col, a);
}
// k/16 of a colour's brightness, same hue.
static inline uint16_t scale565(uint16_t c, int k) {
  const int r = (((c >> 11) & 31) * k) >> 4, g = (((c >> 5) & 63) * k) >> 4, b = ((c & 31) * k) >> 4;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// A glyph from AstroGlyphs.h, centred on (cx, cy), blended in col. Each
// character of a row is the coverage of that pixel in sixteen steps ('.' is
// none, '1'..'9' and 'a'..'f' more, 'f' or '#' solid).
static inline uint8_t glyphLevel(char c) {
  if (c >= '1' && c <= '9') return (uint8_t)(c - '0');
  if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
  if (c == '#') return 15;
  return 0;
}
static void drawGlyph(int cx, int cy, const AstroGlyph& g, uint16_t col) {
  const int x0 = cx - g.w / 2, y0 = cy - g.h / 2;
  for (int y = 0; y < g.h; y++) {
    const char* r = g.rows[y];
    for (int x = 0; x < g.w; x++) {
      const uint8_t a = glyphLevel(r[x]);
      if (a) blendPixel(x0 + x, y0 + y, col, a);
    }
  }
}

// The small sphere under each glyph: dark, tinted in the body's hue, lit from
// the upper left, with a rim in the body's colour. It covers the stars and
// the aspect lines behind the glyph, so the symbol always reads against a
// quiet ground, and it is what makes the wheel look like planets in a sky
// rather than characters on a chart. The edge is anti-aliased the same way as
// the glyphs.
static void drawPlanetDisc(int cx, int cy, uint16_t col) {
  const float R = (float)R_DISC;
  for (int dy = -R_DISC - 1; dy <= R_DISC + 1; dy++) {
    for (int dx = -R_DISC - 1; dx <= R_DISC + 1; dx++) {
      const float d = sqrtf((float)(dx * dx + dy * dy));
      float cov = R + 0.5f - d;                 // coverage of the edge pixel
      if (cov <= 0.0f) continue;
      if (cov > 1.0f) cov = 1.0f;
      // Shading: 0 at the highlight, up and left of the centre, 1 at the far
      // rim. The ground runs from six sixteenths of the body's colour there
      // down to one; the rim, the last pixel and a half, is at eight.
      const float hx = (float)dx + 0.4f * R, hy = (float)dy + 0.4f * R;
      float s = sqrtf(hx * hx + hy * hy) / (1.4f * R);
      if (s > 1.0f) s = 1.0f;
      int k = (int)(6.0f - 5.0f * s + 0.5f);
      if (d > R - 1.5f) k = 8;
      blendPixel(cx + dx, cy + dy, scale565(col, k), (uint8_t)(cov * 15.0f + 0.5f));
    }
  }
}

// --- Short sign labels ------------------------------------------------------
// Three letters, for the one place the wheel spells a sign out in text (the
// ascendant line in the middle). Derived from the names in Lang.h (S_Z_*),
// kept here because they are abbreviations the browser never sees. The
// wheel itself uses the glyphs from AstroGlyphs.h, as a chart does.
static const char* const Z_SHORT_CZ[12] = { "Ber", "Byk", "Bli", "Rak", "Lev", "Pan", "Vah", "Sti", "Str", "Koz", "Vod", "Ryb" };
static const char* const Z_SHORT_EN[12] = { "Ari", "Tau", "Gem", "Can", "Leo", "Vir", "Lib", "Sco", "Sag", "Cap", "Aqu", "Pis" };

// International names (Settings_PlanetNamesIntl) are the Latin ones, which
// are the English column of the table - so that setting simply forces the
// English spelling whatever language the interface is in.
static bool intlNames() { return Lang_Get() == LANG_EN || Settings_PlanetNamesIntl(); }

// Compass points for the azimuth in the detail. These follow the interface
// language, not the names setting: "JZ" is not Latin for anything.
static const char* const COMPASS_CZ[8] = { "S", "SV", "V", "JV", "J", "JZ", "Z", "SZ" };
static const char* const COMPASS_EN[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
static const char* compass(float azDeg) {
  const int i = ((int)lroundf(azDeg / 45.0f)) & 7;
  return Lang_Get() == LANG_EN ? COMPASS_EN[i] : COMPASS_CZ[i];
}
static const char* zShort(int s) { return intlNames() ? Z_SHORT_EN[s] : Z_SHORT_CZ[s]; }
static const char* pName(int b)  { return intlNames() ? TE((StrId)(S_P_SUN + b))   : T((StrId)(S_P_SUN + b)); }
static const char* zName(int s)  { return intlNames() ? TE((StrId)(S_Z_ARIES + s)) : T((StrId)(S_Z_ARIES + s)); }

// --- State ------------------------------------------------------------------
static AstroChart    s_chart;
static bool          s_chartOk = false;
static unsigned long s_chartAt = 0;
static bool          s_lastTimeValid = false;

static uint8_t       s_mode = 0;            // 0 = ascendant left, 1 = Aries left
static bool          s_aspects = true;
static int           s_sel = -1;            // body whose detail is open
static unsigned long s_selAt = 0;

// Where each disc ended up on the last draw, for hit-testing a tap.
static int16_t s_px[AB_COUNT], s_py[AB_COUNT];
static bool    s_placed = false;

// Time travel. The wheel shows now unless s_browsing, and then the absolute
// moment s_at; the chart is a function of that and nothing else.
static bool          s_browsing = false;
static time_t        s_at = 0;
static uint8_t       s_unit = 2;            // 0 five minutes, 1 hour, 2 day, 3 month, 4 year
static unsigned long s_lastUse = 0;         // last touch or web request: the idle reset
static bool          s_chips = false;       // the chips are up
static unsigned long s_chipsAt = 0;
static bool          s_dirty = false;       // changed outside Tick, wants a redraw
static unsigned long s_holdNext = 0;        // next step of a held arrow
static unsigned long s_holdPrev = 0;        // heldMs last seen, to spot a new touch

static time_t shownTime() { return s_browsing ? s_at : time(nullptr); }

// Local civil time without the C library: localtime_r knows only the rule in
// force today, but the wheel goes to 1800, and TimeZone_OffsetAt() knows what
// the clock did in every year since. Howard Hinnant's civil_from_days, the
// inverse of TimeUtil_DaysFromCivil.
struct Civil { int Y, M, D, h, mi; };
static void civilFromDays(long z, int* y, int* m, int* d) {
  z += 719468;
  const long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp  = (5 * doy + 2) / 153;
  const unsigned dd  = doy - (153 * mp + 2) / 5 + 1;
  const unsigned mm  = mp < 10 ? mp + 3 : mp - 9;
  *y = (int)(era * 400 + (long)yoe + (mm <= 2 ? 1 : 0)); *m = (int)mm; *d = (int)dd;
}
static Civil toLocal(time_t utc) {
  const long long s = (long long)utc + TimeZone_OffsetAt(utc);
  const long days = (long)((s >= 0 ? s : s - 86399) / 86400);
  const long rem  = (long)(s - (long long)days * 86400);
  Civil c;
  civilFromDays(days, &c.Y, &c.M, &c.D);
  c.h = (int)(rem / 3600); c.mi = (int)((rem % 3600) / 60);
  return c;
}
static time_t fromLocal(const Civil& c) {
  return TimeZone_LocalToUtc(TimeUtil_UtcToEpoch(c.Y, c.M, c.D, c.h, c.mi, 0));
}
static int daysInMonth(int y, int m) {
  static const uint8_t n[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return n[m - 1];
}

static void touched() { s_lastUse = millis(); s_chipsAt = s_lastUse; }

static void setAt(time_t t) {
  if (t < T_MIN) t = T_MIN;
  if (t > T_MAX) t = T_MAX;
  s_at = t; s_browsing = true;
  s_chartOk = false; s_dirty = true;
}

// One step of the current unit. Minutes and hours are plain seconds. Days,
// months and years go through the calendar, so the 21st stays the 21st and
// 14:00 stays 14:00 across a change of the clock, and the 31st becomes the
// 30th where it has to rather than spilling into the next month. Five
// minutes is the fine step: the ascendant moves a degree every four.
static void stepTime(int dir) {
  time_t t = shownTime();
  if (s_unit == 0)      t += (time_t)dir * 300;
  else if (s_unit == 1) t += (time_t)dir * 3600;
  else {
    Civil c = toLocal(t);
    if (s_unit == 2) {
      // A day in the calendar, not 86400 seconds: across the clock change
      // the wall time must not slip by an hour. Days from civil handles the
      // month end; the leap second nobody has.
      const long days = TimeUtil_DaysFromCivil(c.Y, (unsigned)c.M, (unsigned)c.D) + dir;
      civilFromDays(days, &c.Y, &c.M, &c.D);
    } else if (s_unit == 3) {
      c.M += dir;
      if (c.M < 1)  { c.M = 12; c.Y--; }
      if (c.M > 12) { c.M = 1;  c.Y++; }
    } else {
      c.Y += dir;
    }
    const int dim = daysInMonth(c.Y, c.M);
    if (c.D > dim) c.D = dim;
    t = fromLocal(c);
  }
  setAt(t);
}

bool ScreenPlanets_Browsing() { return s_browsing; }

void ScreenPlanets_SetTime(time_t utc) {
  setAt(utc);
  touched();
  s_chips = true;             // show the way onwards from the moment just set
}

void ScreenPlanets_SetNow() {
  s_browsing = false;
  s_chartOk = false; s_dirty = true;
  s_lastUse = millis();
}

void ScreenPlanets_BrowseIso(char* out, size_t cap) {
  if (!out || !cap) return;
  out[0] = '\0';
  if (!s_browsing) return;
  const Civil c = toLocal(s_at);
  snprintf(out, cap, "%04d-%02d-%02dT%02d:%02d", c.Y, c.M, c.D, c.h, c.mi);
}

bool ScreenPlanets_DetailOpen() { return s_sel >= 0; }
void ScreenPlanets_CloseDetail() { s_sel = -1; }

void ScreenPlanets_Enter() {
  s_sel = -1;
  s_chartOk = false;          // compute fresh for the time we are entered at
  s_placed = false;
  s_browsing = false;         // a new visit starts at now
  s_chips = false;
}

void ScreenPlanets_ChangeRange(int dir) {
  (void)dir;                  // two orientations - either direction toggles
  s_mode = s_mode ? 0 : 1;
}

void ScreenPlanets_RangeText(char* out, size_t cap) {
  if (!out || !cap) return;
  snprintf(out, cap, "%s", s_mode ? T(S_ARIES_LEFT) : T(S_ASC_LEFT));
}

static void recompute() {
  Astro_Compute(shownTime(), Settings_Lat(), Settings_Lon(), &s_chart);
  s_chartOk = true;
  s_chartAt = millis();
}

bool ScreenPlanets_Tick() {
  bool want = s_dirty;
  s_dirty = false;
  const bool tv = Outside_TimeValid();
  if (tv != s_lastTimeValid) { s_lastTimeValid = tv; want = true; }
  // A chosen moment needs no clock at all - it is the one thing on the
  // device that can be drawn before the first HTTP response.
  if (!tv && !s_browsing) return want;

  const unsigned long now = millis();
  // At now the chart ages a minute between recomputes; browsing, it is
  // recomputed the moment the time changes (s_chartOk drops) and not otherwise.
  if (!s_chartOk || (!s_browsing && now - s_chartAt >= RECOMPUTE_MS)) { recompute(); want = true; }

  // The chips tidy themselves away; the wheel finds its own way back to now.
  if (s_chips && now - s_chipsAt >= CHIPS_HOLD_MS) { s_chips = false; want = true; }
  if (s_browsing && now - s_lastUse >= BROWSE_IDLE_MS) { ScreenPlanets_SetNow(); want = true; }

  // The detail expiring is a change on screen, so it has to ask for a redraw.
  if (s_sel >= 0 && millis() - s_selAt >= DETAIL_HOLD_MS) { s_sel = -1; want = true; }

  // The clock at the top moves every minute.
  if (UI_StatusLineChanged()) want = true;
  return want;
}

// --- Geometry helpers -------------------------------------------------------
// Ecliptic longitude -> screen angle. The zodiac runs ANTICLOCKWISE on a
// chart (Aries, then Taurus to its right going round the bottom), with the
// reference point - the ascendant, or 0 Aries - on the left. Screen y grows
// downwards, so anticlockwise on the glass is a MINUS on the sine.
static float refLon() { return s_mode ? 0.0f : s_chart.ascLon; }

static inline float lonToRad(float lon) {
  return (180.0f + (lon - refLon())) * 0.0174532925f;
}
static inline void polar(float a, float r, int* x, int* y) {
  *x = CX + (int)lroundf(r * cosf(a));
  *y = CY - (int)lroundf(r * sinf(a));
}

// A filled sector of the zodiac ring in CHART angles. UI_FillRing wants
// screen-clockwise radians from three o'clock, so the angles are negated.
static void zodiacSector(float lon0, float lon1, int rIn, int rOut, uint16_t col) {
  const float a0 = -lonToRad(lon1), a1 = -lonToRad(lon0);
  UI_FillRing(CX, CY, rIn, rOut, a0, a1, col);
}

// Can a size-1 label centred at (x, y) sit inside the round glass?
static bool insideGlass(int x, int y, int halfW, int halfH) {
  const long R = LCD_WIDTH / 2 - 3;
  const int cx[4] = { x - halfW, x + halfW, x - halfW, x + halfW };
  const int cy[4] = { y - halfH, y - halfH, y + halfH, y + halfH };
  for (int i = 0; i < 4; i++) {
    const long dx = cx[i] - CX, dy = cy[i] - CY;
    if (dx * dx + dy * dy > R * R) return false;
  }
  return true;
}

// --- Drawing: the sky -------------------------------------------------------
// A field of faint stars behind the wheel, fixed to the zodiac so that it
// turns with the ring - as the fixed stars do. Generated from a seed every
// frame, so it is always the same sky and costs no memory. Dim enough to sit
// under everything: most stars are a fifth grey, a handful are half, and
// the few brightest get a faint cross. The middle, inside the aspect circle,
// is left dark - that is the Earth, and the aspect lines live there.
static void drawStarBand(uint32_t& seed, int n, float rIn, float rOut) {
  auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) * (1.0f / 16777216.0f); };
  for (int i = 0; i < n; i++) {
    const float lon = rnd() * 360.0f;
    const float r = sqrtf(rIn * rIn + rnd() * (rOut * rOut - rIn * rIn));
    const float b = rnd();
    int x, y;
    polar(lonToRad(lon), r, &x, &y);
    uint16_t c;
    if      (b < 0.60f) c = 0x31A6;        // 19 % grey
    else if (b < 0.88f) c = 0x5ACB;        // 35 %
    else if (b < 0.97f) c = 0x8410;        // 52 %
    else                c = 0xAD55;        // 68 %, with a cross
    gfx->drawPixel(x, y, c);
    if (b >= 0.97f) {
      gfx->drawPixel(x - 1, y, 0x31A6); gfx->drawPixel(x + 1, y, 0x31A6);
      gfx->drawPixel(x, y - 1, 0x31A6); gfx->drawPixel(x, y + 1, 0x31A6);
    }
  }
}

static void drawStars() {
  uint32_t seed = 0x9E3779B9u;
  drawStarBand(seed, STARS_INNER, (float)(R_ASPECT + 8), (float)(R_ZIN - 5));
  drawStarBand(seed, STARS_OUTER, (float)(R_ZOUT + 4), (float)(LCD_WIDTH / 2 - 4));
}

// --- Drawing: the ring ------------------------------------------------------
static void drawZodiac() {
  for (int s = 0; s < 12; s++) {
    const int elem = s % 4;                      // fire, earth, air, water repeat
    zodiacSector(s * 30.0f, (s + 1) * 30.0f, R_ZIN, R_ZOUT, C_ELEM_FILL[elem]);
  }

  // Degree ticks on the inner edge: every five degrees short, every ten a
  // little longer, and a black line across the whole band at every sign
  // boundary so the twelve sectors read as twelve.
  for (int deg = 0; deg < 360; deg += 5) {
    const float a = lonToRad((float)deg);
    const float c = cosf(a), sn = sinf(a);
    int len = (deg % 10 == 0) ? 7 : 4;
    uint16_t col = C_GRAY;
    int r1 = R_ZIN + len;
    if (deg % 30 == 0) { col = C_BLACK; r1 = R_ZOUT + 1; }
    gfx->drawLine(CX + (int)(R_ZIN * c), CY - (int)(R_ZIN * sn),
                  CX + (int)(r1 * c),    CY - (int)(r1 * sn), col);
  }
  gfx->drawCircle(CX, CY, R_ZIN, C_DKGRAY);
  gfx->drawCircle(CX, CY, R_ZOUT, C_DKGRAY);

  // The sign glyphs in the middle of each sector, in the element's bright
  // colour, the way every chart labels its wheel.
  for (int s = 0; s < 12; s++) {
    int x, y;
    polar(lonToRad(s * 30.0f + 15.0f), R_ZLABEL, &x, &y);
    const AstroGlyph& g = SIGN_GLYPHS[s];
    if (!Layout_Claim(x - g.w / 2, y - g.h / 2, g.w, g.h)) continue;
    drawGlyph(x, y, g, C_ELEM_TEXT[s % 4]);
  }
}

// --- Drawing: axes ----------------------------------------------------------
// A label just outside the ring at a chart angle, if the glass has room for
// it there. Near the top and bottom it has not - the dots and the clock own
// the top, and the bottom of the circle is too narrow - and then the axis
// line alone has to do.
static void outerLabel(float lon, const char* txt, uint16_t col) {
  int x, y;
  polar(lonToRad(lon), R_ZOUT + 16, &x, &y);
  const int w = Layout_TextW(txt, 1);
  if (!insideGlass(x, y, w / 2 + 1, 5)) return;
  if (!Layout_Claim(x - w / 2 - 1, y - 5, w + 2, 10)) return;
  gfx->setTextSize(1);
  gfx->setTextColor(col);
  gfx->setCursor(x - w / 2, y - 4);
  gfx->print(txt);
}

static void drawAxes() {
  // Horizon (ascendant - descendant) and meridian (MC - IC) across the
  // planet area. Drawn before the planets, so they pass underneath.
  const float lons[2] = { s_chart.ascLon, s_chart.mcLon };
  for (int k = 0; k < 2; k++) {
    const float a = lonToRad(lons[k]);
    const float c = cosf(a), sn = sinf(a);
    const int r0 = R_ASPECT + 4, r1 = R_ZIN - 4;
    gfx->drawLine(CX + (int)(r0 * c), CY - (int)(r0 * sn), CX + (int)(r1 * c), CY - (int)(r1 * sn), C_AXIS);
    gfx->drawLine(CX - (int)(r0 * c), CY + (int)(r0 * sn), CX - (int)(r1 * c), CY + (int)(r1 * sn), C_AXIS);
    // Arrowhead at the ring for the named end.
    gfx->fillTriangle(CX + (int)(r1 * c), CY - (int)(r1 * sn),
                      CX + (int)((r1 - 9) * c - 5 * sn), CY - (int)((r1 - 9) * sn + 5 * c),
                      CX + (int)((r1 - 9) * c + 5 * sn), CY - (int)((r1 - 9) * sn - 5 * c), C_GRAY);
  }
  outerLabel(s_chart.ascLon, "ASC", C_WHITE);
  outerLabel(s_chart.mcLon,  "MC",  C_WHITE);
}

// --- Drawing: aspects -------------------------------------------------------
// The five Ptolemaic aspects. Conjunctions are not drawn - two planets within
// orb are stacked next to each other already, which says it louder than a
// line of zero length could. The orb is ASPECT_ORB_DEG for everything.
static uint16_t aspectColor(float sep) {
  const float orb = ASPECT_ORB_DEG;
  if (fabsf(sep - 180.0f) <= orb) return C_OPPOSITION;
  if (fabsf(sep - 120.0f) <= orb) return C_TRINE;
  if (fabsf(sep -  90.0f) <= orb) return C_SQUARE;
  if (fabsf(sep -  60.0f) <= orb) return C_SEXTILE;
  return 0;
}

static void drawAspects() {
  gfx->drawCircle(CX, CY, R_ASPECT + 2, C_DKGRAY);
  if (!s_aspects) return;
  for (int i = AB_SUN; i <= AB_PLUTO; i++) {
    for (int j = i + 1; j <= AB_PLUTO; j++) {
      const float sep = fabsf(Astro_DeltaDeg(s_chart.body[i].lon, s_chart.body[j].lon));
      const uint16_t col = aspectColor(sep);
      if (!col) continue;
      int x0, y0, x1, y1;
      polar(lonToRad(s_chart.body[i].lon), R_ASPECT, &x0, &y0);
      polar(lonToRad(s_chart.body[j].lon), R_ASPECT, &x1, &y1);
      gfx->drawLine(x0, y0, x1, y1, col);
    }
  }
}

// --- Drawing: planets -------------------------------------------------------
// Place the bodies on up to three concentric levels so no two discs overlap.
// Order of placement is the order of the enum - Sun first - so when a level
// is contested the traditional order decides who stays outermost.
static void placeBodies(uint8_t* level) {
  for (int i = 0; i < AB_COUNT; i++) {
    int lv = 0;
    for (lv = 0; lv < R_LEVELS; lv++) {
      bool free_ = true;
      for (int j = 0; j < i; j++) {
        if (level[j] != lv) continue;
        if (fabsf(Astro_DeltaDeg(s_chart.body[i].lon, s_chart.body[j].lon)) < MIN_SEP_DEG) { free_ = false; break; }
      }
      if (free_) break;
    }
    if (lv >= R_LEVELS) lv = R_LEVELS - 1;    // a stellium: overlap rather than vanish
    level[i] = (uint8_t)lv;
  }
}

static void drawPlanets() {
  uint8_t level[AB_COUNT];
  placeBodies(level);

  for (int i = 0; i < AB_COUNT; i++) {
    const AstroPos& p = s_chart.body[i];
    const float a = lonToRad(p.lon);
    const float c = cosf(a), sn = sinf(a);
    const uint16_t col = C_BODY[i];

    // Pointer: a short line from the ring inwards at the exact degree. The
    // disc may have been pushed inwards or sideways; this never is.
    gfx->drawLine(CX + (int)((R_ZIN - 1) * c), CY - (int)((R_ZIN - 1) * sn),
                  CX + (int)((R_ZIN - 9) * c), CY - (int)((R_ZIN - 9) * sn), col);

    const int r = R_PLANET - level[i] * R_STEP;
    int x, y;
    polar(a, (float)r, &x, &y);
    s_px[i] = (int16_t)x; s_py[i] = (int16_t)y;

    // The body: a shaded disc in its hue with the glyph in full colour on
    // top, the way a chart marks it. Below the horizon at the device's
    // location the glyph is drawn dim and the disc darker still - a glyph
    // and a disc dimmed alike would melt into each other - so one glance at
    // the wheel says what is actually up tonight. The pointer on the ring
    // follows the glyph.
    const bool up = p.altDeg >= 0.0f;
    const uint16_t drawCol = up ? col : dim565(col);
    drawPlanetDisc(x, y, up ? col : scale565(col, 4));
    drawGlyph(x, y, PLANET_GLYPHS[i], drawCol);
    if (!up) {
      gfx->drawLine(CX + (int)((R_ZIN - 1) * c), CY - (int)((R_ZIN - 1) * sn),
                    CX + (int)((R_ZIN - 9) * c), CY - (int)((R_ZIN - 9) * sn), drawCol);
    }

    // Retrograde: a small red R at the lower right, the notation a printed
    // chart uses. On a black patch, so it reads over the disc's rim and the
    // stars alike.
    if (p.retro) {
      gfx->fillRect(x + R_DISC - 2, y + R_DISC - 10, 8, 10, C_BLACK);
      gfx->setTextSize(1);
      gfx->setTextColor(C_RED);
      gfx->setCursor(x + R_DISC - 1, y + R_DISC - 9);
      gfx->print("R");
    }
    if (i == s_sel) gfx->drawCircle(x, y, R_DISC + 3, C_WHITE);
  }
  s_placed = true;
}

// --- Drawing: the middle ----------------------------------------------------
// The Moon as it looks tonight: a disc with the lit part drawn in. The
// terminator is the ellipse x = w * cos(elongation) on every row, which for
// the lit fraction f is w * (1 - 2f). Waxing, the light is on the right, as
// it is in the evening sky from the northern hemisphere; the user's latitude
// decides which, since south of the equator the Moon hangs the other way up.
#define MOON_R 18
static void drawMoonDisc(int cx, int cy) {
  const float f = s_chart.moonIllum;
  bool litRight = s_chart.moonWaxing;
  if (Settings_Lat() < 0) litRight = !litRight;
  for (int dy = -MOON_R; dy <= MOON_R; dy++) {
    const int w = (int)sqrtf((float)(MOON_R * MOON_R - dy * dy));
    const int xt = (int)lroundf(w * (1.0f - 2.0f * f));      // terminator, measured from the lit limb
    gfx->drawFastHLine(cx - w, cy + dy, 2 * w + 1, 0x18E3);  // the dark side
    int x0, x1;
    if (litRight) { x0 = cx + xt;  x1 = cx + w; }
    else          { x0 = cx - w;   x1 = cx - xt; }
    if (x1 >= x0) gfx->drawFastHLine(x0, cy + dy, x1 - x0 + 1, C_BODY[AB_MOON]);
  }
  gfx->drawCircle(cx, cy, MOON_R, C_GRAY);
}

static void drawCentre() {
  // The Moon in the middle, its phase as a picture, and under it the word
  // for which way the phase is going and the ascendant in words - the
  // latter because in the fixed orientation it is not obviously anywhere in
  // particular. Text sits on black backings because the aspect lines cross
  // the middle underneath; the Moon covers them by itself.
  drawMoonDisc(CX, CY - 12);

  char buf[24];
  const char* dir = s_chart.moonWaxing ? T(S_WAXING) : T(S_WANING);
  gfx->fillRect(CX - Layout_TextW(dir, 1) / 2 - 3, CY + 12, Layout_TextW(dir, 1) + 6, 12, C_BLACK);
  UI_TextCentered(dir, CY + 14, C_GRAY, 1);

  snprintf(buf, sizeof(buf), "ASC %s %d\xF8", zShort(Astro_SignOf(s_chart.ascLon)),
           (int)Astro_DegInSign(s_chart.ascLon));
  gfx->fillRect(CX - Layout_TextW(buf, 1) / 2 - 3, CY + 26, Layout_TextW(buf, 1) + 6, 12, C_BLACK);
  UI_TextCentered(buf, CY + 28, C_WHITE, 1);
}

// --- Drawing: the time chips -----------------------------------------------
static void chipRect(int cx, int cy, int w, int h, uint16_t border) {
  gfx->fillRoundRect(cx - w / 2, cy - h / 2, w, h, 8, C_DKGRAY);
  gfx->drawRoundRect(cx - w / 2, cy - h / 2, w, h, 8, border);
}

static void drawArrow(int cx, int cy, int dir, uint16_t col) {
  if (dir < 0) gfx->fillTriangle(cx - 7, cy, cx + 5, cy - 8, cx + 5, cy + 8, col);
  else         gfx->fillTriangle(cx + 7, cy, cx - 5, cy - 8, cx - 5, cy + 8, col);
}

static const char* unitName() {
  switch (s_unit) {
    case 0:  return T(S_U_MIN5);
    case 1:  return T(S_U_HOUR);
    case 3:  return T(S_U_MONTH);
    case 4:  return T(S_U_YEAR);
    default: return T(S_U_DAY);
  }
}

static void drawChips() {
  if (!s_chips) return;
  const time_t t = shownTime();
  const bool atMin = t <= T_MIN, atMax = t >= T_MAX;

  // The arrows, greyed out at the ends of the range.
  chipRect(CX - CHIP_DX, CHIP_Y, CHIP_ARW, CHIP_H, C_GRAY);
  drawArrow(CX - CHIP_DX, CHIP_Y, -1, atMin ? C_GRAY : C_WHITE);
  chipRect(CX + CHIP_DX, CHIP_Y, CHIP_ARW, CHIP_H, C_GRAY);
  drawArrow(CX + CHIP_DX, CHIP_Y, +1, atMax ? C_GRAY : C_WHITE);

  // NOW: lit while it would do something, quiet while the wheel is at now.
  const uint16_t nowCol = s_browsing ? C_YELLOW : C_GRAY;
  chipRect(CX, CHIP_Y, CHIP_NOW, CHIP_H, nowCol);
  const char* nowTxt = T(S_T_NOW);
  UI_TextCentered(nowTxt, CHIP_Y - 8, nowCol, 2);
  if (strcmp(nowTxt, "Ted") == 0) {
    // The font has no Ď: a caron over the D, drawn by hand. The text is
    // three size-2 characters, so the D is the twelve pixels right of CX+6.
    const int dx = CX + 12, top = CHIP_Y - 8;
    gfx->drawLine(dx - 4, top - 5, dx, top - 2, nowCol);
    gfx->drawLine(dx, top - 2, dx + 4, top - 5, nowCol);
  }

  // The step: tap to cycle five minutes, hour, day, month, year.
  chipRect(CX, UNIT_Y, UNIT_W, UNIT_H, C_GRAY);
  UI_TextCentered(unitName(), UNIT_Y - 8, C_WHITE, 2);
}

// Which chip is under a finger: 1 back, 2 now, 3 forward, 4 the unit, 0 none.
// Targets a little larger than the chips - the glass is being tapped at
// arm's length.
static int chipAt(int x, int y) {
  if (!s_chips) return 0;
  if (abs(y - CHIP_Y) <= CHIP_H / 2 + 6) {
    if (abs(x - (CX - CHIP_DX)) <= CHIP_ARW / 2 + 6) return 1;
    if (abs(x - CX) <= CHIP_NOW / 2 + 4) return 2;
    if (abs(x - (CX + CHIP_DX)) <= CHIP_ARW / 2 + 6) return 3;
  }
  if (abs(y - UNIT_Y) <= UNIT_H / 2 + 8 && abs(x - CX) <= UNIT_W / 2 + 8) return 4;
  return 0;
}

// The clock line at the top says what moment the wheel is for. At now that is
// the ordinary status line; browsing, the date and time of the wheel in
// yellow, so a chart of 1987 can never be mistaken for tonight's.
static void drawBrowseLine() {
  const Civil c = toLocal(s_at);
  char txt[24];
  if (Lang_Get() == LANG_EN) snprintf(txt, sizeof(txt), "%04d-%02d-%02d %02d:%02d", c.Y, c.M, c.D, c.h, c.mi);
  else                       snprintf(txt, sizeof(txt), "%d.%d.%04d %02d:%02d", c.D, c.M, c.Y, c.h, c.mi);
  const int w = Layout_TextW(txt, 2);
  gfx->fillRect(CX - w / 2 - 6, LY_STATUS - 3, w + 12, 22, C_BLACK);
  UI_TextCentered(txt, LY_STATUS, C_YELLOW, 2);
}

// --- Drawing: the detail panel ---------------------------------------------
static void fmtSignDeg(float lon, char* out, size_t cap, bool full) {
  const float d = Astro_DegInSign(lon);
  int deg = (int)d;
  int min = (int)lroundf((d - deg) * 60.0f);
  if (min == 60) { min = 0; deg++; }
  snprintf(out, cap, "%s %d\xF8" "%02d'", full ? zName(Astro_SignOf(lon)) : zShort(Astro_SignOf(lon)), deg, min);
}

static void drawDetail() {
  if (s_sel < 0 || s_sel >= AB_COUNT) return;
  const int b = s_sel;
  const AstroPos& p = s_chart.body[b];
  const AstroFacts* f = Astro_Facts((AstroBody)b);
  const uint16_t col = C_BODY[b];

  // 290 high: the title and up to eight rows. The corners stay inside the
  // round glass (sqrt(160^2 + 145^2) = 216 < 240).
  const int pw = 320, ph = 290;
  const int px = CX - pw / 2, py = CY - ph / 2;
  gfx->fillRoundRect(px, py, pw, ph, 14, C_DKGRAY);
  gfx->drawRoundRect(px, py, pw, ph, 14, col);

  // Close button, as on the aircraft detail.
  const int cxx = px + pw - 24, cyy = py + 22;
  gfx->fillCircle(cxx, cyy, 15, C_RED);
  gfx->drawLine(cxx - 6, cyy - 6, cxx + 6, cyy + 6, C_WHITE);
  gfx->drawLine(cxx - 6, cyy + 6, cxx + 6, cyy - 6, C_WHITE);

  // Heading: the name in the body's colour, and a red R if it is retrograde.
  gfx->setTextSize(3); gfx->setTextColor(col);
  gfx->setCursor(px + 18, py + 16);
  gfx->print(pName(b));
  if (p.retro) {
    gfx->setTextColor(C_RED);
    gfx->print(" R");
  }

  char line[40], v[24];
  int ty = py + 56;
  gfx->setTextSize(2); gfx->setTextColor(C_WHITE);
  auto row = [&](const char* s) {
    // The panel is 23 characters wide at this size; anything longer is cut
    // and ends with a full stop rather than running over the border.
    char tmp[40];
    strncpy(tmp, s, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = '\0';
    const int maxCh = (pw - 36) / 12;
    if ((int)strlen(tmp) > maxCh) { tmp[maxCh - 1] = '.'; tmp[maxCh] = '\0'; }
    gfx->setCursor(px + 18, ty); gfx->print(tmp); ty += 26;
  };

  // Sign and degree - the one line every chart reader looks for first.
  fmtSignDeg(p.lon, v, sizeof(v), true);
  row(v);

  // Where it is in the sky from here: altitude with its sign (plus is up) and
  // the compass direction, or that it is below the horizon. By day the sky
  // hides everything but the Moon, and that is worth saying next to a
  // planet that is otherwise "up" - the Sun itself needs no such note.
  if (b != AB_NODE) {
    if (p.altDeg < 0.0f) {
      snprintf(line, sizeof(line), "%s: %s", T(S_SKY), T(S_BELOW_HORIZON));
    } else {
      const bool hidden = s_chart.daylight && b != AB_SUN && b != AB_MOON;
      snprintf(line, sizeof(line), "%s: +%.0f\xF8 %s%s%s", T(S_SKY), p.altDeg, compass(p.azDeg),
               hidden ? ", " : "", hidden ? T(S_DAYTIME) : "");
    }
    row(line);
  }

  // Distance from the Earth. The Moon in kilometres, because "0.0026 AU"
  // means nothing to anyone and "384 400 km" means something to everyone.
  if (b == AB_MOON)      snprintf(line, sizeof(line), "%s: %.0f km", T(S_DISTANCE), p.distAu * 149597870.7f);
  else if (b != AB_NODE) snprintf(line, sizeof(line), "%s: %.3f AU", T(S_DISTANCE), p.distAu);
  if (b != AB_NODE) row(line);

  // Daily motion along the ecliptic. Negative is retrograde, and for the
  // node it is always negative - it runs the other way.
  snprintf(line, sizeof(line), "%s: %+.2f\xF8/d", T(S_DAILY), p.dailyDeg);
  row(line);

  // Orbital period: in days under two years, in years above.
  if (f->periodDays < 730.0f)
    snprintf(line, sizeof(line), "%s: %.1f %s", T(S_PERIOD), f->periodDays, T(S_DAYS));
  else
    snprintf(line, sizeof(line), "%s: %.1f %s", T(S_PERIOD), f->periodDays / 365.25f, T(S_YEARS));
  row(line);

  // Mean distance from the Sun and mean orbital speed - for the planets. The
  // Sun's own row would be the Earth's orbit under the wrong name, the Moon's
  // is about the Earth, and the node has neither; those are left out.
  if (b >= AB_MERCURY && b <= AB_PLUTO) {
    snprintf(line, sizeof(line), "%s: %.2f AU", T(S_FROM_SUN), f->meanDistAu);
    row(line);
    snprintf(line, sizeof(line), "%s: %.1f km/s", T(S_SPEED), f->speedKms);
    row(line);
    snprintf(line, sizeof(line), "%s: %.0f\xF8", T(S_ELONGATION), p.elongDeg);
    row(line);
  } else if (b == AB_MOON) {
    snprintf(line, sizeof(line), "%s: %.1f km/s", T(S_SPEED), f->speedKms);
    row(line);
    snprintf(line, sizeof(line), "%s: %d%% %s", T(S_ILLUMINATED),
             (int)lroundf(s_chart.moonIllum * 100.0f),
             s_chart.moonWaxing ? T(S_WAXING) : T(S_WANING));
    row(line);
  }
}

// --- Frame ------------------------------------------------------------------
void ScreenPlanets_Draw() {
  gfx->fillScreen(C_BLACK);
  Layout_Begin();
  Layout_ReserveBand(LY_DOTS - 6, 12);
  Layout_ReserveBand(LY_STATUS - 3, 22);
  if (s_browsing) drawBrowseLine();
  else            UI_DrawStatusLine(LY_STATUS);

  if (!Outside_TimeValid() && !s_browsing) {
    // The chart is a function of the time and nothing else, so without the
    // clock there is genuinely nothing to draw. The clock arrives with the
    // first HTTP response of any other screen - usually within seconds.
    UI_TextCentered(T(S_PLANETS), CY - 20, C_WHITE, 2);
    UI_TextCentered(T(S_NO_TIME), CY + 10, C_YELLOW, 2);
    s_placed = false;
    return;
  }
  if (!s_chartOk) recompute();

  drawStars();
  drawZodiac();
  drawAxes();
  drawAspects();
  drawPlanets();
  drawCentre();
  drawChips();
  drawDetail();
}

bool ScreenPlanets_HandleTap(int x, int y) {
  if (!s_chartOk || !s_placed) return false;

  // The close button of an open detail, or anywhere on the panel but a
  // planet: close it.
  if (s_sel >= 0) {
    s_sel = -1;
    return true;
  }

  // The time chips, while they are up.
  switch (chipAt(x, y)) {
    case 1: stepTime(-1);                touched(); return true;
    case 3: stepTime(+1);                touched(); return true;
    case 2: ScreenPlanets_SetNow();      touched(); return true;
    case 4: s_unit = (s_unit + 1) % 5;   touched(); return true;
    default: break;
  }

  // A planet: the nearest disc within reach of a finger. Discs are 22 px
  // across and a fingertip is wider, so the target is generous.
  int best = -1;
  long bestD2 = 20L * 20L;
  for (int i = 0; i < AB_COUNT; i++) {
    const long dx = x - s_px[i], dy = y - s_py[i];
    const long d2 = dx * dx + dy * dy;
    if (d2 < bestD2) { bestD2 = d2; best = i; }
  }
  if (best >= 0) {
    s_sel = best;
    s_selAt = millis();
    return true;
  }

  // The middle: aspect lines on or off.
  {
    const long dx = x - CX, dy = y - CY;
    if (dx * dx + dy * dy < (long)R_ASPECT * R_ASPECT) {
      s_aspects = !s_aspects;
      return true;
    }
  }

  // Anywhere else on the wheel: the time chips come up, or go away again.
  s_chips = !s_chips;
  touched();
  return true;
}

bool ScreenPlanets_HandleHold(int x, int y, unsigned long heldMs) {
  if (heldMs < s_holdPrev) s_holdNext = 0;      // a new touch has begun
  s_holdPrev = heldMs;
  if (!s_chips || heldMs < HOLD_START_MS) return false;
  const int c = chipAt(x, y);
  if (c != 1 && c != 3) return false;

  // Running: one step per frame, so the planets visibly move - a whole year
  // of days in half a minute, a century of years in eight seconds.
  const unsigned long now = millis();
  if (s_holdNext == 0 || (long)(now - s_holdNext) >= 0) {
    stepTime(c == 1 ? -1 : +1);
    s_holdNext = now + HOLD_STEP_MS;
    touched();
  }
  return true;
}

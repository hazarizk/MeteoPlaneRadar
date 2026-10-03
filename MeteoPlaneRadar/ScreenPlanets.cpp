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
#include "Config.h"

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
#define R_STEP     26
#define R_LEVELS    3
#define PLANET_R   12       // half the glyph box (24 px glyphs, AstroGlyphs.h)
#define MIN_SEP_DEG 12.0f   // closer than this at the same radius = stack

// Aspect lines run between points on this inner circle; the middle inside it
// is for the Earth and the Moon's phase.
#define R_ASPECT  58

// How long the detail panel stays up by itself. Longer than the price
// screen's hour readout: there are seven lines to read.
#define DETAIL_HOLD_MS 60000UL

// Recompute the chart this often. The Moon moves half a degree an hour - a
// pixel and a half - so once a minute is already generous; what actually
// moves on the screen between recomputes is the ascendant, one degree every
// four minutes.
#define RECOMPUTE_MS 60000UL

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

// A glyph from AstroGlyphs.h, centred on (cx, cy), lit pixels in col. Drawn
// as horizontal runs rather than single pixels; a glyph is a few dozen runs.
static void drawGlyph(int cx, int cy, const AstroGlyph& g, uint16_t col) {
  const int x0 = cx - g.w / 2, y0 = cy - g.h / 2;
  for (int y = 0; y < g.h; y++) {
    const char* r = g.rows[y];
    int x = 0;
    while (x < g.w) {
      if (r[x] != '#') { x++; continue; }
      int x1 = x;
      while (x1 < g.w && r[x1] == '#') x1++;
      gfx->drawFastHLine(x0 + x, y0 + y, x1 - x, col);
      x = x1;
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

bool ScreenPlanets_DetailOpen() { return s_sel >= 0; }
void ScreenPlanets_CloseDetail() { s_sel = -1; }

void ScreenPlanets_Enter() {
  s_sel = -1;
  s_chartOk = false;          // compute fresh for the time we are entered at
  s_placed = false;
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
  Astro_Compute(time(nullptr), Settings_Lat(), Settings_Lon(), &s_chart);
  s_chartOk = true;
  s_chartAt = millis();
}

bool ScreenPlanets_Tick() {
  bool want = false;
  const bool tv = Outside_TimeValid();
  if (tv != s_lastTimeValid) { s_lastTimeValid = tv; want = true; }
  if (!tv) return want;

  if (!s_chartOk || millis() - s_chartAt >= RECOMPUTE_MS) { recompute(); want = true; }

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
// line of zero length could. Orb: eight degrees when the Sun or the Moon is
// involved, six otherwise, which is the common textbook choice.
static uint16_t aspectColor(float sep, bool luminary) {
  const float orb = luminary ? 8.0f : 6.0f;
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
      const uint16_t col = aspectColor(sep, i == AB_SUN || i == AB_MOON || j == AB_SUN || j == AB_MOON);
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

    // The symbol itself, as on a chart: no disc, just the glyph in the body's
    // colour. Below the horizon at the device's location it is drawn dim, so
    // one glance at the wheel says what is actually up tonight. The pointer
    // on the ring follows suit.
    const bool up = p.altDeg >= 0.0f;
    const uint16_t drawCol = up ? col : dim565(col);
    drawGlyph(x, y, PLANET_GLYPHS[i], drawCol);
    if (!up) {
      gfx->drawLine(CX + (int)((R_ZIN - 1) * c), CY - (int)((R_ZIN - 1) * sn),
                    CX + (int)((R_ZIN - 9) * c), CY - (int)((R_ZIN - 9) * sn), drawCol);
    }

    // Retrograde: a small red R at the lower right, the notation a printed
    // chart uses.
    if (p.retro) {
      gfx->setTextSize(1);
      gfx->setTextColor(C_RED);
      gfx->setCursor(x + PLANET_R + 1, y + PLANET_R - 7);
      gfx->print("R");
    }
    if (i == s_sel) gfx->drawCircle(x, y, PLANET_R + 5, C_WHITE);
  }
  s_placed = true;
}

// --- Drawing: the middle ----------------------------------------------------
static void drawCentre() {
  // The Earth: a disc with a cross, the astronomical symbol, which is also
  // the one thing on this screen that is not moving.
  gfx->fillCircle(CX, CY - 14, 9, C_EARTH);
  gfx->drawLine(CX - 9, CY - 14, CX + 9, CY - 14, C_BLACK);
  gfx->drawLine(CX, CY - 23, CX, CY - 5, C_BLACK);

  // The Moon's phase under it: lit fraction and which way it is going. On a
  // black backing, because the aspect lines cross the middle underneath.
  gfx->fillRect(CX - 40, CY - 2, 80, 48, C_BLACK);
  char buf[24];
  snprintf(buf, sizeof(buf), "%d%%", (int)lroundf(s_chart.moonIllum * 100.0f));
  {
    // The Moon glyph and the percentage side by side, centred as a pair.
    const int tw = Layout_TextW(buf, 1);
    const int total = PLANET_GLYPHS[AB_MOON].w + 4 + tw;
    const int x0 = CX - total / 2;
    drawGlyph(x0 + PLANET_GLYPHS[AB_MOON].w / 2, CY + 10, PLANET_GLYPHS[AB_MOON], C_BODY[AB_MOON]);
    gfx->setTextSize(1);
    gfx->setTextColor(C_BODY[AB_MOON]);
    gfx->setCursor(x0 + PLANET_GLYPHS[AB_MOON].w + 4, CY + 6);
    gfx->print(buf);
  }
  UI_TextCentered(s_chart.moonWaxing ? T(S_WAXING) : T(S_WANING), CY + 22, C_GRAY, 1);

  // And the ascendant in words, since in the fixed orientation it is not
  // obviously anywhere in particular.
  snprintf(buf, sizeof(buf), "ASC %s %d\xF8", zShort(Astro_SignOf(s_chart.ascLon)),
           (int)Astro_DegInSign(s_chart.ascLon));
  UI_TextCentered(buf, CY + 35, C_WHITE, 1);
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
  UI_DrawStatusLine(LY_STATUS);

  if (!Outside_TimeValid()) {
    // The chart is a function of the time and nothing else, so without the
    // clock there is genuinely nothing to draw. The clock arrives with the
    // first HTTP response of any other screen - usually within seconds.
    UI_TextCentered(T(S_PLANETS), CY - 20, C_WHITE, 2);
    UI_TextCentered(T(S_NO_TIME), CY + 10, C_YELLOW, 2);
    s_placed = false;
    return;
  }
  if (!s_chartOk) recompute();

  drawZodiac();
  drawAxes();
  drawAspects();
  drawPlanets();
  drawCentre();
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
  return false;
}

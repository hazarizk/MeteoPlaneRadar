// =============================================================================
//  MeteoPlaneRadar
//  Astro - where the planets are, as seen from the Earth - interface.
//
//  The planets screen needs one thing: the GEOCENTRIC ECLIPTIC LONGITUDE of
//  each body for "now", which is exactly what an astrological chart plots on
//  its wheel. Everything here is computed on the device from Keplerian
//  elements - no network, no ephemeris file, no key. The method is Paul
//  Schlyter's "How to compute planetary positions" (stjarnhimlen.se/comp/
//  ppcomp.html), including the main perturbation terms for the Moon, Jupiter,
//  Saturn and Uranus, and his series for Pluto. Accuracy is a few arc minutes
//  for the planets and under ten for the Moon, which on a 480-pixel wheel
//  where one degree is three pixels is far more than can be drawn. Valid for
//  roughly 1900-2100; the firmware will not see either end.
//
//  LONGITUDES ARE TROPICAL (ecliptic of date, 0 = the vernal equinox), which
//  is what Western astrology and every planetarium app use. The twelve signs
//  are simply thirty-degree slices of that circle starting at Aries - they
//  are NOT the constellations of the same name, which drifted away from the
//  signs by about 24 degrees over the last two thousand years.
//
//  Time comes from time(nullptr), which is a correct UTC epoch as soon as the
//  first HTTP response has arrived (see Outside.h). Nothing here uses local
//  time: the sky does not care about time zones.
//
//  Project: MeteoPlaneRadar - live aircraft radar on a round touchscreen
//  Author:  Petr / chiptron.cz   (vyvoj / development: chiptron.cz)
//  Web:     https://chiptron.cz
// =============================================================================
#pragma once
#include <Arduino.h>
#include <time.h>

// The bodies, in the order the screen draws and lists them. The Sun and the
// Moon count as "planets" here, as they do in astrology. The lunar node is the
// point where the Moon's orbit crosses the ecliptic going north - not a body,
// but every chart marks it, so it rides along as the last entry.
enum AstroBody : uint8_t {
  AB_SUN = 0, AB_MOON, AB_MERCURY, AB_VENUS, AB_MARS,
  AB_JUPITER, AB_SATURN, AB_URANUS, AB_NEPTUNE, AB_PLUTO,
  AB_NODE,
  AB_COUNT
};

struct AstroPos {
  float lon;        // geocentric ecliptic longitude, degrees 0..360 (tropical)
  float lat;        // geocentric ecliptic latitude, degrees
  float distAu;     // distance from the Earth in AU (the Moon too, for uniformity)
  float dailyDeg;   // change of longitude per day, degrees; negative = retrograde
  float elongDeg;   // angular distance from the Sun along the ecliptic, 0..180
  bool  retro;      // dailyDeg < 0
  // Where it is in the sky at the device's location: altitude above the
  // horizon (negative = below) and azimuth from north through east. These are
  // the only numbers here that depend on where the device stands.
  float altDeg;
  float azDeg;
};

// Everything the screen shows at once. One call per redraw is cheap - a few
// hundred trig evaluations.
struct AstroChart {
  time_t   when;
  AstroPos body[AB_COUNT];
  float    ascLon;      // ascendant: the ecliptic degree rising in the east
  float    mcLon;       // medium coeli: the ecliptic degree on the meridian
  float    moonIllum;   // 0..1 fraction of the Moon's disc that is lit
  bool     moonWaxing;  // true between new and full
  bool     daylight;    // the Sun is up: nothing but the Moon can be seen
};

// Compute the chart for a UTC epoch at a place. Latitude and longitude are only
// used for the ascendant and the MC; the planets are the same everywhere on
// Earth to the precision used here.
void Astro_Compute(time_t utc, double latDeg, double lonDeg, AstroChart* out);

// --- Helpers for the screen -------------------------------------------------
// Sign index 0..11 (0 = Aries) and the degree within it for a longitude.
static inline int   Astro_SignOf(float lon) { int s = (int)(lon / 30.0f); return ((s % 12) + 12) % 12; }
static inline float Astro_DegInSign(float lon) { float d = fmodf(lon, 30.0f); return d < 0 ? d + 30.0f : d; }

// Shortest signed difference a - b in degrees, -180..180.
float Astro_DeltaDeg(float a, float b);

// Static orbital facts for the detail panel. Values are mean values from the
// usual references (NASA planetary fact sheets); the Sun's row describes the
// Earth's orbit, which is what the Sun's apparent motion is.
struct AstroFacts {
  float periodDays;    // sidereal orbital period in days
  float meanDistAu;    // mean distance from the Sun in AU (the Moon: from the Earth)
  float speedKms;      // mean orbital speed in km/s
};
const AstroFacts* Astro_Facts(AstroBody b);

// =============================================================================
//  MeteoPlaneRadar
//  Astro - planetary positions from Keplerian elements. See Astro.h.
//
//  Author:  Petr / chiptron.cz   (vyvoj / development: chiptron.cz)
// =============================================================================
#include "Astro.h"
#include <math.h>

// Degrees in, degrees out. The elements are published in degrees, every
// perturbation term is in degrees, and the screen wants degrees - converting
// at the trig calls is less error-prone than converting the whole table.
static const double DEG = 0.017453292519943295;
static const double RAD = 57.29577951308232;

static inline double sind(double x) { return sin(x * DEG); }
static inline double cosd(double x) { return cos(x * DEG); }
static inline double atan2d(double y, double x) { return atan2(y, x) * RAD; }
static inline double rev(double x) { x = fmod(x, 360.0); return x < 0 ? x + 360.0 : x; }

// Schlyter's day number: days since 2000 Jan 0.0 UT, i.e. 1999-12-31 00:00 UT,
// which is epoch 946598400. Fractional days matter - the Moon moves half a
// degree an hour.
static inline double dayNumber(time_t utc) { return ((double)utc - 946598400.0) / 86400.0; }

// --- Orbital elements -------------------------------------------------------
// N  longitude of the ascending node     i  inclination
// w  argument of perihelion              a  semi-major axis (AU; Moon: Earth radii)
// e  eccentricity                        M  mean anomaly
struct Elem { double N, i, w, a, e, M; };

static Elem elemOf(AstroBody b, double d) {
  Elem o;
  switch (b) {
    case AB_SUN:   // these are really the Earth's, seen the other way round
      o = { 0.0, 0.0, 282.9404 + 4.70935e-5 * d, 1.0, 0.016709 - 1.151e-9 * d, 356.0470 + 0.9856002585 * d }; break;
    case AB_MOON:
      o = { 125.1228 - 0.0529538083 * d, 5.1454, 318.0634 + 0.1643573223 * d, 60.2666, 0.054900, 115.3654 + 13.0649929509 * d }; break;
    case AB_MERCURY:
      o = { 48.3313 + 3.24587e-5 * d, 7.0047 + 5.00e-8 * d, 29.1241 + 1.01444e-5 * d, 0.387098, 0.205635 + 5.59e-10 * d, 168.6562 + 4.0923344368 * d }; break;
    case AB_VENUS:
      o = { 76.6799 + 2.46590e-5 * d, 3.3946 + 2.75e-8 * d, 54.8910 + 1.38374e-5 * d, 0.723330, 0.006773 - 1.302e-9 * d, 48.0052 + 1.6021302244 * d }; break;
    case AB_MARS:
      o = { 49.5574 + 2.11081e-5 * d, 1.8497 - 1.78e-8 * d, 286.5016 + 2.92961e-5 * d, 1.523688, 0.093405 + 2.516e-9 * d, 18.6021 + 0.5240207766 * d }; break;
    case AB_JUPITER:
      o = { 100.4542 + 2.76854e-5 * d, 1.3030 - 1.557e-7 * d, 273.8777 + 1.64505e-5 * d, 5.20256, 0.048498 + 4.469e-9 * d, 19.8950 + 0.0830853001 * d }; break;
    case AB_SATURN:
      o = { 113.6634 + 2.38980e-5 * d, 2.4886 - 1.081e-7 * d, 339.3939 + 2.97661e-5 * d, 9.55475, 0.055546 - 9.499e-9 * d, 316.9670 + 0.0334442282 * d }; break;
    case AB_URANUS:
      o = { 74.0005 + 1.3978e-5 * d, 0.7733 + 1.9e-8 * d, 96.6612 + 3.0565e-5 * d, 19.18171 - 1.55e-8 * d, 0.047318 + 7.45e-9 * d, 142.5905 + 0.011725806 * d }; break;
    case AB_NEPTUNE:
      o = { 131.7806 + 3.0173e-5 * d, 1.7700 - 2.55e-7 * d, 272.8461 - 6.027e-6 * d, 30.05826 + 3.313e-8 * d, 0.008606 + 2.15e-9 * d, 260.2471 + 0.005995147 * d }; break;
    default:       // Pluto and the node have no Keplerian treatment here
      o = { 0, 0, 0, 0, 0, 0 }; break;
  }
  o.M = rev(o.M);
  return o;
}

// Kepler's equation. The first approximation is Schlyter's; a handful of
// Newton steps then take it to well below an arc second even for Mercury.
static double eccAnom(double M, double e) {
  double E = M + e * RAD * sind(M) * (1.0 + e * cosd(M));
  for (int k = 0; k < 8; k++) {
    double dE = (E - e * RAD * sind(E) - M) / (1.0 - e * cosd(E));
    E -= dE;
    if (fabs(dE) < 1e-6) break;
  }
  return E;
}

// True anomaly and distance from the orbit's focus.
static void anomDist(const Elem& o, double* v, double* r) {
  const double E  = eccAnom(o.M, o.e);
  const double xv = o.a * (cosd(E) - o.e);
  const double yv = o.a * sqrt(1.0 - o.e * o.e) * sind(E);
  *v = atan2d(yv, xv);
  *r = sqrt(xv * xv + yv * yv);
}

// Position in the orbit -> rectangular ecliptic coordinates about the focus.
static void toEcliptic(const Elem& o, double v, double r, double* x, double* y, double* z) {
  const double u = v + o.w;
  *x = r * (cosd(o.N) * cosd(u) - sind(o.N) * sind(u) * cosd(o.i));
  *y = r * (sind(o.N) * cosd(u) + cosd(o.N) * sind(u) * cosd(o.i));
  *z = r * sind(u) * sind(o.i);
}

// The Sun's geocentric ecliptic position. Also hands back its mean anomaly and
// argument of perihelion, which the Moon's perturbations need.
static void sunPos(double d, double* xs, double* ys, double* lonSun, double* Ms, double* ws) {
  const Elem s = elemOf(AB_SUN, d);
  double v, r;
  anomDist(s, &v, &r);
  const double lon = rev(v + s.w);
  *xs = r * cosd(lon);
  *ys = r * sind(lon);
  *lonSun = lon;
  *Ms = s.M;
  *ws = s.w;
}

// Geocentric ecliptic longitude, latitude and distance of one body at day d.
// Longitude in degrees 0..360, latitude in degrees, distance in AU.
static void bodyAt(AstroBody b, double d, double* lon, double* lat, double* dist) {
  double xs, ys, lonSun, Ms, ws;
  sunPos(d, &xs, &ys, &lonSun, &Ms, &ws);

  if (b == AB_SUN) {
    *lon = lonSun; *lat = 0.0; *dist = sqrt(xs * xs + ys * ys);
    return;
  }

  if (b == AB_NODE) {
    // The mean ascending node of the lunar orbit. Astrologers mostly use the
    // mean node rather than the true one, and the difference is under two
    // degrees anyway. It runs backwards through the zodiac, one lap in 18.6
    // years, which is why dailyDeg comes out negative for it.
    *lon = rev(elemOf(AB_MOON, d).N); *lat = 0.0; *dist = 0.0;
    return;
  }

  if (b == AB_PLUTO) {
    // No Keplerian elements: Pluto's orbit is too perturbed by Neptune for
    // them to hold. Schlyter's trigonometric series is good to about a degree
    // over 1800-2100, which on this wheel is three pixels.
    const double S = 50.03 + 0.033459652 * d;
    const double P = 238.95 + 0.003968789 * d;
    const double lh = 238.9508 + 0.00400703 * d
      - 19.799 * sind(P)     + 19.848 * cosd(P)
      +  0.897 * sind(2 * P) -  4.956 * cosd(2 * P)
      +  0.610 * sind(3 * P) +  1.211 * cosd(3 * P)
      -  0.341 * sind(4 * P) -  0.190 * cosd(4 * P)
      +  0.128 * sind(5 * P) -  0.034 * cosd(5 * P)
      -  0.038 * sind(6 * P) +  0.031 * cosd(6 * P)
      +  0.020 * sind(S - P) -  0.010 * cosd(S - P);
    const double bh = -3.9082
      - 5.453 * sind(P)     - 14.975 * cosd(P)
      + 3.527 * sind(2 * P) +  1.673 * cosd(2 * P)
      - 1.051 * sind(3 * P) +  0.328 * cosd(3 * P)
      + 0.179 * sind(4 * P) -  0.292 * cosd(4 * P)
      + 0.019 * sind(5 * P) +  0.100 * cosd(5 * P)
      - 0.031 * sind(6 * P) -  0.026 * cosd(6 * P)
      + 0.011 * cosd(S - P);
    const double rh = 40.72
      + 6.68 * sind(P)     + 6.90 * cosd(P)
      - 1.18 * sind(2 * P) - 0.03 * cosd(2 * P)
      + 0.15 * sind(3 * P) - 0.14 * cosd(3 * P);
    const double xh = rh * cosd(lh) * cosd(bh);
    const double yh = rh * sind(lh) * cosd(bh);
    const double zh = rh * sind(bh);
    const double xg = xh + xs, yg = yh + ys, zg = zh;
    *lon = rev(atan2d(yg, xg));
    *lat = atan2d(zg, sqrt(xg * xg + yg * yg));
    *dist = sqrt(xg * xg + yg * yg + zg * zg);
    return;
  }

  const Elem o = elemOf(b, d);
  double v, r;
  anomDist(o, &v, &r);
  double xh, yh, zh;
  toEcliptic(o, v, r, &xh, &yh, &zh);

  if (b == AB_MOON) {
    // Already geocentric (the Moon orbits the Earth), in Earth radii. The
    // perturbations are the ones Schlyter lists; without them the Moon can be
    // a degree and a half out, which is three times its own diameter.
    double lm = rev(atan2d(yh, xh));
    double bm = atan2d(zh, sqrt(xh * xh + yh * yh));
    double rm = r;

    const double Mm = o.M, Nm = o.N, wm = o.w;
    const double Ls = rev(Ms + ws);            // Sun's mean longitude
    const double Lm = rev(Mm + wm + Nm);       // Moon's mean longitude
    const double D  = Lm - Ls;                 // mean elongation
    const double F  = Lm - Nm;                 // argument of latitude

    lm += -1.274 * sind(Mm - 2 * D)
        +  0.658 * sind(2 * D)
        -  0.186 * sind(Ms)
        -  0.059 * sind(2 * Mm - 2 * D)
        -  0.057 * sind(Mm - 2 * D + Ms)
        +  0.053 * sind(Mm + 2 * D)
        +  0.046 * sind(2 * D - Ms)
        +  0.041 * sind(Mm - Ms)
        -  0.035 * sind(D)
        -  0.031 * sind(Mm + Ms)
        -  0.015 * sind(2 * F - 2 * D)
        +  0.011 * sind(Mm - 4 * D);
    bm += -0.173 * sind(F - 2 * D)
        -  0.055 * sind(Mm - F - 2 * D)
        -  0.046 * sind(Mm + F - 2 * D)
        +  0.033 * sind(F + 2 * D)
        +  0.017 * sind(2 * Mm + F);
    rm += -0.58 * cosd(Mm - 2 * D)
        -  0.46 * cosd(2 * D);

    *lon = rev(lm);
    *lat = bm;
    *dist = rm * 6378.137 / 149597870.7;      // Earth radii -> AU
    return;
  }

  // Heliocentric longitude/latitude, then the perturbations the outer planets
  // exert on one another. The terms are small but the Jupiter-Saturn ones are
  // most of a degree, which is a visible shift on the wheel.
  double lh = rev(atan2d(yh, xh));
  double bh = atan2d(zh, sqrt(xh * xh + yh * yh));
  if (b == AB_JUPITER || b == AB_SATURN || b == AB_URANUS) {
    const double Mj = elemOf(AB_JUPITER, d).M;
    const double Msa = elemOf(AB_SATURN, d).M;
    const double Mu = elemOf(AB_URANUS, d).M;
    if (b == AB_JUPITER) {
      lh += -0.332 * sind(2 * Mj - 5 * Msa - 67.6)
          -  0.056 * sind(2 * Mj - 2 * Msa + 21)
          +  0.042 * sind(3 * Mj - 5 * Msa + 21)
          -  0.036 * sind(Mj - 2 * Msa)
          +  0.022 * cosd(Mj - Msa)
          +  0.023 * sind(2 * Mj - 3 * Msa + 52)
          -  0.016 * sind(Mj - 5 * Msa - 69);
    } else if (b == AB_SATURN) {
      lh +=  0.812 * sind(2 * Mj - 5 * Msa - 67.6)
          -  0.229 * cosd(2 * Mj - 4 * Msa - 2)
          +  0.119 * sind(Mj - 2 * Msa - 3)
          +  0.046 * sind(2 * Mj - 6 * Msa - 69)
          +  0.014 * sind(Mj - 3 * Msa + 32);
      bh += -0.020 * cosd(2 * Mj - 4 * Msa - 2)
          +  0.018 * sind(2 * Mj - 6 * Msa - 49);
    } else {
      lh +=  0.040 * sind(Msa - 2 * Mu + 6)
          +  0.035 * sind(Msa - 3 * Mu + 33)
          -  0.015 * sind(Mj - Mu + 20);
    }
    xh = r * cosd(lh) * cosd(bh);
    yh = r * sind(lh) * cosd(bh);
    zh = r * sind(bh);
  }

  // Heliocentric -> geocentric: add the Sun's position.
  const double xg = xh + xs, yg = yh + ys, zg = zh;
  *lon = rev(atan2d(yg, xg));
  *lat = atan2d(zg, sqrt(xg * xg + yg * yg));
  *dist = sqrt(xg * xg + yg * yg + zg * zg);
}

float Astro_DeltaDeg(float a, float b) {
  float d = fmodf(a - b, 360.0f);
  if (d > 180.0f)  d -= 360.0f;
  if (d < -180.0f) d += 360.0f;
  return d;
}

// Ascendant and MC for a place. Greenwich sidereal time from the standard
// linear expression (good to a few seconds of time, far more than the half
// degree the wheel can show); the ascendant from the usual spherical
// trigonometry. Latitudes within a degree of the poles are clamped - the
// formula degenerates there and nobody runs this device at 89.9 N.
struct Observer { double lat, lst, eps; };   // degrees

static Observer observerAt(time_t utc, double latDeg, double lonDeg) {
  Observer o;
  const double jd   = (double)utc / 86400.0 + 2440587.5;
  const double gmst = rev(280.46061837 + 360.98564736629 * (jd - 2451545.0));
  o.lst = rev(gmst + lonDeg);                      // local sidereal time
  o.eps = 23.4393 - 3.563e-7 * dayNumber(utc);     // obliquity of the ecliptic
  if (latDeg >  89.0) latDeg =  89.0;
  if (latDeg < -89.0) latDeg = -89.0;
  o.lat = latDeg;
  return o;
}

static void ascMc(const Observer& o, float* asc, float* mc) {
  const double y = cosd(o.lst);
  const double x = -(sind(o.lst) * cosd(o.eps) + tan(o.lat * DEG) * sind(o.eps));
  *asc = (float)rev(atan2d(y, x));
  *mc  = (float)rev(atan2d(sind(o.lst), cosd(o.lst) * cosd(o.eps)));
}

// Ecliptic longitude/latitude -> altitude and azimuth for the observer.
// Through equatorial coordinates and the hour angle, the textbook way. No
// refraction and no parallax: a degree on the horizon is of no consequence
// on a chart, and the question being answered is "is it up", not "where
// exactly do I point the telescope".
static void altAz(const Observer& o, double lon, double lat, float* alt, float* az) {
  const double xe = cosd(lon) * cosd(lat);
  const double ye = sind(lon) * cosd(lat) * cosd(o.eps) - sind(lat) * sind(o.eps);
  const double ze = sind(lon) * cosd(lat) * sind(o.eps) + sind(lat) * cosd(o.eps);
  const double ra  = atan2d(ye, xe);
  const double dec = atan2d(ze, sqrt(xe * xe + ye * ye));
  const double ha  = o.lst - ra;
  const double sinAlt = sind(dec) * sind(o.lat) + cosd(dec) * cosd(o.lat) * cosd(ha);
  *alt = (float)(asin(sinAlt) * RAD);
  // Azimuth from north through east.
  const double y = -sind(ha) * cosd(dec);
  const double x = sind(dec) * cosd(o.lat) - cosd(dec) * sind(o.lat) * cosd(ha);
  *az = (float)rev(atan2d(y, x));
}

void Astro_Compute(time_t utc, double latDeg, double lonDeg, AstroChart* out) {
  if (!out) return;
  out->when = utc;
  const double d = dayNumber(utc);
  const Observer obs = observerAt(utc, latDeg, lonDeg);

  double sunLon = 0;
  for (int i = 0; i < AB_COUNT; i++) {
    double lon, lat, dist;
    bodyAt((AstroBody)i, d, &lon, &lat, &dist);
    altAz(obs, lon, lat, &out->body[i].altDeg, &out->body[i].azDeg);
    // Central difference over one day for the daily motion. A day is short
    // enough that even the Moon's thirteen degrees stay far from wrapping.
    double l0, l1, dummy;
    bodyAt((AstroBody)i, d - 0.5, &l0, &dummy, &dummy);
    bodyAt((AstroBody)i, d + 0.5, &l1, &dummy, &dummy);
    AstroPos& p = out->body[i];
    p.lon = (float)lon;
    p.lat = (float)lat;
    p.distAu = (float)dist;
    p.dailyDeg = Astro_DeltaDeg((float)l1, (float)l0);
    p.retro = p.dailyDeg < 0 && i != AB_NODE;   // the node always runs backwards
    if (i == AB_SUN) sunLon = lon;
    p.elongDeg = fabsf(Astro_DeltaDeg((float)lon, (float)sunLon));
  }

  // The Moon's phase from its elongation. A signed difference says which side
  // of the Sun it is on: ahead of the Sun in longitude it is waxing.
  {
    const float e = Astro_DeltaDeg(out->body[AB_MOON].lon, out->body[AB_SUN].lon);
    out->moonIllum = (1.0f - cosf(e * (float)DEG)) * 0.5f;
    out->moonWaxing = e > 0;
  }

  ascMc(obs, &out->ascLon, &out->mcLon);
  // "Daylight" starts a little before the Sun's own rise: at -6 degrees
  // (civil twilight) the sky is already too bright for anything but the Moon
  // and Venus, and that is the question this flag answers.
  out->daylight = out->body[AB_SUN].altDeg > -6.0f;
}

const AstroFacts* Astro_Facts(AstroBody b) {
  static const AstroFacts F[AB_COUNT] = {
    //  period d    mean AU   km/s
    {   365.256f,   1.000f,  29.78f },   // Sun (the Earth's orbit)
    {    27.322f,   0.00257f, 1.022f },  // Moon (about the Earth)
    {    87.969f,   0.387f,  47.36f },   // Mercury
    {   224.701f,   0.723f,  35.02f },   // Venus
    {   686.980f,   1.524f,  24.07f },   // Mars
    {  4332.59f,    5.203f,  13.06f },   // Jupiter
    { 10759.2f,     9.537f,   9.68f },   // Saturn
    { 30688.5f,    19.19f,    6.80f },   // Uranus
    { 60182.0f,    30.07f,    5.43f },   // Neptune
    { 90560.0f,    39.48f,    4.67f },   // Pluto
    {  6798.4f,     0.0f,     0.0f  },   // Node: one retrograde lap in 18.6 years
  };
  if ((int)b < 0 || b >= AB_COUNT) return &F[0];
  return &F[b];
}

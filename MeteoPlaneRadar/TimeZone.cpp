// =============================================================================
//  MeteoPlaneRadar
//  Local time zone. See TimeZone.h.
//
//  Author:  Petr / chiptron.cz   (vyvoj / development: chiptron.cz)
// =============================================================================
#include "TimeZone.h"
#include "TzHistory.h"
#include "Settings.h"
#include "Config.h"

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static char s_tz[24] = "";
static bool s_fromNet = false;

const char* TimeZone_Text()    { return s_tz; }
bool        TimeZone_FromNetwork() { return s_fromNet; }

static void applyTz(const char* tz) {
  strncpy(s_tz, tz, sizeof(s_tz) - 1);
  s_tz[sizeof(s_tz) - 1] = '\0';
  setenv("TZ", s_tz, 1);
  tzset();
}

void TimeZone_Begin() {
  s_fromNet = false;
  applyTz(TZ_INFO);
}

int TimeZone_Offset() {
  // Ask the C library rather than keeping our own copy: with the compiled-in
  // rule in force the answer depends on the date, and only the library knows
  // whether today is summer time.
  //
  // The obvious way to read it is tm_gmtoff, but that field is a BSD/glibc
  // extension and the newlib shipped with the ESP32 core does not have it -
  // it compiles on a PC and fails on the device. So take the long way round:
  // break the SAME instant into a local and a UTC calendar and subtract them.
  // That works on every C library, because it uses nothing but the fields the
  // standard guarantees.
  const time_t now = time(nullptr);
  struct tm lt, gt;
  localtime_r(&now, &lt);
  gmtime_r(&now, &gt);

  // Day difference. tm_yday jumps by a whole year across New Year's Eve, so
  // the year fields decide the sign there and the result is clamped to the
  // one day an offset can ever span.
  int days = lt.tm_yday - gt.tm_yday;
  if (lt.tm_year != gt.tm_year) days = (lt.tm_year > gt.tm_year) ? 1 : -1;
  else if (days >  1)          days = -1;
  else if (days < -1)          days =  1;

  return days * 86400
       + (lt.tm_hour - gt.tm_hour) * 3600
       + (lt.tm_min  - gt.tm_min)  * 60
       + (lt.tm_sec  - gt.tm_sec);
}

// --- Any other day ----------------------------------------------------------
// The last change at or before utc, by binary search over TzHistory.h.
static int historyOffset(time_t utc) {
  int lo = 0, hi = TZ_HISTORY_N - 1;
  while (lo < hi) {
    const int mid = (lo + hi + 1) / 2;
    if (TZ_HISTORY[mid].at <= (int64_t)utc) lo = mid; else hi = mid - 1;
  }
  return TZ_HISTORY[lo].off;
}

// Does the history describe where the device stands? It is Prague's. The
// modern EU rule is the same from Madrid to Warsaw, and the older history is
// shared with the neighbours closely enough (Vienna and Bratislava had most of
// it), so it serves any device in Central European Time: in Europe by its
// coordinates, and agreeing with the offset that is in force right now. A
// device in New York, or in Lisbon, gets the plain current offset instead.
static bool historyApplies() {
  const double lat = Settings_Lat(), lon = Settings_Lon();
  if (lat < 34.0 || lat > 72.0 || lon < -10.0 || lon > 32.0) return false;
  return historyOffset(time(nullptr)) == TimeZone_Offset();
}

int TimeZone_OffsetAt(time_t utc) {
  if (!historyApplies()) return TimeZone_Offset();
  if ((int64_t)utc < TZ_HISTORY[0].at) return TZ_HISTORY[0].off;
  return historyOffset(utc);
}

time_t TimeZone_LocalToUtc(time_t localAsUtc) {
  // A change of the clock within a day of the reading gives two offsets to
  // try; a reading is a valid UTC instant under an offset when that offset
  // is the one in force at the instant it produces.
  const int oBefore = TimeZone_OffsetAt(localAsUtc - 86400);
  const int oAfter  = TimeZone_OffsetAt(localAsUtc + 86400);
  const time_t uB = localAsUtc - oBefore, uA = localAsUtc - oAfter;
  const bool vB = TimeZone_OffsetAt(uB) == oBefore;
  const bool vA = TimeZone_OffsetAt(uA) == oAfter;
  if (vB && vA) return uB < uA ? uB : uA;   // the hour that comes twice: the earlier one
  if (vB) return uB;
  if (vA) return uA;
  return uB;                                 // the hour that is skipped: an hour later on the wall
}

void TimeZone_SetOffset(int seconds) {
  // Sanity. Real offsets run from -12:00 to +14:00; anything else is a parse
  // accident and must not be allowed to move the clock.
  if (seconds < -12 * 3600 || seconds > 14 * 3600) {
    Serial.printf("CAS: ignoruji nesmyslny posun %d s\n", seconds);
    return;
  }

  // Already right? Then say nothing and - importantly - leave the compiled-in
  // rule in place, so a Czech device keeps real DST dates instead of a frozen
  // offset that would be an hour out for a few weeks after each changeover.
  if (TimeZone_Offset() == seconds) return;

  // POSIX counts WEST as positive, which is the opposite of everyone else, so
  // the sign flips here exactly once. UTC+2 in Prague becomes the string
  // "UTC-2:00", and New York's UTC-5 becomes "UTC+5:00".
  const int west = -seconds;
  const char sign = (west < 0) ? '-' : '+';
  const int mag = (west < 0) ? -west : west;

  char tz[24];
  snprintf(tz, sizeof(tz), "UTC%c%d:%02d", sign, mag / 3600, (mag % 3600) / 60);
  applyTz(tz);
  s_fromNet = true;
  Serial.printf("CAS: posun %+d s, TZ=\"%s\"\n", seconds, s_tz);
}

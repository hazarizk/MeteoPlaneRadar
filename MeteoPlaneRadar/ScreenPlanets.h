// =============================================================================
//  MeteoPlaneRadar
//  Screen: the planets - a geocentric chart of where everything is right now,
//  drawn the way an astrological chart draws it - interface.
//
//  The round panel is, again, the reason this works. The ecliptic is a circle,
//  so the zodiac is a ring of twelve thirty-degree signs, the Earth is in the
//  middle, and each planet sits on the ring at its longitude - which is both
//  what a horoscope wheel shows and what you would actually see if you could
//  look along the ecliptic from the Earth. Everything is computed on the
//  device from orbital elements (Astro.*), so this screen needs the clock and
//  nothing else: no network request, no API, no key.
//
//  What is on it:
//    - the zodiac ring, the four elements in four colours, degree ticks
//    - the planets as their classic glyphs (AstroGlyphs.h), colour-coded,
//      with a pointer to their exact degree; a red R next to one means it is
//      retrograde; a dim one is below the horizon at the device's location
//    - the ascendant/descendant and MC/IC axes for the device's location
//    - aspect lines between planets within two degrees of exact
//      (conjunction is a stacking, the others are drawn), colour by aspect;
//      tap the middle to hide them
//    - the Moon in the middle, drawn with its phase as it looks tonight
//
//  Controls:
//    tap a planet       - its detail: sign and degree, altitude and compass
//                         direction (or "below the horizon"), distance, daily motion,
//                         orbital period, mean distance from the Sun, speed,
//                         elongation (the Moon: phase)
//    tap the middle     - aspect lines on/off
//    tap elsewhere      - close the detail
//    swipe              - rotate the wheel: ascendant on the left (the way a
//                         chart is drawn) or 0 Aries on the left (fixed)
//    tap the wheel      - the time chips at the foot come up (and go away):
//                         < NOW >, under them the step - hour, day, month,
//                         year. An arrow steps the wheel in time; held, it
//                         runs. NOW brings it back. The chips hide by
//                         themselves after a minute, the wheel comes back to
//                         now after ten minutes alone or on leaving the
//                         screen, so the display never stays parked in 1987.
//
//  Time travel reaches from 1800 to 2100 - the range the orbital elements in
//  Astro.* are good for. The web page can set a moment outright (a birth
//  chart is the obvious use); the chips are for wandering from there.
//
//  Project: MeteoPlaneRadar - live aircraft radar on a round touchscreen
//  Author:  Petr / chiptron.cz   (vyvoj / development: chiptron.cz)
//  Web:     https://chiptron.cz
// =============================================================================
#pragma once
#include <Arduino.h>
#include <time.h>

void ScreenPlanets_Enter();
void ScreenPlanets_Draw();
bool ScreenPlanets_Tick();                    // true = needs a redraw
bool ScreenPlanets_HandleTap(int x, int y);

// Swipe: wheel orientation. Named like the radar screens' range so the screen
// manager can treat it the same way.
void ScreenPlanets_ChangeRange(int dir);

// "ASC vlevo" / "Beran vlevo" for the web remote control's range readout.
void ScreenPlanets_RangeText(char* out, size_t cap);

// The detail panel is a modal like the aircraft detail: while it is open a
// swipe or a long press closes it instead of acting, and the automatic
// cycling waits.
bool ScreenPlanets_DetailOpen();
void ScreenPlanets_CloseDetail();

// --- Time travel ------------------------------------------------------------
// "Browsing" is the wheel showing any moment but now. While it does, the
// automatic cycling waits, like it does for an open detail.
bool ScreenPlanets_Browsing();
// Set the wheel to a UTC moment (clamped to 1800-2100) or back to now. From
// the web; the chips on the glass do the same through HandleTap/HandleHold.
void ScreenPlanets_SetTime(time_t utc);
void ScreenPlanets_SetNow();
// The moment shown, in the device's local time as "YYYY-MM-DDTHH:MM" for the
// web page's date field - or "" when the wheel shows now.
void ScreenPlanets_BrowseIso(char* out, size_t cap);
// A finger resting on the glass, called every loop while it is down with the
// current position and how long it has been there. On an arrow chip this runs
// the time. True = the screen has taken the touch over: the release must not
// be turned into a tap or a long press.
bool ScreenPlanets_HandleHold(int x, int y, unsigned long heldMs);

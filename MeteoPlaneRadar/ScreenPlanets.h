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
//    - the planets, colour-coded, with a pointer to their exact degree; a red
//      ring around one means it is retrograde; a hollow disc means it is
//      below the horizon at the device's location right now
//    - the ascendant/descendant and MC/IC axes for the device's location
//    - aspect lines between planets (conjunction is a stacking, the others
//      are drawn), colour by aspect; tap the middle to hide them
//    - the Moon's phase in the middle
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
//
//  Project: MeteoPlaneRadar - live aircraft radar on a round touchscreen
//  Author:  Petr / chiptron.cz   (vyvoj / development: chiptron.cz)
//  Web:     https://chiptron.cz
// =============================================================================
#pragma once
#include <Arduino.h>

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

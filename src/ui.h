#pragma once

#include <stdint.h>
#include "vfd.h"

void uiBegin(uint8_t page, uint8_t font, uint8_t viz, uint8_t clk);
void uiNextPage();
void uiPrevPage();
void uiSetPage(uint8_t idx);
void uiNextFont();
void uiPrevFont();
void uiNextViz();
void uiPrevViz();
void uiNextClock();
void uiPrevClock();
void uiNextClockFont();
void uiPrevClockFont();
uint8_t uiPageIndex();
uint8_t uiFontIndex();
uint8_t uiVizIndex();
uint8_t uiClockIndex();
uint8_t uiClockFontIndex();
const char* uiPageName(uint8_t idx);     // "OVERVIEW", "TIME", …

void uiSet12h(bool on);
void uiSetClockFont(uint8_t idx);
bool uiIs12h();

void uiSetDashGlitch(bool on);
bool uiIsDashGlitch();

// Matrix-page brightness mode timeout indicator: 0 hides the bar, otherwise
// 0..1 fraction of timeout remaining. Main feeds this each frame.
void uiSetMatrixBrightProgress(float p);

// Manually fire one of the cat's activities by name (e.g. "meow", "nap",
// "chandelier"). Returns true when the name was recognised.
bool uiTriggerCatAction(const char* name);

void uiRender(VFD& vfd);                              // draw the active page
void uiBoot(VFD& vfd, const char* msg);               // splash during startup

// Register the live VFD so callback-driven render paths (notably OTA
// progress) can update the screen without going through uiRender().
void uiSetDisplay(VFD& vfd);
void uiOtaTick();                                     // refresh OTA splash now

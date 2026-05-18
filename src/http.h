#pragma once

#include "vfd.h"

// Minimal HTTP server that serves the current u8g2 framebuffer as a 1-bit
// BMP at /screenshot.bmp. Used for grabbing screenshots without USB.
void httpBegin(VFD& vfd);
void httpLoop();

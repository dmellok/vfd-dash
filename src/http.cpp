#include "http.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>

static WiFiServer s_server(80);
static VFD*       s_vfd     = nullptr;
static bool       s_running = false;

void httpBegin(VFD& vfd) {
    s_vfd = &vfd;
    s_server.begin();
    s_running = true;
}

// Write a minimal 1-bit BMP header (14 + 40 + 8 = 62 bytes) for a w×h image.
static void writeBmpHeader(WiFiClient& c, int w, int h) {
    const int rowBytes      = ((w + 31) / 32) * 4;     // 4-byte aligned
    const uint32_t pxSize   = (uint32_t)rowBytes * h;
    const uint32_t pxOffset = 14 + 40 + 8;
    const uint32_t fileSize = pxOffset + pxSize;

    uint8_t hdr[14 + 40 + 8] = {0};
    // BITMAPFILEHEADER ---------------------------------------------------
    hdr[0]  = 'B';  hdr[1]  = 'M';
    hdr[2]  = fileSize        & 0xff;
    hdr[3]  = (fileSize >> 8) & 0xff;
    hdr[4]  = (fileSize >> 16) & 0xff;
    hdr[5]  = (fileSize >> 24) & 0xff;
    hdr[10] = pxOffset        & 0xff;
    hdr[11] = (pxOffset >> 8) & 0xff;

    // BITMAPINFOHEADER ---------------------------------------------------
    hdr[14] = 40;                                       // header size
    hdr[18] = w        & 0xff;
    hdr[19] = (w >> 8) & 0xff;
    hdr[22] = h        & 0xff;                          // positive = bottom-up
    hdr[23] = (h >> 8) & 0xff;
    hdr[26] = 1;                                        // planes
    hdr[28] = 1;                                        // bits per pixel
    hdr[46] = 2;                                        // colors used

    // Palette: index 0 = black, index 1 = white (BGRA) -------------------
    hdr[54] = 0x00; hdr[55] = 0x00; hdr[56] = 0x00; hdr[57] = 0x00;  // black
    hdr[58] = 0xFF; hdr[59] = 0xFF; hdr[60] = 0xFF; hdr[61] = 0x00;  // white
    c.write(hdr, sizeof(hdr));
}

// u8g2's full-buffer layout: 256 columns × ceil(H/8) "pages" of 8 vertical
// pixels each. byte buf[page * 256 + col]; bit n = pixel at y=page*8+n.
// BMP wants row-major, MSB-leftmost, bottom-up.
static void writeBmpPixels(WiFiClient& c, VFD& vfd) {
    constexpr int W = 256;
    constexpr int H = 50;
    constexpr int rowBytes = W / 8;                     // 32, 4-byte aligned

    uint8_t* buf = vfd.getBufferPtr();
    uint8_t  row[rowBytes];
    for (int r = H - 1; r >= 0; --r) {
        memset(row, 0, rowBytes);
        const int page = r >> 3;
        const int bit  = r & 7;
        for (int x = 0; x < W; ++x) {
            if ((buf[page * W + x] >> bit) & 1) {
                row[x >> 3] |= (uint8_t)(1 << (7 - (x & 7)));
            }
        }
        c.write(row, rowBytes);
    }
}

static void serveScreenshot(WiFiClient& c) {
    c.print(F("HTTP/1.1 200 OK\r\n"
              "Content-Type: image/bmp\r\n"
              "Content-Length: 1662\r\n"
              "Cache-Control: no-store\r\n"
              "Connection: close\r\n\r\n"));
    writeBmpHeader(c, 256, 50);
    writeBmpPixels(c, *s_vfd);
}

static void serveIndex(WiFiClient& c) {
    static const char* body =
        "<!doctype html><meta charset=utf-8>"
        "<title>VFD HUD</title>"
        "<body style='font-family:system-ui;padding:24px;'>"
        "<h1>VFD HUD</h1>"
        "<p>Live screenshot: <a href=/screenshot.bmp>/screenshot.bmp</a></p>"
        "</body>";
    c.print(F("HTTP/1.1 200 OK\r\n"
              "Content-Type: text/html\r\n"
              "Connection: close\r\n\r\n"));
    c.print(body);
}

void httpLoop() {
    if (!s_running || !s_vfd) return;
    WiFiClient client = s_server.accept();
    if (!client) return;

    // Read the request line; drain the rest of the headers so the client
    // doesn't see a RST when we close.
    String reqLine = client.readStringUntil('\n');
    while (client.connected() && client.available()) {
        String line = client.readStringUntil('\n');
        if (line.length() <= 1) break;                  // blank → end of headers
    }

    if (reqLine.startsWith("GET /screenshot")) {
        serveScreenshot(client);
    } else if (reqLine.startsWith("GET / ") || reqLine.startsWith("GET /\r")) {
        serveIndex(client);
    } else {
        client.print(F("HTTP/1.1 404 Not Found\r\n"
                       "Connection: close\r\n\r\n"));
    }
    client.flush();
    client.stop();
}

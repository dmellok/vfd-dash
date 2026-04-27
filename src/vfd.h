#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

// Pin map — INBN0BV1287UD carrier wired to a Pico W.
// Share GND with the carrier; power the carrier separately (4.5–20V, ≥2A).
static constexpr uint8_t PIN_VFD_FIL_EN = 2;
static constexpr uint8_t PIN_VFD_RESET  = 6;
static constexpr uint8_t PIN_VFD_DATA   = 7;
static constexpr uint8_t PIN_VFD_CS     = 8;
static constexpr uint8_t PIN_VFD_CLK    = 9;

// GP1287BI uses 8-bit SPI, LSB-first (per datasheet). U8g2's stock software
// SPI byte drivers are MSB-first, so we bit-bang it ourselves.
extern "C" inline uint8_t vfd_byte_sw_spi_lsb(u8x8_t *u8x8, uint8_t msg,
                                              uint8_t arg_int, void *arg_ptr) {
    switch (msg) {
    case U8X8_MSG_BYTE_INIT:
        pinMode(PIN_VFD_CS,   OUTPUT);
        pinMode(PIN_VFD_CLK,  OUTPUT);
        pinMode(PIN_VFD_DATA, OUTPUT);
        digitalWrite(PIN_VFD_CS,  HIGH);
        digitalWrite(PIN_VFD_CLK, HIGH);    // SPI mode 3: clock idles high
        break;

    case U8X8_MSG_BYTE_SEND: {
        uint8_t *data = (uint8_t *)arg_ptr;
        while (arg_int-- > 0) {
            uint8_t b = *data++;
            for (uint8_t i = 0; i < 8; ++i) {
                digitalWrite(PIN_VFD_CLK,  LOW);
                digitalWrite(PIN_VFD_DATA, b & 1);
                b >>= 1;
                digitalWrite(PIN_VFD_CLK,  HIGH);
            }
        }
        break;
    }

    case U8X8_MSG_BYTE_START_TRANSFER: digitalWrite(PIN_VFD_CS, LOW);  break;
    case U8X8_MSG_BYTE_END_TRANSFER:   digitalWrite(PIN_VFD_CS, HIGH); break;
    default: return 0;
    }
    return 1;
}

class VFD : public U8G2 {
public:
    VFD() {
        u8g2_Setup_futaba_vfd_gp1287ai_256x50_f(
            &u8g2, U8G2_R0,
            vfd_byte_sw_spi_lsb,
            u8x8_gpio_and_delay_arduino);
        u8x8_SetPin_4Wire_SW_SPI(getU8x8(),
            PIN_VFD_CLK, PIN_VFD_DATA, PIN_VFD_CS,
            U8X8_PIN_NONE,
            PIN_VFD_RESET);
    }

    void powerOnInit() {
        pinMode(PIN_VFD_FIL_EN, OUTPUT);
        pinMode(PIN_VFD_RESET,  OUTPUT);
        digitalWrite(PIN_VFD_RESET,  LOW);
        digitalWrite(PIN_VFD_FIL_EN, HIGH);   // active-HIGH per datasheet
        delay(500);                           // filament warm-up
        digitalWrite(PIN_VFD_RESET,  HIGH);
        delay(50);
        begin();
        setContrast(128);
    }
};

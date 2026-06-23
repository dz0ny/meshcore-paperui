#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <RadioLib.h>
#include <FS.h>
#include <SD.h>
#include <EEPROM.h>
#include <SPIFFS.h>

// Software version (shared)
#define T_PAPER_SW_VERSION    "v0.2.0"

#ifndef T_PAPER_GIT_HASH
#define T_PAPER_GIT_HASH "unknown"
#endif

#define T_PAPER_FW_VERSION T_PAPER_SW_VERSION " (" T_PAPER_GIT_HASH ")"

// ---------- Peripheral status enum (shared) ----------

enum {
    E_PERI_INK_POWER = 0,
    E_PERI_BQ25896,
    E_PERI_BQ27220,
    E_PERI_RTC,
    E_PERI_TOUCH,
    E_PERI_LORA,
    E_PERI_SD_CARD,
    E_PERI_GPS,
    E_PERI_WIFI,
    E_PERI_DISPLAY,
    E_PERI_KEYBOARD,
    E_PERI_TRACKBALL,
    E_PERI_MAX,
};

static inline const char* peri_name(int idx) {
    switch (idx) {
        case E_PERI_INK_POWER: return "ink_power";
        case E_PERI_BQ25896: return "bq25896";
        case E_PERI_BQ27220: return "bq27220";
        case E_PERI_RTC: return "rtc";
        case E_PERI_TOUCH: return "touch";
        case E_PERI_LORA: return "lora";
        case E_PERI_SD_CARD: return "sd_card";
        case E_PERI_GPS: return "gps";
        case E_PERI_WIFI: return "wifi";
        case E_PERI_DISPLAY: return "display";
        case E_PERI_KEYBOARD: return "keyboard";
        case E_PERI_TRACKBALL: return "trackball";
        default: return "unknown";
    }
}

// ---------- Task priorities (shared) ----------

#define GPS_PRIORITY     (configMAX_PRIORITIES - 1)
#define LORA_PRIORITY    (configMAX_PRIORITIES - 2)
#define BATTERY_PRIORITY (configMAX_PRIORITIES - 4)
#define BUTTON_PRIORITY  (configMAX_PRIORITIES - 5)

// ---------- Board namespace — common interface ----------

namespace board {

extern bool peri_status[E_PERI_MAX];

void init();
void seed_clock_from_rtc();

// Global I2C mutex
extern SemaphoreHandle_t i2c_mutex;

// Home button flag
extern volatile bool home_button_pressed;

// Battery
bool battery_is_charging();
uint16_t battery_percent();
uint16_t battery_voltage_mv();
int16_t  battery_current_ma();
uint16_t battery_temperature();
uint16_t battery_full_capacity();
uint16_t battery_design_capacity();
uint16_t battery_remain_capacity();
uint16_t battery_health();

// Charger (BQ25896)
bool     charger_is_valid();
bool     charger_vbus_in();
const char* charger_status_str();
const char* charger_bus_status_str();
const char* charger_ntc_status_str();
float    charger_vbus_v();
float    charger_vsys_v();
float    charger_vbat_v();
float    charger_target_v();
float    charger_current_ma();
float    charger_prechrg_ma();

// GPS
void gps_get_coord(double* lat, double* lng);
uint32_t gps_satellites();

// RTC
void rtc_get_time(uint8_t* h, uint8_t* m, uint8_t* s);
void rtc_get_date(uint8_t* year, uint8_t* month, uint8_t* day, uint8_t* week);

// Keyboard
int keyboard_read_char();
void keyboard_set_backlight(uint8_t level);  // 0-255
uint8_t keyboard_get_backlight();

} // namespace board

// ---------- Board-specific headers ----------

#if defined(MESHUI_SIM)

// Host simulator (tools/sim/t5): no e-paper driver stack. The screens only touch
// board::touch (isPressed/getPoint) and board::peri_status; provide a tiny touch
// shim and the version/pin macros they reference, nothing else.
#define T_PAPER_HW_VERSION    "T5-ePaper-S3-PRO (sim)"
#define BOARD_BOOT_BTN        (0)

namespace board {
    struct SimTouch {
        bool isPressed() { return false; }
        uint8_t getPoint(int16_t* x, int16_t* y, uint8_t /*n*/) { if (x) *x = 0; if (y) *y = 0; return 0; }
    };
    extern SimTouch touch;
}

#elif defined(BOARD_EPAPER)

#include <driver/i2c.h>
#include <epdiy.h>
#include "TouchDrvGT911.hpp"
#include <SensorPCF8563.hpp>
#define XPOWERS_CHIP_BQ25896
#include <XPowersLib.h>
#include "bq27220.h"
#include "board/pca9555.h"

#define T_PAPER_HW_VERSION    "T5-ePaper-S3-PRO"

// Pin definitions (T5S3 4.7" e-paper PRO)
#define BOARD_GPS_RXD       44
#define BOARD_GPS_TXD       43
#define SerialMon           Serial
#define SerialGPS           Serial2

#define BOARD_I2C_PORT      (0)
#define BOARD_SCL           (40)
#define BOARD_SDA           (39)

#define BOARD_SPI_MISO      (21)
#define BOARD_SPI_MOSI      (13)
#define BOARD_SPI_SCLK      (14)

#define BOARD_TOUCH_SCL     (BOARD_SCL)
#define BOARD_TOUCH_SDA     (BOARD_SDA)
#define BOARD_TOUCH_INT     (3)
#define BOARD_TOUCH_RST     (9)

#define BOARD_RTC_SCL       (BOARD_SCL)
#define BOARD_RTC_SDA       (BOARD_SDA)
#define BOARD_RTC_IRQ       (2)

#define BOARD_SD_MISO       (BOARD_SPI_MISO)
#define BOARD_SD_MOSI       (BOARD_SPI_MOSI)
#define BOARD_SD_SCLK       (BOARD_SPI_SCLK)
#define BOARD_SD_CS         (12)

#define BOARD_LORA_MISO     (BOARD_SPI_MISO)
#define BOARD_LORA_MOSI     (BOARD_SPI_MOSI)
#define BOARD_LORA_SCLK     (BOARD_SPI_SCLK)
#define BOARD_LORA_CS       (46)
#define BOARD_LORA_IRQ      (10)
#define BOARD_LORA_RST      (1)
#define BOARD_LORA_BUSY     (47)

#define BOARD_BL_EN         (11)
#define BOARD_PCA9535_INT   (38)
#define BOARD_BOOT_BTN      (0)

// E-paper specific exports
extern "C" {
    void io_extend_lora_gps_power_on(bool en);
    uint8_t read_io(int io);
    void set_config(i2c_port_t port, uint8_t config_value, int high_port);
    bool button_read(void);
}

namespace board {
    extern XPowersPPM ppm;
    extern BQ27220 bq27220;
    extern TouchDrvGT911 touch;
    extern SensorPCF8563 rtc;
    extern SX1262 lora_radio;
    extern EpdiyHighlevelState hl;

    namespace detail {
        bool screen_init();
        bool bq25896_init();
        bool bq27220_init();
        bool rtc_init();
        bool touch_init();
        bool sd_init();
        bool gps_init();
    }
}

#else
#error "Define BOARD_EPAPER"
#endif

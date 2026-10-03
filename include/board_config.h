#pragma once

// ESP32-DevKit <-> Ra-02 (SX1278), VSPI
constexpr int PIN_LORA_NSS   = 5;
constexpr int PIN_LORA_SCK   = 18;
constexpr int PIN_LORA_MISO  = 19;
constexpr int PIN_LORA_MOSI  = 23;
constexpr int PIN_LORA_RST   = 14;
constexpr int PIN_LORA_DIO0  = 26;
constexpr int PIN_LORA_DIO1  = 33;  // DCLK im Direktmodus
constexpr int PIN_LORA_DIO2  = 32;  // DATA im Direktmodus

constexpr float POCSAG_FREQ_MHZ = 439.9875f;

constexpr float POCSAG_DEFAULT_BAUD = 1200.0f;
constexpr float POCSAG_DEVIATION_KHZ = 4.5f;
constexpr float RX_BANDWIDTH_KHZ = 20.8f;  // großzügig wegen Quarzfehler der SX1278-Module
constexpr float FREQ_CORRECTION_PPM = -12.0f;  // gemessen 2026-10-03: Modul + Pluto-Sender zusammen; positiv = Modul läuft zu hoch

// Onboard-LED des ESP32-DevKit (GPIO 2, high = an): aus im Ruhezustand, 3x kurzes Blinken pro erkanntem Ruf
constexpr int PIN_LED = 2;
constexpr bool LED_ACTIVE_HIGH = true;
constexpr uint32_t LED_BLINK_ON_MS = 80;
constexpr uint32_t LED_BLINK_OFF_MS = 80;
constexpr int LED_BLINK_COUNT = 5;

// I2C-OLED (Standard: SSD1306 128x32, 0,91"). Typ per Build-Flag wählbar, siehe platformio.ini.
constexpr int PIN_I2C_SDA = 21;
constexpr int PIN_I2C_SCL = 22;
// OLED-Versorgung aus GPIOs (nur wenn keine festen 3V3/GND-Pins frei sind). -1 = nicht benutzen.
// GND-Pin wird dauerhaft LOW, VCC-Pin HIGH (maximale Treiberstärke). Das OLED zieht nur wenige mA.
constexpr int PIN_OLED_GND = 17;
constexpr int PIN_OLED_VCC = 16;
constexpr uint32_t UI_REFRESH_MS = 250;
constexpr uint32_t UI_PAGE_MS = 3000;  // Zeit pro Textseite bei langen Nachrichten

// Animation bei neuem Ruf: einmal weiß aufblinken, dann Text in Großschrift einmal durchlaufen lassen
constexpr uint32_t UI_FLASH_MS = 250;
constexpr uint32_t UI_MARQUEE_PX_PER_S = 80;
constexpr uint32_t UI_ANIM_REFRESH_MS = 25;

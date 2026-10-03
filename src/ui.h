#pragma once
// OLED-Anzeige (U8g2, I2C): Kopfzeile mit RIC/Funktion/RSSI/Alter, darunter der Nachrichtentext mit
// Zeilenumbruch und automatischem seitenweisem Scrollen. Ohne erkanntes Display passiert nichts.
#include <stdint.h>
#include <string>

namespace ui {

struct Status {
  float freqMHz;
  int baud;
  float rssiDbm;
  bool locked;
  uint32_t batches;
  uint32_t calls;
};

bool begin();  // false, wenn kein Display am I2C-Bus antwortet
void showMessage(uint32_t ric, uint8_t function, const std::string &utf8Text, float rssiDbm, int corrected,
                 int uncorrectable);
void update(const Status &st);  // zyklisch aus loop() aufrufen, zeichnet höchstens alle UI_REFRESH_MS

}  // namespace ui

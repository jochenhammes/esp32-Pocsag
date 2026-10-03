#include "ui.h"
#include <U8g2lib.h>
#include <Wire.h>
#include <vector>
#include "board_config.h"
#include "driver/gpio.h"

namespace ui {

namespace {

#if defined(DISPLAY_SH1106_128X64)
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
constexpr int ROWS = 8;
#elif defined(DISPLAY_SSD1306_128X64)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
constexpr int ROWS = 8;
#else
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
constexpr int ROWS = 4;
#endif

constexpr int COLS = 25;  // 128 px / 5 px (u8g2_font_5x8_tf)
constexpr int BODY_ROWS = ROWS - 1;

bool present = false;
uint32_t lastDraw = 0;

// Animation: Flash -> Marquee -> Idle (normale Anzeige)
enum class Phase { Idle, Flash, Marquee };
Phase phase = Phase::Idle;
uint32_t phaseStart = 0;
uint32_t pageStart = 0;
std::string marqueeText;
int marqueeWidth = 0;
const uint8_t *bigFont = nullptr;
const int CONTRAST_NORMAL = 180;

struct Shown {
  bool valid = false;
  uint32_t ric = 0;
  uint8_t function = 0;
  float rssi = 0;
  int corrected = 0, bad = 0;
  uint32_t at = 0;
  std::vector<std::string> lines;
} shown;

// UTF-8 in Zeilen zu höchstens COLS Zeichen umbrechen (bevorzugt an Leerzeichen).
std::vector<std::string> wrap(const std::string &s) {
  std::vector<std::string> out;
  std::vector<std::string> chars;  // ein Eintrag je Codepoint
  for (size_t i = 0; i < s.size();) {
    unsigned char c = s[i];
    size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    if (i + n > s.size()) n = 1;
    std::string cp = s.substr(i, n);
    if (cp == "\r") {
      i += n;
      continue;
    }
    if (n == 1 && c < 0x20 && c != '\n') cp = " ";
    chars.push_back(cp);
    i += n;
  }
  size_t start = 0;
  while (start < chars.size()) {
    size_t end = start;
    int lastSpace = -1;
    bool forced = false;
    while (end < chars.size() && (int)(end - start) < COLS) {
      if (chars[end] == "\n") {
        forced = true;
        break;
      }
      if (chars[end] == " ") lastSpace = (int)end;
      end++;
    }
    size_t next = end;
    if (!forced && end < chars.size() && lastSpace > (int)start) {
      end = lastSpace;  // am letzten Leerzeichen umbrechen
      next = end + 1;
    } else if (forced) {
      next = end + 1;
    }
    std::string line;
    for (size_t k = start; k < end; k++) line += chars[k];
    out.push_back(line);
    start = next;
  }
  if (out.empty()) out.push_back("");
  return out;
}

std::string age(uint32_t ms) {
  char buf[12];
  uint32_t s = ms / 1000;
  if (s < 100) snprintf(buf, sizeof(buf), "%us", (unsigned)s);
  else if (s < 6000) snprintf(buf, sizeof(buf), "%um", (unsigned)(s / 60));
  else snprintf(buf, sizeof(buf), "%uh", (unsigned)(s / 3600));
  return buf;
}

void drawRow(int row, const std::string &s) { u8g2.drawUTF8(0, row * 8, s.c_str()); }

// Größte Schrift wählen, die in die Displayhöhe passt und Umlaute/ß enthält.
void pickBigFont() {
  static const uint8_t *const candidates[] = {
      u8g2_font_logisoso58_tf, u8g2_font_logisoso54_tf, u8g2_font_logisoso50_tf, u8g2_font_logisoso46_tf,
      u8g2_font_logisoso42_tf, u8g2_font_logisoso38_tf, u8g2_font_logisoso34_tf, u8g2_font_logisoso32_tf,
      u8g2_font_logisoso30_tf, u8g2_font_logisoso28_tf, u8g2_font_logisoso26_tf, u8g2_font_logisoso24_tf,
      u8g2_font_logisoso22_tf, u8g2_font_logisoso20_tf, u8g2_font_logisoso18_tf, u8g2_font_logisoso16_tf};
  int h = u8g2.getDisplayHeight();
  for (const uint8_t *f : candidates) {
    u8g2.setFont(f);
    if (u8g2.getAscent() - u8g2.getDescent() <= h && u8g2.getMaxCharHeight() <= h + 4 && u8g2_IsGlyph(u8g2.getU8g2(), 0xE4) &&
        u8g2_IsGlyph(u8g2.getU8g2(), 0xDF)) {
      bigFont = f;
      break;
    }
  }
  u8g2.setFont(u8g2_font_5x8_tf);
}

void drawFlash() {
  u8g2.setDrawColor(1);
  u8g2.drawBox(0, 0, u8g2.getDisplayWidth(), u8g2.getDisplayHeight());
}

// Text in Großschrift von rechts nach links; liefert true, solange noch Text sichtbar ist.
bool drawMarquee(uint32_t elapsedMs) {
  int w = u8g2.getDisplayWidth(), h = u8g2.getDisplayHeight();
  int x = w - (int)((uint64_t)elapsedMs * UI_MARQUEE_PX_PER_S / 1000);
  if (x + marqueeWidth < 0) return false;
  u8g2.setFont(bigFont);
  u8g2.setFontPosBaseline();
  int total = u8g2.getAscent() - u8g2.getDescent();
  u8g2.drawUTF8(x, (h - total) / 2 + u8g2.getAscent(), marqueeText.c_str());
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.setFontPosTop();
  return true;
}

}  // namespace

bool begin() {
  // Versorgung aus GPIOs: erst Masse, dann Plus, dann dem OLED Zeit zum Hochfahren geben
  if (PIN_OLED_GND >= 0) {
    pinMode(PIN_OLED_GND, OUTPUT);
    gpio_set_drive_capability((gpio_num_t)PIN_OLED_GND, GPIO_DRIVE_CAP_3);
    digitalWrite(PIN_OLED_GND, LOW);
  }
  if (PIN_OLED_VCC >= 0) {
    pinMode(PIN_OLED_VCC, OUTPUT);
    gpio_set_drive_capability((gpio_num_t)PIN_OLED_VCC, GPIO_DRIVE_CAP_3);
    digitalWrite(PIN_OLED_VCC, HIGH);
  }
  if (PIN_OLED_GND >= 0 || PIN_OLED_VCC >= 0) delay(100);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);
  // I2C-Scan: nur melden, was antwortet
  Serial.print("{\"i2c\":[");
  bool first = true;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("%s\"0x%02X\"", first ? "" : ",", a);
      first = false;
      if (a == 0x3C || a == 0x3D) present = true;
    }
  }
  Serial.printf("],\"display\":%s}\n", present ? "true" : "false");
  if (!present) return false;
  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.setFontPosTop();
  u8g2.setContrast(CONTRAST_NORMAL);
  pickBigFont();
  u8g2.clearBuffer();
  drawRow(0, "POCSAG-Empfaenger");
  drawRow(1, "ESP32 + SX1278");
  u8g2.sendBuffer();
  return true;
}

void showMessage(uint32_t ric, uint8_t function, const std::string &utf8Text, float rssiDbm, int corrected,
                 int uncorrectable) {
  shown.valid = true;
  shown.ric = ric;
  shown.function = function;
  shown.rssi = rssiDbm;
  shown.corrected = corrected;
  shown.bad = uncorrectable;
  shown.at = millis();
  shown.lines = wrap(utf8Text);
  pageStart = millis();
  lastDraw = 0;  // sofort neu zeichnen

  // Laufschrift: Steuerzeichen als Leerzeichen; Tonrufe ohne Text zeigen die RIC
  marqueeText.clear();
  for (char c : utf8Text) marqueeText += ((unsigned char)c < 0x20) ? ' ' : c;
  if (marqueeText.find_first_not_of(' ') == std::string::npos) marqueeText = std::to_string(ric);
  if (bigFont) {
    u8g2.setFont(bigFont);
    marqueeWidth = u8g2.getUTF8Width(marqueeText.c_str());
    u8g2.setFont(u8g2_font_5x8_tf);
    phase = Phase::Flash;
    phaseStart = millis();
    u8g2.setContrast(255);
  }
}

void update(const Status &st) {
  if (!present) return;
  uint32_t interval = phase == Phase::Idle ? UI_REFRESH_MS : UI_ANIM_REFRESH_MS;
  if (millis() - lastDraw < interval) return;
  lastDraw = millis();
  char buf[48];
  u8g2.clearBuffer();
  if (phase == Phase::Flash) {
    if (millis() - phaseStart < UI_FLASH_MS) {
      drawFlash();
      u8g2.sendBuffer();
      return;
    }
    phase = Phase::Marquee;
    phaseStart = millis();
  }
  if (phase == Phase::Marquee) {
    if (drawMarquee(millis() - phaseStart)) {
      u8g2.sendBuffer();
      return;
    }
    phase = Phase::Idle;
    pageStart = millis();
    u8g2.setContrast(CONTRAST_NORMAL);
    u8g2.clearBuffer();
  }
  if (!shown.valid) {
    snprintf(buf, sizeof(buf), "POCSAG %.4f %dBd", st.freqMHz, st.baud);
    drawRow(0, buf);
    snprintf(buf, sizeof(buf), "RSSI %.0f dBm  %s", st.rssiDbm, st.locked ? "LOCK" : "");
    drawRow(1, buf);
    if (ROWS > 2) {
      snprintf(buf, sizeof(buf), "Batches %lu  Rufe %lu", (unsigned long)st.batches, (unsigned long)st.calls);
      drawRow(2, buf);
    }
    if (ROWS > 3) drawRow(3, "Warte auf Rufe ...");
  } else {
    int pages = ((int)shown.lines.size() + BODY_ROWS - 1) / BODY_ROWS;
    int page = pages > 1 ? (int)(((millis() - pageStart) / UI_PAGE_MS) % pages) : 0;
    std::string a = age(millis() - shown.at);
    if (pages > 1) snprintf(buf, sizeof(buf), "%lu F%u %.0f %s %d/%d", (unsigned long)shown.ric, shown.function,
                            shown.rssi, a.c_str(), page + 1, pages);
    else snprintf(buf, sizeof(buf), "%lu F%u %.0fdBm %s%s", (unsigned long)shown.ric, shown.function, shown.rssi,
                  a.c_str(), shown.bad ? " !" : "");
    drawRow(0, buf);
    for (int r = 0; r < BODY_ROWS; r++) {
      size_t idx = (size_t)page * BODY_ROWS + r;
      if (idx < shown.lines.size()) drawRow(r + 1, shown.lines[idx]);
    }
  }
  u8g2.sendBuffer();
}

}  // namespace ui

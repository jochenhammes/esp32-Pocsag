// POCSAG-Empfänger: SX1278 (Ra-02) im Direktmodus -> Decoder -> JSON-Zeilen auf Serial.
#include <Arduino.h>
#include <string>
#include "batch_decoder.h"
#include "board_config.h"
#include "radio.h"
#include "text.h"
#include "ui.h"

using namespace pocsag;

static BatchDecoder *decoder;

// LED-Blinken ohne Blockieren: ledStep zählt die verbleibenden Schaltvorgänge (gerade = an, ungerade = aus)
static int ledStep = 0;
static uint32_t ledAt = 0;

static void ledWrite(bool on) { digitalWrite(PIN_LED, on == LED_ACTIVE_HIGH ? HIGH : LOW); }

static void ledTrigger() {
  ledStep = LED_BLINK_COUNT * 2;  // beginnt mit "an"; ein neuer Ruf während des Blinkens startet neu
  ledAt = millis();
}

static void ledUpdate() {
  if (ledStep == 0 || (int32_t)(millis() - ledAt) < 0) return;
  bool on = (ledStep % 2) == 0;
  ledWrite(on);
  ledAt = millis() + (on ? LED_BLINK_ON_MS : LED_BLINK_OFF_MS);
  ledStep--;
}
static uint32_t callCount = 0;
static bool rawDump = false;  // Debug: Rohbits als "R <64 Bit>"-Zeilen ausgeben
static float rssiPeak = -200.0f;  // Maximum seit dem letzten Status

static void jsonEscape(const std::string &s, std::string &out) {
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += (char)c;  // UTF-8 unverändert
        }
    }
  }
}

static void onMessage(const Message &m) {
  ledTrigger();
  callCount++;
  // Text in beiden Zeichensätzen: DAPNET-Netzbetreiber nutzen je nach Sender ASCII oder DIN 66003.
  std::string textDe = bitsToText(m.payload, TextKind::Auto, Charset::German, m.function);
  std::string esc;
  jsonEscape(textDe, esc);
  ui::showMessage(m.ric, m.function, textDe, radio::rssiDbm(), m.corrected, m.uncorrectable);
  Serial.printf(
      "{\"t\":%lu,\"ric\":%lu,\"func\":%u,\"text\":\"%s\",\"bits\":%u,\"corrected\":%d,\"bad\":%d,"
      "\"inverted\":%s,\"baud\":%d,\"rssi\":%.1f}\n",
      (unsigned long)millis(), (unsigned long)m.ric, m.function, esc.c_str(), (unsigned)m.payload.size(),
      m.corrected, m.uncorrectable, m.inverted ? "true" : "false", (int)radio::baud(), radio::rssiDbm());
}

static void printStatus() {
  Serial.printf(
      "{\"status\":{\"batches\":%lu,\"cw_ok\":%lu,\"cw_bad\":%lu,\"locked\":%s,\"bits\":%lu,\"overruns\":%lu,"
      "\"rssi\":%.1f,\"rssi_peak\":%.1f,\"baud\":%d,\"ppm\":%.1f,\"uptime_s\":%lu}}\n",
      (unsigned long)decoder->batches, (unsigned long)decoder->codewordsOk, (unsigned long)decoder->codewordsBad,
      decoder->locked ? "true" : "false", (unsigned long)radio::bitsReceived(), (unsigned long)radio::overruns(),
      radio::rssiDbm(), rssiPeak, (int)radio::baud(), radio::ppm(), (unsigned long)(millis() / 1000));
  rssiPeak = -200.0f;
}

// Serielle Befehle: "baud 512|1200|2400", "ppm <x>", "status"
static void handleCommand(String line) {
  line.trim();
  if (line == "status") {
    printStatus();
  } else if (line.startsWith("baud ")) {
    int b = line.substring(5).toInt();
    if (b == 512 || b == 1200 || b == 2400) {
      decoder->flush();
      delete decoder;
      decoder = new BatchDecoder(onMessage);
      Serial.printf("{\"ok\":%s,\"baud\":%d}\n", radio::setBaud((float)b) ? "true" : "false", b);
    } else {
      Serial.println("{\"error\":\"baud muss 512, 1200 oder 2400 sein\"}");
    }
  } else if (line.startsWith("ppm ")) {
    float p = line.substring(4).toFloat();
    Serial.printf("{\"ok\":%s,\"ppm\":%.1f}\n", radio::setPpm(p) ? "true" : "false", p);
  } else if (line == "raw on" || line == "raw off") {
    rawDump = line == "raw on";
    Serial.printf("{\"raw\":%s}\n", rawDump ? "true" : "false");
  } else if (line.startsWith("reg ")) {  // "reg 40" liest, "reg 40 0A" schreibt (hex)
    int sp = line.indexOf(' ', 4);
    uint8_t a = (uint8_t)strtol(line.substring(4, sp < 0 ? line.length() : sp).c_str(), nullptr, 16);
    if (sp > 0) radio::writeReg(a, (uint8_t)strtol(line.substring(sp + 1).c_str(), nullptr, 16));
    Serial.printf("{\"reg\":\"0x%02X\",\"val\":\"0x%02X\",\"dio2\":%d}\n", a, radio::readReg(a), radio::dio2Level());
  } else if (line.startsWith("bw ")) {
    float b = line.substring(3).toFloat();
    Serial.printf("{\"ok\":%s,\"bw\":%.1f}\n", radio::setBandwidth(b) ? "true" : "false", b);
  } else if (line.length()) {
    Serial.println("{\"error\":\"unbekannter Befehl (status | baud N | ppm X | bw kHz | raw on/off)\"}");
  }
}

void setup() {
  pinMode(PIN_LED, OUTPUT);
  ledWrite(false);
  Serial.begin(115200);
  delay(1000);
  Serial.println("{\"boot\":\"esp32-pocsag\"}");
  decoder = new BatchDecoder(onMessage);
  if (!radio::begin(POCSAG_FREQ_MHZ, POCSAG_DEFAULT_BAUD, FREQ_CORRECTION_PPM)) {
    Serial.println("{\"error\":\"Funkmodul nicht initialisiert\"}");
    while (true) delay(1000);
  }
  ui::begin();
  Serial.printf("{\"rx\":{\"freq_mhz\":%.4f,\"baud\":%d}}\n", POCSAG_FREQ_MHZ, (int)POCSAG_DEFAULT_BAUD);
}

void loop() {
  static uint32_t lastStatus = 0;
  ledUpdate();
  uint8_t bit;
  int n = 0;
  while (n < 256 && radio::popBit(bit)) {
    decoder->pushBit(bit);
    n++;
    if (rawDump) {
      static char rawLine[80];
      static int rawLen = 0;
      rawLine[rawLen++] = '0' + bit;
      if (rawLen == 64) {
        rawLine[rawLen] = 0;
        Serial.printf("R %s\n", rawLine);
        rawLen = 0;
      }
    }
  }
  float r = radio::rssiDbm();
  if (r > rssiPeak) rssiPeak = r;
  ui::update({POCSAG_FREQ_MHZ, (int)radio::baud(), r, decoder->locked, decoder->batches, callCount});
  if (millis() - lastStatus >= 5000) {
    lastStatus = millis();
    printStatus();
  }
  while (Serial.available()) {
    static String cmd;
    char c = Serial.read();
    if (c == '\n') {
      handleCommand(cmd);
      cmd = "";
    } else if (c != '\r' && cmd.length() < 64) {
      cmd += c;
    }
  }
  delay(2);
}

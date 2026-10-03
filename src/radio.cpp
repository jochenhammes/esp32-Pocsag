#include "radio.h"
#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include "board_config.h"
#include "driver/gpio.h"

namespace radio {

namespace {

constexpr uint32_t RING_SIZE = 4096;  // Bits, Zweierpotenz

SPIClass spi(VSPI);
Module mod(PIN_LORA_NSS, PIN_LORA_DIO0, PIN_LORA_RST, PIN_LORA_DIO1, spi);
SX1278 sx(&mod);

volatile uint8_t ring[RING_SIZE];
volatile uint32_t head = 0;  // nur ISR
volatile uint32_t tail = 0;  // nur loop()
volatile uint32_t nBits = 0;
volatile uint32_t nOverruns = 0;

float freq_ = 0, baud_ = 0, ppm_ = 0, bw_ = RX_BANDWIDTH_KHZ;

void IRAM_ATTR onDclk() {
  uint32_t h = head;
  if (h - tail >= RING_SIZE) {
    nOverruns = nOverruns + 1;
    return;
  }
  ring[h & (RING_SIZE - 1)] = gpio_get_level((gpio_num_t)PIN_LORA_DIO2);
  head = h + 1;
  nBits = nBits + 1;
}

// Das Modul läuft bei ppm > 0 zu hoch, also entsprechend tiefer abstimmen.
float tunedFreq() { return freq_ / (1.0f + ppm_ * 1e-6f); }

bool isrAttached = false;

int16_t startRx() {
  if (isrAttached) detachInterrupt(digitalPinToInterrupt(PIN_LORA_DIO1));
  isrAttached = false;
  int16_t st = sx.setFrequency(tunedFreq());
  if (st != RADIOLIB_ERR_NONE) return st;
  st = sx.setBitRate(baud_ / 1000.0f);
  if (st != RADIOLIB_ERR_NONE) return st;
  st = sx.setRxBandwidth(bw_);
  if (st != RADIOLIB_ERR_NONE) return st;
  st = sx.enableBitSync();
  if (st != RADIOLIB_ERR_NONE) return st;
  st = sx.receiveDirect();
  if (st != RADIOLIB_ERR_NONE) return st;
  head = tail = 0;
  attachInterrupt(digitalPinToInterrupt(PIN_LORA_DIO1), onDclk, RISING);
  isrAttached = true;
  return RADIOLIB_ERR_NONE;
}

}  // namespace

bool begin(float freqMHz, float baud, float ppm) {
  freq_ = freqMHz;
  baud_ = baud;
  ppm_ = ppm;
  spi.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
  pinMode(PIN_LORA_DIO1, INPUT);  // GPIO-Eingangspuffer aktivieren, sonst liest gpio_get_level() in der ISR immer 0
  pinMode(PIN_LORA_DIO2, INPUT);
  int16_t st = sx.beginFSK(tunedFreq(), baud_ / 1000.0f, POCSAG_DEVIATION_KHZ, RX_BANDWIDTH_KHZ, 10, 16, false);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("{\"error\":\"beginFSK\",\"code\":%d}\n", st);
    return false;
  }
  st = startRx();
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("{\"error\":\"receiveDirect\",\"code\":%d}\n", st);
    return false;
  }
  return true;
}

bool setBaud(float b) {
  baud_ = b;
  return startRx() == RADIOLIB_ERR_NONE;
}

bool setPpm(float p) {
  ppm_ = p;
  return startRx() == RADIOLIB_ERR_NONE;
}

bool setBandwidth(float khz) {
  bw_ = khz;
  return startRx() == RADIOLIB_ERR_NONE;
}

bool popBit(uint8_t &bit) {
  uint32_t t = tail;
  if (t == head) return false;
  bit = ring[t & (RING_SIZE - 1)];
  tail = t + 1;
  return true;
}

uint8_t readReg(uint8_t addr) { return mod.SPIreadRegister(addr); }
void writeReg(uint8_t addr, uint8_t value) { mod.SPIwriteRegister(addr, value); }
int dio2Level() { return gpio_get_level((gpio_num_t)PIN_LORA_DIO2); }

float baud() { return baud_; }
float ppm() { return ppm_; }
// RSSI direkt aus RegRssiValue (0x11) lesen; sx.getRSSI() würde den Modus umschalten und den Direktmodus stoppen.
float rssiDbm() { return -(float)mod.SPIreadRegister(0x11) / 2.0f; }
uint32_t bitsReceived() { return nBits; }
uint32_t overruns() { return nOverruns; }

}  // namespace radio

#pragma once
// SX1278 im FSK-Direktmodus: DIO1 = DCLK (Bittakt), DIO2 = DATA. Die ISR schreibt jedes Bit in einen
// lock-freien Ringpuffer (ein Produzent = ISR, ein Konsument = loop()).
#include <stdint.h>

namespace radio {

bool begin(float freqMHz, float baud, float ppm);
bool setBaud(float baud);
bool setPpm(float ppm);
bool setBandwidth(float khz);  // Rx-Bandbreite (SSB-Wert der SX127x-Tabelle)

// Nächstes Bit aus dem Ringpuffer (0/1); false, wenn leer.
// Debug: SX127x-Register lesen/schreiben
uint8_t readReg(uint8_t addr);
void writeReg(uint8_t addr, uint8_t value);
int dio2Level();

bool popBit(uint8_t &bit);

float baud();
float ppm();
float rssiDbm();
uint32_t bitsReceived();
uint32_t overruns();

}  // namespace radio

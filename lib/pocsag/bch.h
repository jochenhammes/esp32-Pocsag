#pragma once
// POCSAG-Codewörter: BCH(31,21) (Generator x^10+x^9+x^8+x^6+x^5+x^3+1) plus gerades Paritätsbit.
// Port von pluto_tx/pocsag_codec.py, ohne Arduino-Abhängigkeiten.
#include <stdint.h>

namespace pocsag {

constexpr uint32_t SYNC = 0x7CD215D8u;
constexpr uint32_t IDLE = 0x7A89C197u;
constexpr uint32_t BCH_POLY = 0x769u;
constexpr int BATCH_WORDS = 16;

inline int popcount32(uint32_t x) { return __builtin_popcount(x); }
inline int parity32(uint32_t x) { return __builtin_popcount(x) & 1; }

uint32_t bchRemainder(uint32_t v31);
uint32_t makeCodeword(uint32_t data21);
uint32_t addressCodeword(uint32_t address18, uint32_t function);
uint32_t messageCodeword(uint32_t data20);
bool isValid(uint32_t cw);

struct Correction {
  bool ok;
  uint32_t codeword;
  int errors;
};

// Korrigiert bis zu 2 Bitfehler (inkl. Paritätsbit); ok=false, wenn nicht korrigierbar.
Correction correctCodeword(uint32_t cw);

}  // namespace pocsag

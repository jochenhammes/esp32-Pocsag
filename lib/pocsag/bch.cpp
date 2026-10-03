#include "bch.h"

namespace pocsag {

uint32_t bchRemainder(uint32_t v31) {
  for (int i = 30; i >= 10; i--) {
    if ((v31 >> i) & 1u) v31 ^= BCH_POLY << (i - 10);
  }
  return v31 & 0x3FFu;
}

uint32_t makeCodeword(uint32_t data21) {
  uint32_t body = (data21 << 10) | bchRemainder(data21 << 10);
  return (body << 1) | (uint32_t)parity32(body);
}

uint32_t addressCodeword(uint32_t address18, uint32_t function) {
  return makeCodeword(((address18 & 0x3FFFFu) << 2) | (function & 3u));
}

uint32_t messageCodeword(uint32_t data20) {
  return makeCodeword((1u << 20) | (data20 & 0xFFFFFu));
}

bool isValid(uint32_t cw) { return bchRemainder(cw >> 1) == 0 && parity32(cw) == 0; }

namespace {

// Syndrom (10 Bit) -> Bitmaske der zu kippenden Bits im 32-Bit-Wort (0 = keine Korrektur bekannt).
// Gewicht 1 hat Vorrang vor Gewicht-2-Kollisionen, wie im Python-Original.
struct SyndromeTable {
  uint32_t flips[1024];
  SyndromeTable() {
    for (auto &f : flips) f = 0;
    for (int i = 0; i < 31; i++) {
      uint32_t s = bchRemainder(1u << i);
      if (!flips[s]) flips[s] = 1u << (i + 1);
    }
    for (int i = 0; i < 31; i++) {
      for (int j = i + 1; j < 31; j++) {
        uint32_t s = bchRemainder((1u << i) | (1u << j));
        if (!flips[s]) flips[s] = (1u << (i + 1)) | (1u << (j + 1));
      }
    }
  }
};

const SyndromeTable &table() {
  static const SyndromeTable t;
  return t;
}

}  // namespace

Correction correctCodeword(uint32_t cw) {
  uint32_t s = bchRemainder(cw >> 1);
  uint32_t flips = 0;
  if (s) {
    flips = table().flips[s];
    if (!flips) return {false, 0, 0};
    cw ^= flips;
  }
  int errors = popcount32(flips);
  if (parity32(cw)) {  // Rest-Paritätsfehler: das Paritätsbit selbst war betroffen
    cw ^= 1u;
    errors++;
  }
  if (errors > 2 || !isValid(cw)) return {false, 0, 0};
  return {true, cw, errors};
}

}  // namespace pocsag

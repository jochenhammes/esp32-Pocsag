#include "text.h"

namespace pocsag {

namespace {

const char NUMERIC_CHARS[] = "0123456789*U -][";  // Index = 4-Bit-Code, LSB zuerst

const char *germanChar(int code) {
  switch (code) {
    case 0x5B: return "\xC3\x84";  // Ä
    case 0x5C: return "\xC3\x96";  // Ö
    case 0x5D: return "\xC3\x9C";  // Ü
    case 0x7B: return "\xC3\xA4";  // ä
    case 0x7C: return "\xC3\xB6";  // ö
    case 0x7D: return "\xC3\xBC";  // ü
    case 0x7E: return "\xC3\x9F";  // ß
    default: return nullptr;
  }
}

}  // namespace

std::string bitsToText(const uint8_t *bits, size_t n, TextKind kind, Charset cs, int function) {
  if (kind == TextKind::Auto) kind = (function == 0) ? TextKind::Numeric : TextKind::Alpha;
  std::string out;
  if (kind == TextKind::Numeric) {
    for (size_t i = 0; i + 4 <= n; i += 4) {
      int code = bits[i] | (bits[i + 1] << 1) | (bits[i + 2] << 2) | (bits[i + 3] << 3);
      out += NUMERIC_CHARS[code];
    }
    while (!out.empty() && (out.back() == ' ')) out.pop_back();
    return out;
  }
  for (size_t i = 0; i + 7 <= n; i += 7) {
    int code = 0;
    for (int k = 0; k < 7; k++) code |= bits[i + k] << k;
    if (code == 0x04) break;
    const char *de = (cs == Charset::German) ? germanChar(code) : nullptr;
    if (de) {
      out += de;
    } else if (code == 0x0A || code == 0x0D || (code >= 0x20 && code < 0x7F)) {
      out += (char)code;
    }
  }
  return out;
}

}  // namespace pocsag

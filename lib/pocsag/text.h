#pragma once
// Nachrichtentext aus den 20-Bit-Nutzdaten (ein Bit pro Byte, 0/1).
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace pocsag {

enum class TextKind { Auto, Alpha, Numeric };
enum class Charset { Ascii, German };  // German = DIN 66003 (Umlaute auf [ \ ] { | } ~)

// Liefert UTF-8. Auto: Funktion 0 = numerisch, sonst alpha.
std::string bitsToText(const uint8_t *bits, size_t n, TextKind kind, Charset cs, int function = 3);
inline std::string bitsToText(const std::vector<uint8_t> &bits, TextKind kind, Charset cs, int function = 3) {
  return bitsToText(bits.data(), bits.size(), kind, cs, function);
}

}  // namespace pocsag

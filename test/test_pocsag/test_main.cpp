#include <unity.h>
#include <string.h>
#include <string>
#include <vector>
#include "batch_decoder.h"
#include "bch.h"
#include "text.h"
#include "vectors.h"

using namespace pocsag;

namespace {

uint32_t rngState = 12345;
uint32_t rnd() {
  rngState = rngState * 1664525u + 1013904223u;
  return rngState >> 8;
}

std::vector<uint8_t> unpack(const Vector &v) {
  std::vector<uint8_t> bits(v.nbits);
  for (uint32_t i = 0; i < v.nbits; i++) bits[i] = (v.bits[i >> 3] >> (7 - (i & 7))) & 1;
  return bits;
}

struct Decoded {
  Message msg;
  std::string text;
};

std::vector<Decoded> decode(const std::vector<uint8_t> &bits, BatchDecoder **out = nullptr) {
  static std::vector<Decoded> results;
  results.clear();
  static BatchDecoder *dec = nullptr;
  delete dec;
  dec = new BatchDecoder([](const Message &m) { results.push_back({m, ""}); });
  for (uint8_t b : bits) dec->pushBit(b);
  dec->flush();
  if (out) *out = dec;
  return results;
}

TextKind kindOf(const Vector &v) { return strcmp(v.kind, "numeric") == 0 ? TextKind::Numeric : TextKind::Alpha; }
Charset charsetOf(const Vector &v) { return strcmp(v.charset, "de") == 0 ? Charset::German : Charset::Ascii; }

void expectVector(const Vector &v, const std::vector<Decoded> &res, bool allowCorrected = true) {
  TEST_ASSERT_EQUAL_MESSAGE(1, (int)res.size(), v.name);
  const Message &m = res[0].msg;
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(v.ric, m.ric, v.name);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(v.function, m.function, v.name);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, m.uncorrectable, v.name);
  if (!allowCorrected) TEST_ASSERT_EQUAL_INT_MESSAGE(0, m.corrected, v.name);
  if (strcmp(v.kind, "tone") != 0) {
    std::string text = bitsToText(m.payload, kindOf(v), charsetOf(v));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(v.text, text.c_str(), v.name);
  } else {
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)m.payload.size(), v.name);
  }
}

}  // namespace

void test_sync_and_idle_valid() {
  TEST_ASSERT_TRUE(isValid(SYNC));
  TEST_ASSERT_TRUE(isValid(IDLE));
}

void test_codewords_match_python() {
  for (int i = 0; i < N_CODEWORDS; i++) {
    const CodewordRef &r = CODEWORDS[i];
    uint32_t cw = r.isAddress ? addressCodeword(r.field, r.function) : messageCodeword(r.field);
    TEST_ASSERT_EQUAL_HEX32(r.cw, cw);
    TEST_ASSERT_TRUE(isValid(cw));
  }
}

void test_bch_corrects_one_and_two_bit_errors_exhaustively() {
  const uint32_t words[] = {addressCodeword(0x2AAAA, 2), messageCodeword(0xABCDE), messageCodeword(0), IDLE, SYNC};
  for (uint32_t cw : words) {
    Correction c0 = correctCodeword(cw);
    TEST_ASSERT_TRUE(c0.ok);
    TEST_ASSERT_EQUAL_INT(0, c0.errors);
    for (int i = 0; i < 32; i++) {
      Correction c1 = correctCodeword(cw ^ (1u << i));
      TEST_ASSERT_TRUE(c1.ok);
      TEST_ASSERT_EQUAL_HEX32(cw, c1.codeword);
      TEST_ASSERT_EQUAL_INT(1, c1.errors);
      for (int j = i + 1; j < 32; j++) {
        Correction c2 = correctCodeword(cw ^ (1u << i) ^ (1u << j));
        TEST_ASSERT_TRUE(c2.ok);
        TEST_ASSERT_EQUAL_HEX32(cw, c2.codeword);
        TEST_ASSERT_EQUAL_INT(2, c2.errors);
      }
    }
  }
}

void test_bch_never_returns_corrupted_word_as_clean() {
  uint32_t cw = messageCodeword(0x12345);
  int aliased = 0, total = 0;
  for (int i = 0; i < 32; i++)
    for (int j = i + 1; j < 32; j++)
      for (int k = j + 1; k < 32; k++) {
        uint32_t bad = cw ^ (1u << i) ^ (1u << j) ^ (1u << k);
        Correction c = correctCodeword(bad);
        total++;
        if (c.ok) {
          aliased++;
          TEST_ASSERT_NOT_EQUAL(bad, c.codeword);
          TEST_ASSERT_TRUE(isValid(c.codeword));
        }
      }
  TEST_ASSERT_LESS_THAN(total / 2, aliased);
}

void test_text_alpha_numeric_german() {
  // "A" = 0x41 LSB zuerst, danach EOT
  const uint8_t a[] = {1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_STRING("A", bitsToText(a, sizeof(a), TextKind::Alpha, Charset::Ascii).c_str());
  // 0x5B = Ä (DIN 66003) -> UTF-8; im ASCII-Zeichensatz wird 0x5B als '[' ausgegeben
  const uint8_t ae[] = {1, 1, 0, 1, 1, 0, 1};
  TEST_ASSERT_EQUAL_STRING("\xC3\x84", bitsToText(ae, 7, TextKind::Alpha, Charset::German).c_str());
  TEST_ASSERT_EQUAL_STRING("[", bitsToText(ae, 7, TextKind::Alpha, Charset::Ascii).c_str());
  // numerisch: 1 dann 2, LSB zuerst; Auto + Funktion 0 -> numerisch
  const uint8_t n[] = {1, 0, 0, 0, 0, 1, 0, 0};
  TEST_ASSERT_EQUAL_STRING("12", bitsToText(n, 8, TextKind::Auto, Charset::Ascii, 0).c_str());
}

void test_decode_all_vectors_clean() {
  for (int i = 0; i < N_VECTORS; i++) {
    auto res = decode(unpack(VECTORS[i]));
    expectVector(VECTORS[i], res, false);
    TEST_ASSERT_FALSE(res[0].msg.inverted);
  }
}

void test_decode_inverted_polarity() {
  for (int i = 0; i < N_VECTORS; i++) {
    auto bits = unpack(VECTORS[i]);
    for (auto &b : bits) b ^= 1;
    auto res = decode(bits);
    expectVector(VECTORS[i], res, false);
    TEST_ASSERT_TRUE(res[0].msg.inverted);
  }
}

void test_decode_after_noise_prefix() {
  for (int i = 0; i < N_VECTORS; i++) {
    auto bits = unpack(VECTORS[i]);
    std::vector<uint8_t> in;
    for (int k = 0; k < 1000 + i * 13; k++) in.push_back(rnd() & 1);
    in.insert(in.end(), bits.begin(), bits.end());
    auto res = decode(in);
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)res.size(), VECTORS[i].name);
    TEST_ASSERT_EQUAL_UINT32(VECTORS[i].ric, res[0].msg.ric);
  }
}

// Ein Bitfehler pro Codewort (ab dem Ende der Präambel) muss vollständig korrigiert werden.
void test_one_bit_error_per_word() {
  for (int i = 0; i < N_VECTORS; i++) {
    auto bits = unpack(VECTORS[i]);
    for (size_t w = 576; w + 32 <= bits.size(); w += 32) bits[w + rnd() % 32] ^= 1;
    auto res = decode(bits);
    expectVector(VECTORS[i], res);
  }
}

void test_random_bit_errors() {
  int ok = 0, total = 0;
  for (int i = 0; i < N_VECTORS; i++) {
    for (int trial = 0; trial < 20; trial++) {
      auto bits = unpack(VECTORS[i]);
      for (auto &b : bits)
        if (rnd() % 1000 < 3) b ^= 1;  // 0,3 % Bitfehlerrate
      auto res = decode(bits);
      total++;
      if (res.size() == 1 && res[0].msg.ric == VECTORS[i].ric && res[0].msg.uncorrectable == 0) ok++;
    }
  }
  TEST_ASSERT_GREATER_THAN(total * 9 / 10, ok);
}

void test_pure_noise_gives_no_calls() {
  std::vector<uint8_t> bits;
  for (int k = 0; k < 200000; k++) bits.push_back(rnd() & 1);
  BatchDecoder *dec = nullptr;
  auto res = decode(bits, &dec);
  TEST_ASSERT_EQUAL_INT(0, (int)res.size());
}

void test_two_calls_back_to_back() {
  auto a = unpack(VECTORS[1]);
  auto b = unpack(VECTORS[2]);
  std::vector<uint8_t> bits = a;
  for (int k = 0; k < 200; k++) bits.push_back(rnd() & 1);
  bits.insert(bits.end(), b.begin(), b.end());
  auto res = decode(bits);
  TEST_ASSERT_EQUAL_INT(2, (int)res.size());
  TEST_ASSERT_EQUAL_UINT32(VECTORS[1].ric, res[0].msg.ric);
  TEST_ASSERT_EQUAL_UINT32(VECTORS[2].ric, res[1].msg.ric);
}

void test_long_message_spans_batches() {
  const Vector *v = nullptr;
  for (int i = 0; i < N_VECTORS; i++)
    if (strcmp(VECTORS[i].name, "long") == 0) v = &VECTORS[i];
  TEST_ASSERT_NOT_NULL(v);
  BatchDecoder *dec = nullptr;
  auto res = decode(unpack(*v), &dec);
  expectVector(*v, res, false);
  TEST_ASSERT_GREATER_THAN(2, (int)dec->batches);
}

// Nach Ende einer Sendung dekodiert der Decoder noch bis zu zwei Batches Rauschen. Daraus dürfen kaum
// Phantomrufe entstehen.
void test_noise_after_call_gives_few_phantoms() {
  int phantoms = 0;
  const int trials = 300;
  for (int t = 0; t < trials; t++) {
    auto bits = unpack(VECTORS[1]);
    for (int k = 0; k < 3000; k++) bits.push_back(rnd() & 1);
    auto res = decode(bits);
    TEST_ASSERT_GREATER_OR_EQUAL(1, (int)res.size());
    phantoms += (int)res.size() - 1;
  }
  printf("phantoms: %d/%d\n", phantoms, trials);
  TEST_ASSERT_LESS_OR_EQUAL(trials / 50, phantoms);  // <= 2 %
}

void setUp() {}
void tearDown() {}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_sync_and_idle_valid);
  RUN_TEST(test_codewords_match_python);
  RUN_TEST(test_bch_corrects_one_and_two_bit_errors_exhaustively);
  RUN_TEST(test_bch_never_returns_corrupted_word_as_clean);
  RUN_TEST(test_text_alpha_numeric_german);
  RUN_TEST(test_decode_all_vectors_clean);
  RUN_TEST(test_decode_inverted_polarity);
  RUN_TEST(test_decode_after_noise_prefix);
  RUN_TEST(test_one_bit_error_per_word);
  RUN_TEST(test_random_bit_errors);
  RUN_TEST(test_pure_noise_gives_no_calls);
  RUN_TEST(test_two_calls_back_to_back);
  RUN_TEST(test_long_message_spans_batches);
  RUN_TEST(test_noise_after_call_gives_few_phantoms);
  return UNITY_END();
}

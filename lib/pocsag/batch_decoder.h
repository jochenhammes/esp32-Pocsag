#pragma once
// Bitweiser POCSAG-Batch-Decoder (Port von BatchDecoder in pluto_tx/pocsag_codec.py).
// Sucht das Sync-Wort in beiden Polaritäten (<= 2 Bitfehler), folgt den Batches (Sync alle 17 Wörter),
// korrigiert per BCH und setzt Nachrichten über mehrere Codewörter/Batches zusammen.
#include <stdint.h>
#include <functional>
#include <vector>
#include "bch.h"

namespace pocsag {

struct Message {
  uint32_t ric = 0;
  uint8_t function = 0;
  std::vector<uint8_t> payload;  // ein Bit pro Byte, ganze 20-Bit-Codewörter
  int corrected = 0;             // korrigierte Bits
  int uncorrectable = 0;         // nicht korrigierbare Nachrichten-Codewörter ("beschädigt")
  bool inverted = false;         // Polarität
};

class BatchDecoder {
 public:
  using Callback = std::function<void(const Message &)>;

  explicit BatchDecoder(Callback cb = nullptr, int syncErrors = 2, int maxMissedSyncs = 2)
      : cb_(std::move(cb)), syncErrors_(syncErrors), maxMissed_(maxMissedSyncs) {
    resetHunt();
  }

  void pushBit(int bit);
  void flush() { finishMessage(); }

  uint32_t batches = 0;
  uint32_t codewordsOk = 0;
  uint32_t codewordsBad = 0;
  bool locked = false;

 private:
  enum class State { Hunt, Batch, Sync };

  void resetHunt();
  void beginBatch(bool missed = false);
  void codeword(uint32_t word, int index);
  void finishMessage();

  Callback cb_;
  int syncErrors_;
  int maxMissed_;
  uint32_t reg_ = 0;
  State state_ = State::Hunt;
  int nbits_ = 0;
  int wordsInBatch_ = 0;
  int missed_ = 0;
  bool inverted_ = false;
  bool haveMsg_ = false;
  Message msg_;
};

}  // namespace pocsag

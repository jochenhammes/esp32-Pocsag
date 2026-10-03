#include "batch_decoder.h"

namespace pocsag {

void BatchDecoder::resetHunt() {
  reg_ = 0;
  state_ = State::Hunt;
  nbits_ = 0;
  wordsInBatch_ = 0;
  missed_ = 0;
  haveMsg_ = false;
  locked = false;
}

void BatchDecoder::pushBit(int bit) {
  reg_ = (reg_ << 1) | (uint32_t)(bit & 1);
  if (state_ == State::Hunt) {
    for (int inv = 0; inv < 2; inv++) {
      uint32_t ref = inv ? ~SYNC : SYNC;
      if (popcount32(reg_ ^ ref) <= syncErrors_) {
        inverted_ = inv;
        beginBatch();
        return;
      }
    }
    return;
  }
  if (++nbits_ < 32) return;
  nbits_ = 0;
  uint32_t word = inverted_ ? ~reg_ : reg_;
  if (state_ == State::Sync) {
    if (popcount32(word ^ SYNC) <= syncErrors_ + 1) {
      beginBatch();
    } else {
      missed_++;
      finishMessage();
      if (missed_ >= maxMissed_) {
        resetHunt();
      } else {
        beginBatch(true);
      }
    }
    return;
  }
  codeword(word, wordsInBatch_);
  if (++wordsInBatch_ == BATCH_WORDS) {
    state_ = State::Sync;
    wordsInBatch_ = 0;
  }
}

void BatchDecoder::beginBatch(bool missed) {
  if (!missed) {
    missed_ = 0;
    batches++;
    locked = true;
  }
  state_ = State::Batch;
  nbits_ = 0;
  wordsInBatch_ = 0;
}

void BatchDecoder::codeword(uint32_t word, int index) {
  if (word == 0 || word == 0xFFFFFFFFu) {  // Rauschteppich dekodiert als gültige "Adresse 0": kein Ruf
    finishMessage();
    return;
  }
  if (popcount32(word ^ IDLE) <= 3) {  // das eine Codewort, das jeder Empfänger vorab kennt
    codewordsOk++;
    finishMessage();
    return;
  }
  Correction fixed = correctCodeword(word);
  // Ein zufälliges 32-Bit-Wort liegt mit ~26 % Wahrscheinlichkeit innerhalb von 2 Bitfehlern irgendeines
  // Codeworts; eine neue Adresse wird daher nur mit höchstens einem korrigierten Bit akzeptiert.
  // Nach einem verpassten Sync (Sendeende, Rauschen) wird eine neue Adresse nur noch fehlerfrei akzeptiert.
  int maxAddressErrors = missed_ > 0 ? 0 : 1;
  if (fixed.ok && (fixed.codeword >> 31) == 0 && fixed.errors > maxAddressErrors) fixed.ok = false;
  if (!fixed.ok) {
    codewordsBad++;
    if (haveMsg_) {
      msg_.uncorrectable++;
      msg_.payload.insert(msg_.payload.end(), 20, 0);
    }
    return;
  }
  uint32_t cw = fixed.codeword;
  codewordsOk++;
  if (cw == IDLE) {
    finishMessage();
    return;
  }
  if ((cw >> 31) == 0) {
    finishMessage();
    msg_ = Message();
    msg_.ric = (((cw >> 13) & 0x3FFFFu) << 3) | (uint32_t)(index >> 1);
    msg_.function = (cw >> 11) & 3u;
    msg_.corrected = fixed.errors;
    msg_.inverted = inverted_;
    haveMsg_ = true;
  } else if (haveMsg_) {
    uint32_t data = (cw >> 11) & 0xFFFFFu;
    for (int i = 19; i >= 0; i--) msg_.payload.push_back((data >> i) & 1u);
    msg_.corrected += fixed.errors;
  }
}

void BatchDecoder::finishMessage() {
  if (!haveMsg_) return;
  haveMsg_ = false;
  if (cb_) cb_(msg_);
}

}  // namespace pocsag

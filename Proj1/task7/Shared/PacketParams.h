#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <string>
#include <vector>

namespace Params {
const double sampleRate = 48000.0;
const double carrierFreq = 4000.0; // 低频传播更远
const int baudRate = 500;
const int samplesPerSymbol = (int)(sampleRate / baudRate); // 96 samples

// 【配置】100 bits 一个包
const int CHUNK_SIZE = 100;
const int TOTAL_BITS = 10000;

// --- Hamming(7,4) ---
static uint8_t encodeHamming(uint8_t nibble) {
  static const uint8_t table[16] = {0x00, 0x0B, 0x16, 0x1D, 0x2C, 0x27,
                                    0x3A, 0x31, 0x4B, 0x40, 0x5D, 0x56,
                                    0x67, 0x6C, 0x71, 0x7A};
  return table[nibble & 0x0F];
}

static uint8_t decodeHamming(uint8_t byte) {
  int minErr = 100;
  uint8_t bestVal = 0;
  static const uint8_t codes[16] = {0x00, 0x0B, 0x16, 0x1D, 0x2C, 0x27,
                                    0x3A, 0x31, 0x4B, 0x40, 0x5D, 0x56,
                                    0x67, 0x6C, 0x71, 0x7A};
  for (int i = 0; i < 16; ++i) {
    uint8_t x = codes[i] ^ (byte & 0x7F);
    int errs = 0;
    for (int b = 0; b < 7; ++b)
      if ((x >> b) & 1)
        errs++;
    if (errs < minErr) {
      minErr = errs;
      bestVal = i;
    }
  }
  return bestVal;
}

// --- Preamble (0.2s) ---
static std::vector<float> getPreamble() {
  std::vector<float> p;
  int len = (int)(0.2 * sampleRate);
  for (int i = 0; i < len; ++i) {
    float t = (float)i / sampleRate;
    float freq = 1000.0f + (5000.0f * t / 0.2f);
    float val = std::sin(2.0f * juce::MathConstants<float>::pi * freq * t);
    float win =
        0.5f *
        (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * i / len));
    p.push_back(val * 0.5f * win);
  }
  return p;
}
} // namespace Params

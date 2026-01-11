#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <string>
#include <vector>

namespace Params {
const double sampleRate = 48000.0;

// 【关键修改1】降低载波频率到 4kHz
// (人耳和普通麦克风在这个频段更敏感，且波长更长，绕射能力更强)
const double carrierFreq = 4000.0;

// 【关键修改2】大幅降低波特率！
// 1500 -> 300。这意味着每个符号持续 ~3.3ms，足以抵抗大多数房间的回声。
const int baudRate = 300;

const int CHUNK_SIZE = 16;
const int samplesPerSymbol = (int)(sampleRate / baudRate);
const int encodedSymbolsCount = (Params::CHUNK_SIZE / 4) * 7 + 1;
const int dataDurationSamples = encodedSymbolsCount * Params::samplesPerSymbol;

// 【关键修改3】加长前导码和保护间隔
// 前导码从 0.1s 增加到 0.4s，确保在 2米外微弱信号也能积累出足够的相关峰值
const int Preamble_len = 0.4 * sampleRate;
// 保护间隔增加到 0.1s，让前导码的回声彻底消失后再发数据
const int Guard_len = 0.1 * sampleRate;

// 计算总跳过样本数 (用于接收端跳过已解码的包)
const int Sample_to_skip = Preamble_len + Guard_len + dataDurationSamples;

const int TOTAL_BITS = 16;

// --- Hamming(7,4) 保持不变 ---
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

// --- Preamble (加强版) ---
static std::vector<float> getPreamble() {
  std::vector<float> p;
  int len = Preamble_len; // 使用新的长度
  for (int i = 0; i < len; ++i) {
    float t = (float)i / sampleRate;
    // Chirp 信号：从 1kHz 扫到 4kHz (配合载波)
    float freq = 1000.0f + (3000.0f * t / (float)(len / sampleRate));
    float val = std::sin(2.0f * juce::MathConstants<float>::pi * freq * t);

    // 使用 Hann Window 减少频谱泄露
    float win =
        0.5f *
        (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * i / len));
    p.push_back(val * 0.8f * win); // 稍微提高一点前导码音量
  }
  return p;
}
} // namespace Params

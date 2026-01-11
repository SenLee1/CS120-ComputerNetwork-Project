#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <complex>
#include <vector>

namespace Params {
const double sampleRate = 48000.0;

// --- OFDM 参数设置 ---
const int fftOrder = 9;            // 2^9 = 512
const int fftSize = 1 << fftOrder; // 512 个点
const int cpSize = fftSize / 4; // 128 个点 (循环前缀，抗回声干扰)
const int symbolSize = fftSize + cpSize; // 一个完整 OFDM 符号的总样本数

// 子载波配置：避免直流(0)和高频，选择中间频段
// 48000Hz / 512 ≈ 93.75 Hz per bin
// 使用 Bin 16 (~1.5kHz) 到 Bin 65 (~6kHz)
const int startBin = 16;
const int numCarriers = 50;  // 50个有效子载波
const int bitsPerSymbol = 2; // QPSK 调制 (2 bits per carrier)

// 有效负载计算: 每个OFDM符号携带 50 * 2 = 100 bits
// 原始数据分块大小
const int CHUNK_SIZE = 500;
const int TOTAL_BITS = 10000;

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
// --- Preamble (保持 Chirp 信号用于同步) ---
static std::vector<float> getPreamble() {
  std::vector<float> p;
  int len = (int)(0.1 * sampleRate); // 100ms
  for (int i = 0; i < len; ++i) {
    float t = (float)i / sampleRate;
    float freq = 1000.0f + (5000.0f * t / 0.1f);
    float val = std::sin(2.0f * juce::MathConstants<float>::pi * freq * t);
    // 汉宁窗平滑边缘
    float win =
        0.5f *
        (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * i / len));
    p.push_back(val * 0.5f * win);
  }
  return p;
}
} // namespace Params

#include "MainComponent.h"
#include <complex>
#include <fstream>
#include <iostream>

MainComponent::MainComponent() {
  setSize(400, 250);
  addAndMakeVisible(processButton);
  processButton.setButtonText("Stop & Decode");
  processButton.onClick = [this] {
    isRecording = false;
    processRecording();
  };
  addAndMakeVisible(statusLabel);
  statusLabel.setText("Status: Recording...", juce::dontSendNotification);
  setAudioChannels(1, 0);
}

MainComponent::~MainComponent() { shutdownAudio(); }

// 包含缺失的 GUI 函数，防止报错
void MainComponent::paint(juce::Graphics &g) {
  g.fillAll(juce::Colours::black);
}
void MainComponent::resized() {
  processButton.setBounds(100, 80, 200, 60);
  statusLabel.setBounds(10, 10, getWidth() - 20, 30);
}

void MainComponent::prepareToPlay(int, double) {
  recordedAudio.reserve(48000 * 60); // 预留大内存
  recordedAudio.clear();
  isRecording = true;
}

void MainComponent::releaseResources() {}

void MainComponent::getNextAudioBlock(
    const juce::AudioSourceChannelInfo &bufferToFill) {
  if (!isRecording)
    return;
  auto *inData =
      bufferToFill.buffer->getReadPointer(0, bufferToFill.startSample);
  for (int i = 0; i < bufferToFill.numSamples; ++i)
    recordedAudio.push_back(inData[i]);
}

// --- 按照你的思路重写的逻辑 ---
void MainComponent::processRecording() {
  // 1. 基础信息打印
  float totalSeconds = (float)recordedAudio.size() / Params::sampleRate;
  std::cout << "------------------------------------------------" << std::endl;
  std::cout << "Processing " << totalSeconds << "s (Dynamic Hill Climbing)..."
            << std::endl;

  if (totalSeconds < 1.0f) {
    statusLabel.setText("Error: Audio too short", juce::dontSendNotification);
    return;
  }
  statusLabel.setText("Processing...", juce::dontSendNotification);

  auto preambleTemplate = Params::getPreamble();
  int preambleLen = (int)preambleTemplate.size();

  // 留出足够的尾部空间
  int maxSearch = (int)recordedAudio.size() - preambleLen - 1000;

  std::string totalBits = "";
  int chunksFound = 0;
  int expectedChunks =
      (Params::TOTAL_BITS + Params::CHUNK_SIZE - 1) / Params::CHUNK_SIZE;

  // 当前扫描指针
  int scanPtr = 0;

  // 动态搜索范围 (你提到的200点容忍度)
  const int PEAK_TOLERANCE_WINDOW = 200;

  // --- 主循环 ---
  while (scanPtr < maxSearch && chunksFound < expectedChunks) {

    // A. 计算当前点的分数
    float corr = 0.0f, energy = 0.0f;
    int calcLen = std::min(preambleLen, 1000);
    for (int k = 0; k < calcLen; k += 10) {
      float s = recordedAudio[scanPtr + k];
      corr += s * preambleTemplate[k];
      energy += s * s;
    }
    if (energy < 0.00001f)
      energy = 0.00001f;
    float currentScore = (corr * corr) / energy;

    // B. 触发逻辑
    if (currentScore > 0.1f) {
      // ---> 触发了！发现潜在的山坡

      // 初始化“当前最佳候选”
      int bestPeakPtr = scanPtr;
      float bestPeakScore = currentScore;

      // 计数器：自从上次发现更高点后，又检查了多少个点
      int noBetterFoundCount = 0;

      // 临时的检查指针，从 scanPtr 的下一个开始
      int checkPtr = scanPtr + 1;

      // --- C. 爬山循环 (动态延展) ---
      // 只要我们在 Tolernace 范围内没找到更好的，就一直找。
      // 一旦找到更好的，重置 Tolernace 计数器。
      while (noBetterFoundCount < PEAK_TOLERANCE_WINDOW &&
             checkPtr < maxSearch) {

        // 计算 checkPtr 处的分数 (这里最好全精度计算，不要降采样，保证峰值精准)
        float l_corr = 0.0f, l_energy = 0.0f;
        for (int k = 0; k < calcLen; k += 10) {
          float s = recordedAudio[checkPtr + k];
          l_corr += s * preambleTemplate[k];
          l_energy += s * s;
        }
        if (l_energy < 0.00001f)
          l_energy = 0.00001f;
        float checkScore = (l_corr * l_corr) / l_energy;

        // 比较
        if (checkScore > bestPeakScore) {
          // 发现了更高的点！
          bestPeakScore = checkScore;
          bestPeakPtr = checkPtr; // 更新最佳位置

          // 【关键逻辑】重置计数器！
          // 因为我们发现了新高地，我们要以这个新高地为起点，再往后看 200 点
          noBetterFoundCount = 0;
        } else {
          // 没比当前高，计数器增加
          noBetterFoundCount++;
        }

        checkPtr++;
      }

      // 退出上面那个循环意味着：我们已经连续 200 个点没有发现比 bestPeakPtr
      // 更高的了。 所以 bestPeakPtr 就是真正的局部最高峰 (Local Maxima)。

      // // --- D. 能量校验 (确认不是噪音峰值) ---
      // bool isRealSignal = false;
      // int checkStart = bestPeakPtr + preambleLen + 100;
      // float signalEnergy = 0.0f;
      // for (int k = 0; k < 500 && (checkStart + k) < recordedAudio.size();
      // ++k) {
      //   float s = recordedAudio[checkStart + k];
      //   signalEnergy += s * s;
      // }
      // signalEnergy /= 500;
      //
      // if (signalEnergy > 0.0005f) {
      //   isRealSignal = true;
      // }

      // if (isRealSignal) {
      // ---> 锁定并解码
      chunksFound++;
      std::cout << "[LOCKED] Chunk " << chunksFound << " at " << bestPeakPtr
                << " (Score: " << bestPeakScore << ")" << std::endl;

      // --- 解调逻辑 (保持不变) ---
      int readPtr = bestPeakPtr + preambleLen + 480;
      std::vector<std::complex<float>> symbols;
      int symbolsToRead = (Params::CHUNK_SIZE / 4) * 7 + 1;

      for (int k = 0; k < symbolsToRead; ++k) {
        if (readPtr + Params::samplesPerSymbol >= recordedAudio.size())
          break;
        float iSum = 0.0f, qSum = 0.0f;
        for (int s = 0; s < Params::samplesPerSymbol; ++s) {
          float t = (float)readPtr / Params::sampleRate;
          float val = recordedAudio[readPtr];
          float c = std::cos(2.0f * juce::MathConstants<float>::pi *
                             Params::carrierFreq * t);
          float q = std::sin(2.0f * juce::MathConstants<float>::pi *
                             Params::carrierFreq * t);
          iSum += val * c;
          qSum += val * q;
          readPtr++;
        }
        symbols.push_back(std::complex<float>(iSum, qSum));
      }

      std::string chunkData = "";
      std::string rawBits = "";
      for (size_t k = 1; k < symbols.size(); ++k) {
        float dotProd = symbols[k].real() * symbols[k - 1].real() +
                        symbols[k].imag() * symbols[k - 1].imag();
        rawBits += (dotProd < 0) ? '1' : '0';
      }
      for (size_t i = 0; i < rawBits.length(); i += 7) {
        if (i + 7 > rawBits.length())
          break;
        uint8_t code = 0;
        for (int b = 0; b < 7; ++b)
          if (rawBits[i + b] == '1')
            code |= (1 << b);
        uint8_t nibble = Params::decodeHamming(code);
        for (int b = 0; b < 4; ++b)
          chunkData += ((nibble >> b) & 1) ? '1' : '0';
      }
      totalBits += chunkData;

      // --- E. 跳过当前包 ---
      // 跳过 600 bits 对应的长度 (约 55000 点)
      // scanPtr = bestPeakPtr + 55000;
      scanPtr = bestPeakPtr + 28000;

      continue; // 进入下一次主循环
      // }
    }

    // 没触发，或者能量校验失败，快速往后扫
    scanPtr += 10;
  }

  // --- 结尾处理 ---
  std::cout << "Decoded Total Bits: " << totalBits.length() << std::endl;
  if (totalBits.length() > Params::TOTAL_BITS)
    totalBits = totalBits.substr(0, Params::TOTAL_BITS);
  while (totalBits.length() < Params::TOTAL_BITS)
    totalBits += '0';

  std::ofstream outFile("OUTPUT.txt");
  outFile << totalBits;
  outFile.close();

  statusLabel.setText("Done. Saved OUTPUT.txt", juce::dontSendNotification);
}

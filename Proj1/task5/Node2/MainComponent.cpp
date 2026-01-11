#include "MainComponent.h"
#include <algorithm>
#include <complex>
#include <fstream>
#include <iostream>
#include <limits>

MainComponent::MainComponent() {
  setSize(400, 300);
  addAndMakeVisible(processButton);
  processButton.setButtonText("Stop & Decode (Auto-Phase)");
  processButton.onClick = [this] {
    isRecording = false;
    processRecording();
  };
  addAndMakeVisible(statusLabel);
  statusLabel.setText("Status: Recording...", juce::dontSendNotification);
  setAudioChannels(1, 0);
}

MainComponent::~MainComponent() { shutdownAudio(); }

void MainComponent::paint(juce::Graphics &g) {
  g.fillAll(juce::Colours::black);
  g.setColour(juce::Colours::white);
  g.setFont(14.0f);
  g.drawText("Node 2: Final Receiver\n(Auto Phase & Gain)", getLocalBounds(),
             juce::Justification::centred, true);
}

void MainComponent::resized() {
  processButton.setBounds(100, 100, 200, 60);
  statusLabel.setBounds(10, 10, getWidth() - 20, 30);
}

void MainComponent::prepareToPlay(int, double) {
  recordedAudio.reserve(48000 * 60);
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

// --- 辅助：计算 Hamming 错误分 ---
// 返回值：总纠错比特数。如果纠错数越小，说明解码越靠谱。
int calculateHammingError(const std::string &rawBits) {
  int totalErrors = 0;
  static const uint8_t codes[16] = {0x00, 0x0B, 0x16, 0x1D, 0x2C, 0x27,
                                    0x3A, 0x31, 0x4B, 0x40, 0x5D, 0x56,
                                    0x67, 0x6C, 0x71, 0x7A};

  for (size_t i = 0; i + 7 <= rawBits.length(); i += 7) {
    uint8_t received = 0;
    for (int b = 0; b < 7; ++b) {
      if (rawBits[i + b] == '1')
        received |= (1 << b);
    }

    // 寻找最近的合法 Hamming 码
    int minErr = 10;
    for (int c = 0; c < 16; ++c) {
      uint8_t x = codes[c] ^ (received & 0x7F);
      int errs = 0;
      for (int b = 0; b < 7; ++b)
        if ((x >> b) & 1)
          errs++;
      if (errs < minErr)
        minErr = errs;
    }
    totalErrors += minErr;
  }
  return totalErrors;
}

// 旋转并解调 QPSK
std::string decodeWithRotation(const std::vector<std::complex<float>> &symbols,
                               int rotationSteps) {
  std::string decodedBits = "";
  std::complex<float> rotator(1.0f, 0.0f);
  if (rotationSteps == 1)
    rotator = std::complex<float>(0.0f, 1.0f); // 90度
  else if (rotationSteps == 2)
    rotator = std::complex<float>(-1.0f, 0.0f); // 180度
  else if (rotationSteps == 3)
    rotator = std::complex<float>(0.0f, -1.0f); // 270度

  for (const auto &sym : symbols) {
    std::complex<float> fixedSym = sym * rotator;
    int bitHigh = (fixedSym.real() > 0) ? 0 : 1;
    int bitLow = (fixedSym.imag() > 0) ? 0 : 1;
    decodedBits += ((bitHigh & 1) ? '1' : '0');
    decodedBits += ((bitLow & 1) ? '1' : '0');
  }
  return decodedBits;
}

// 执行 Hamming 解码
std::string doHammingDecode(const std::string &rawBits) {
  std::string finalData = "";
  for (size_t i = 0; i + 7 <= rawBits.length(); i += 7) {
    uint8_t code = 0;
    for (int b = 0; b < 7; ++b)
      if (rawBits[i + b] == '1')
        code |= (1 << b);
    uint8_t nibble = Params::decodeHamming(code);
    for (int b = 0; b < 4; ++b)
      finalData += ((nibble >> b) & 1) ? '1' : '0';
  }
  return finalData;
}

void MainComponent::processRecording() {
  if (recordedAudio.size() < 48000) {
    statusLabel.setText("Audio too short", juce::dontSendNotification);
    return;
  }
  statusLabel.setText("Processing with Auto-Phase...",
                      juce::dontSendNotification);

  auto preambleTemplate = Params::getPreamble();
  int preambleLen = (int)preambleTemplate.size();

  // 阈值设定：极大降低阈值
  float preambleEnergyAvg = 0.0f;
  for (float x : preambleTemplate)
    preambleEnergyAvg += x * x;
  preambleEnergyAvg /= preambleLen;
  // 使用 0.2% 的能量阈值 (比之前的 1% 更低)
  float signalThreshold = preambleEnergyAvg * 0.002f;

  int searchEnd = (int)recordedAudio.size() - preambleLen - Params::fftSize * 2;
  int scanPtr = 0;
  int chunksFound = 0;
  std::string totalFileBits = "";

  std::cout << "=== START RECEIVER (Auto-Phase Mode) ===" << std::endl;
  std::cout << "Preamble Avg Energy: " << preambleEnergyAvg << std::endl;
  std::cout << "Signal Threshold: " << signalThreshold << std::endl;

  while (scanPtr < searchEnd) {

    // 1. 粗同步
    float maxScore = 0.0f;
    int peakIndex = -1;
    int limit = std::min(scanPtr + 96000, searchEnd);
    for (int i = scanPtr; i < limit; i += 10) {
      float corr = 0.0f, energy = 0.0f;
      for (int k = 0; k < preambleLen; k += 25) {
        float s = recordedAudio[i + k];
        corr += s * preambleTemplate[k];
        energy += s * s;
      }
      if (energy < 1e-6f)
        energy = 1e-6f;
      float score = (corr * corr) / energy;
      if (score > maxScore) {
        maxScore = score;
        peakIndex = i;
      }
    }
    if (maxScore < 0.1f) {
      scanPtr = limit;
      continue;
    }

    // 2. 精细同步
    int fineStart = std::max(scanPtr, peakIndex - 500);
    int fineEnd =
        std::min((int)recordedAudio.size() - preambleLen, peakIndex + 500);
    int bestSyncPoint = -1;
    float bestFineScore = 0.0f;
    for (int i = fineStart; i < fineEnd; ++i) {
      float corr = 0.0f, energy = 0.0f;
      for (int k = 0; k < preambleLen; k += 5) {
        float s = recordedAudio[i + k];
        corr += s * preambleTemplate[k];
        energy += s * s;
      }
      if (energy < 1e-6f)
        energy = 1e-6f;
      float score = (corr * corr) / energy;
      if (score > bestFineScore) {
        bestFineScore = score;
        bestSyncPoint = i;
      }
    }
    if (bestFineScore < 0.15f) {
      scanPtr = peakIndex + 100;
      continue;
    }

    // 3. 寻找数据头
    int searchStart = bestSyncPoint + preambleLen;
    int dataStartSample = -1;
    int maxSearchRange = 1000;
    int energyWindow = 32;
    for (int i = 0; i < maxSearchRange; ++i) {
      if (searchStart + i + energyWindow >= recordedAudio.size())
        break;
      float localEnergy = 0.0f;
      for (int w = 0; w < energyWindow; ++w)
        localEnergy += recordedAudio[searchStart + i + w] *
                       recordedAudio[searchStart + i + w];
      localEnergy /= energyWindow;
      if (localEnergy > signalThreshold) {
        dataStartSample = searchStart + i;
        break;
      }
    }

    // 即使没找到边缘，也强制使用 480 偏移
    if (dataStartSample == -1)
      dataStartSample = searchStart + 480;

    chunksFound++;

    // 4. 提取符号
    int rawBitsLen = Params::CHUNK_SIZE;
    if (rawBitsLen % 4 != 0)
      rawBitsLen += (4 - (rawBitsLen % 4));
    int encodedLen = (rawBitsLen / 4) * 7;
    int numSymbols = (encodedLen + (Params::numCarriers * 2) - 1) /
                     (Params::numCarriers * 2);

    std::vector<std::complex<float>> packetSymbols;
    std::vector<float> fftTimeBuf(Params::fftSize * 2);

    for (int symIdx = 0; symIdx < numSymbols; ++symIdx) {
      int currentSymStart =
          dataStartSample + symIdx * (Params::cpSize + Params::fftSize);
      int fftStart = currentSymStart + Params::cpSize + 16; // Offset +16

      if (fftStart + Params::fftSize > recordedAudio.size())
        break;
      for (int i = 0; i < Params::fftSize; ++i)
        fftTimeBuf[i] = recordedAudio[fftStart + i];

      forwardFFT.performRealOnlyForwardTransform(fftTimeBuf.data());

      for (int k = 0; k < Params::numCarriers; ++k) {
        int binIndex = Params::startBin + k;
        packetSymbols.push_back(std::complex<float>(
            fftTimeBuf[binIndex * 2], fftTimeBuf[binIndex * 2 + 1]));
      }
    }

    // --- 5. 智能相位选择 (Smart Phase Selection) ---
    // 尝试 4 种旋转，看哪一种的 Hamming 错误率最低
    int bestRotation = 0;
    int minHammingError = std::numeric_limits<int>::max();
    std::string bestRawBits = "";

    for (int r = 0; r < 4; ++r) {
      std::string tempBits = decodeWithRotation(packetSymbols, r);
      int errors = calculateHammingError(tempBits);
      // std::cout << "   Rot " << r << " Error Score: " << errors << std::endl;
      // // 调试用
      if (errors < minHammingError) {
        minHammingError = errors;
        bestRotation = r;
        bestRawBits = tempBits;
      }
    }

    std::cout << ">>> Chunk " << chunksFound
              << " Locked | Phase: " << bestRotation * 90
              << " deg | Error Score: " << minHammingError << std::endl;

    totalFileBits += doHammingDecode(bestRawBits);

    // 6. 跳过
    scanPtr = bestSyncPoint + 20000;
  }

  // 保存文件
  std::cout << "=== FINISHED ===" << std::endl;
  if (totalFileBits.length() > Params::TOTAL_BITS)
    totalFileBits = totalFileBits.substr(0, Params::TOTAL_BITS);
  while (totalFileBits.length() < Params::TOTAL_BITS)
    totalFileBits += '0';

  std::ofstream outFile("OUTPUT.txt");
  outFile << totalFileBits;
  outFile.close();

  juce::String msg = "Done. Found " + juce::String(chunksFound) + " chunks.";
  statusLabel.setText(msg, juce::dontSendNotification);
}

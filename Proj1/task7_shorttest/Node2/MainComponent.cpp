#include "MainComponent.h"
#include <algorithm> // 用于 std::max_element
#include <cmath>
#include <complex>
#include <iostream>
#include <vector>

MainComponent::MainComponent() {
  setSize(400, 250);

  // 1. 设置按钮逻辑：点击停止录音并立即解码
  addAndMakeVisible(processButton);
  processButton.setButtonText("Stop & Decode");
  processButton.onClick = [this] {
    if (isRecording) {
      isRecording = false; // 停止录音
      processRecording();  // 立即处理
    }
  };

  addAndMakeVisible(statusLabel);
  statusLabel.setText("Status: Recording...", juce::dontSendNotification);

  // 开启麦克风输入
  setAudioChannels(1, 0);
}

MainComponent::~MainComponent() { shutdownAudio(); }

void MainComponent::paint(juce::Graphics &g) {
  g.fillAll(juce::Colours::black);
}

void MainComponent::resized() {
  processButton.setBounds(100, 80, 200, 60);
  statusLabel.setBounds(10, 10, getWidth() - 20, 30);
}

void MainComponent::prepareToPlay(int, double) {
  // 准备录音容器，预留大空间防止重分配
  recordedAudio.clear();
  recordedAudio.reserve(48000 * 10);
  isRecording = true;
  statusLabel.setText("Recording... Speak now!", juce::dontSendNotification);
}

void MainComponent::releaseResources() {}

void MainComponent::getNextAudioBlock(
    const juce::AudioSourceChannelInfo &bufferToFill) {
  if (!isRecording)
    return;

  auto *inData =
      bufferToFill.buffer->getReadPointer(0, bufferToFill.startSample);

  // 实时把数据存入 vector
  for (int i = 0; i < bufferToFill.numSamples; ++i) {
    recordedAudio.push_back(inData[i]);
  }
}

// =============================================================
//  核心解码逻辑 (直接处理内存数据)
// =============================================================
void MainComponent::processRecording() {
  std::cout << "\n=== STOPPED. STARTING DECODE ===" << std::endl;
  std::cout << "Total Samples Recorded: " << recordedAudio.size() << std::endl;

  if (recordedAudio.size() < 48000) {
    statusLabel.setText("Error: Audio too short!", juce::dontSendNotification);
    return;
  }

  // --- Step 1: 自动增益控制 (Normalize) ---
  // 找到录音中的最大绝对值，把整个波形拉大，防止信号太弱算不出相关性
  float maxVal = 0.0f;
  for (float s : recordedAudio) {
    if (std::abs(s) > maxVal)
      maxVal = std::abs(s);
  }

  if (maxVal < 0.001f) {
    std::cout << "[ERROR] Input is silent (Max Amplitude < 0.001)" << std::endl;
    statusLabel.setText("Error: Silence recorded.", juce::dontSendNotification);
    return;
  }

  std::cout << "[Step 1] Normalizing... (Max input was: " << maxVal << ")"
            << std::endl;
  float gain = 0.5f / maxVal; // 放大到 0.5 的幅度
  for (auto &s : recordedAudio)
    s *= gain;

  // --- Step 2: 寻找 Preamble (同步) ---
  auto preamble = Params::getPreamble();
  int preambleLen = (int)preamble.size();

  float globalMaxScore = 0.0f;
  int peakIndex = 0;

  // 扫描：每隔 5 个点计算一次，兼顾速度和精度
  // 注意：我们只扫描前面 80% 的数据，防止越界
  int scanLimit = (int)recordedAudio.size() - preambleLen - 2000;
  if (scanLimit < 0)
    scanLimit = 0;

  for (int i = 0; i < scanLimit; i += 5) {
    float corr = 0.0f;
    float energy = 0.0f;

    // 快速相关计算 (只算前 600 点)
    int calcLen = std::min(preambleLen, 600);

    for (int k = 0; k < calcLen; k += 4) {
      float s = recordedAudio[i + k];
      float p = preamble[k];
      corr += s * p;
      energy += s * s;
    }

    if (energy < 0.00001f)
      energy = 0.00001f;
    float score = (corr * corr) / energy;

    if (score > globalMaxScore) {
      globalMaxScore = score;
      peakIndex = i;
    }
  }

  std::cout << "[Step 2] Sync Check. Max Score: " << globalMaxScore
            << " at index " << peakIndex << std::endl;

  // --- 阈值判定 ---
  // 如果分数依然很低，说明频率对不上，或者全是噪音
  if (globalMaxScore < 0.05f) {
    statusLabel.setText(
        "Signal Not Found (Score: " + juce::String(globalMaxScore, 3) + ")",
        juce::dontSendNotification);
    std::cout << "[FAIL] Score too low. Check Params (Freq/Baud) or Volume."
              << std::endl;
    return;
  }

  statusLabel.setText("Signal Found! Decoding...", juce::dontSendNotification);

  // --- Step 3: 解调 (Demodulation) ---
  // 这里的 offset 必须和发送端严格对应：Peak + Preamble长度 + Guard长度
  int readPtr = peakIndex + preambleLen + Params::Guard_len;

  // 计算我们要读多少个符号 (16 bits -> 4 nibbles -> 28 bits -> 29 symbols w/
  // reference)
  int symbolsToRead = (16 / 4) * 7 + 1;

  std::vector<std::complex<float>> rxSymbols;

  std::cout << "[Step 3] Demodulating starting at sample " << readPtr
            << std::endl;

  for (int k = 0; k < symbolsToRead; ++k) {
    float iSum = 0.0f, qSum = 0.0f;

    // 积分周期
    for (int s = 0; s < Params::samplesPerSymbol; ++s) {
      if (readPtr >= recordedAudio.size())
        break;

      float t = (float)readPtr / Params::sampleRate;
      float val = recordedAudio[readPtr];

      // 正交下变频
      float c = std::cos(2.0f * juce::MathConstants<float>::pi *
                         Params::carrierFreq * t);
      float q = std::sin(2.0f * juce::MathConstants<float>::pi *
                         Params::carrierFreq * t);

      iSum += val * c;
      qSum += val * q;
      readPtr++;
    }
    rxSymbols.push_back({iSum, qSum});
  }

  // --- Step 4: 差分与汉明解码 ---
  std::string decodedData = "";
  std::string currentSevenBits = "";

  for (size_t k = 1; k < rxSymbols.size(); ++k) {
    // 差分相干解调：Dot Product
    float dot = rxSymbols[k].real() * rxSymbols[k - 1].real() +
                rxSymbols[k].imag() * rxSymbols[k - 1].imag();

    char bit = (dot < 0) ? '1' : '0'; // 反相为1，同相为0
    currentSevenBits += bit;

    if (currentSevenBits.length() == 7) {
      // 汉明解码 (7,4)
      uint8_t code = 0;
      for (int b = 0; b < 7; ++b) {
        if (currentSevenBits[b] == '1')
          code |= (1 << b);
      }

      uint8_t nibble = Params::decodeHamming(code);

      // 提取 4 bits
      for (int b = 0; b < 4; ++b) {
        decodedData += ((nibble >> b) & 1) ? '1' : '0';
      }
      currentSevenBits = "";
    }
  }

  std::cout << "[Result] Final Decoded: " << decodedData << std::endl;

  // 验证
  std::string expected = "1010101010101010";
  if (decodedData == expected) {
    statusLabel.setText("Success! " + decodedData, juce::dontSendNotification);
    std::cout << ">>> SUCCESS! Data Verified. <<<" << std::endl;
  } else {
    statusLabel.setText("Mismatch: " + decodedData, juce::dontSendNotification);
    std::cout << ">>> MISMATCH. Expected 1010... <<<" << std::endl;
  }
}

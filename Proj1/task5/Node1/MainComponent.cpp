#include "MainComponent.h"
#include <fstream>
#include <iostream>

MainComponent::MainComponent() {
  setSize(400, 200);
  addAndMakeVisible(sendButton);
  sendButton.setButtonText("Send Signal");
  sendButton.onClick = [this] {
    generateSignal();
    isTransmitting = true;
    playPosition = 0;
  };
  setAudioChannels(0, 2);
}

MainComponent::~MainComponent() { shutdownAudio(); }

void MainComponent::prepareToPlay(int samplesPerBlockExpected,
                                  double sampleRate) {}
void MainComponent::releaseResources() {}

void MainComponent::getNextAudioBlock(
    const juce::AudioSourceChannelInfo &bufferToFill) {
  bufferToFill.clearActiveBufferRegion();
  if (!isTransmitting)
    return;

  auto *left =
      bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample);
  auto *right =
      bufferToFill.buffer->getWritePointer(1, bufferToFill.startSample);

  for (int i = 0; i < bufferToFill.numSamples; ++i) {
    if (playPosition < transmissionSignal.size()) {
      float val = transmissionSignal[playPosition++];
      left[i] = val;
      right[i] = val;
    } else {
      isTransmitting = false;
      break;
    }
  }
}

void MainComponent::generateSignal() {
  transmissionSignal.clear();

  std::ifstream inFile(
      "D:\\Courses\\4.1\\CN\\Project_Local\\Project1\\task4\\INPUT.txt");

  std::string allDataBits;
  if (inFile)
    inFile >> allDataBits;
  else
    allDataBits = std::string(10000, '1');

  std::cout << "length of INPUT: " << allDataBits.length();
  // 截断或补齐
  if (allDataBits.length() > Params::TOTAL_BITS) {
    allDataBits = allDataBits.substr(0, Params::TOTAL_BITS);
  }
  while (allDataBits.length() < Params::TOTAL_BITS)
    allDataBits += '0';

  auto preamble = Params::getPreamble();

  int processedBits = 0;
  int chunkIndex = 0;

  // 分包循环
  while (processedBits < allDataBits.length()) {
    std::string chunkBits =
        allDataBits.substr(processedBits, Params::CHUNK_SIZE);
    processedBits += chunkBits.length();
    chunkIndex++;

    // A. 插入静音 Gap (0.1s)
    for (int i = 0; i < (int)(0.015 * Params::sampleRate); ++i)
      transmissionSignal.push_back(0.0f);

    // B. 插入 Preamble
    transmissionSignal.insert(transmissionSignal.end(), preamble.begin(),
                              preamble.end());

    // Guard (10ms)
    for (int i = 0; i < 480; ++i)
      transmissionSignal.push_back(0.0f);

    // C. DPSK 调制
    float currentPhase = 0.0f;
    // 参考符号
    for (int s = 0; s < Params::samplesPerSymbol; ++s) {
      float t = (float)s / Params::sampleRate;
      transmissionSignal.push_back(
          std::sin(2.0f * juce::MathConstants<float>::pi * Params::carrierFreq *
                       t +
                   currentPhase) *
          0.7f);
    }

    std::string bitsToEncode = chunkBits;
    while (bitsToEncode.length() % 4 != 0)
      bitsToEncode += '0';

    for (size_t i = 0; i < bitsToEncode.length(); i += 4) {
      uint8_t nibble = 0;
      for (int b = 0; b < 4; ++b) {
        if (bitsToEncode[i + b] == '1')
          nibble |= (1 << b);
      }
      uint8_t encoded7 = Params::encodeHamming(nibble);

      for (int bitIdx = 0; bitIdx < 7; ++bitIdx) {
        bool bitVal = (encoded7 >> bitIdx) & 1;
        if (bitVal)
          currentPhase += juce::MathConstants<float>::pi;

        for (int s = 0; s < Params::samplesPerSymbol; ++s) {
          float t = (float)s / Params::sampleRate;
          float val = std::sin(2.0f * juce::MathConstants<float>::pi *
                                   Params::carrierFreq * t +
                               currentPhase);
          transmissionSignal.push_back(val * 0.7f);
        }
      }
    }
  }

  // 尾部静音 (3秒) - 确保发完
  for (int i = 0; i < 48000 * 3; ++i)
    transmissionSignal.push_back(0.0f);

  std::cout << "Generated " << chunkIndex << " chunks. Ready to send."
            << std::endl;
}

void MainComponent::paint(juce::Graphics &g) {
  g.fillAll(juce::Colours::black);
}
void MainComponent::resized() { sendButton.setBounds(10, 10, 150, 40); }

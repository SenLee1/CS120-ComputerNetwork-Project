#pragma once
#include "../Shared/PacketParams.h"
#include <JuceHeader.h>

class MainComponent : public juce::AudioAppComponent {
public:
  MainComponent();
  ~MainComponent() override;

  void prepareToPlay(int, double) override;
  void
  getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override;
  void releaseResources() override;
  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  std::vector<float> preRollBuffer; // 用于保存触发前的那一小段声音
  bool hasTriggered = false;        // 是否已经开始录音
  int silenceCounter = 0;           // 连续静音计数器
  const float TRIGGER_THRESHOLD = 0.01f; // 触发阈值 (音量大于这个才算开始)
  const int SAMPLE_RATE = 48000;
  void processRecording(); // 处理录音数据

  juce::TextButton processButton{"Stop & Decode"};
  juce::Label statusLabel;

  // 大容量 Buffer
  std::vector<float> recordedAudio;
  bool isRecording = true;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

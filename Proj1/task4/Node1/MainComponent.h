#pragma once
#include "../Shared/PacketParams.h"
#include <JuceHeader.h>

class MainComponent : public juce::AudioAppComponent {
public:
  MainComponent();
  ~MainComponent() override;

  void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
  void
  getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override;
  void releaseResources() override;
  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  void generateSignal(); // 核心生成函数

  juce::TextButton sendButton{"Send INPUT.txt"};
  std::vector<float> transmissionSignal;
  int playPosition = 0;
  bool isTransmitting = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

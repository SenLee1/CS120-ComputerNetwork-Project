#pragma once

#include <JuceHeader.h>
#include <fstream>
#include <vector>
#include <deque>

using namespace std;
using namespace juce;

#define BIT_WIDTH 8
#define BIT_NUM 50000
#define PREAMBLE_LENGTH 80
#define PREAMBLE_FREQ_BEGIN 5000
#define PREAMBLE_FREQ_END 10000
#define SUM_THRESHOLD 15
#define FREQ 600
#define PACKAGE_BITS 400
#define PACKAGE_LENGTH ((PACKAGE_BITS*BIT_WIDTH)+PREAMBLE_LENGTH)
#define DELAY_BITS 1200


class MainComponent : public juce::AudioAppComponent {
public:
	MainComponent();
	~MainComponent() override;
	void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
	void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
	void releaseResources() override;

private:
	enum NodeState
	{
		Ready,
		ToSend,
		Sending,
		ToReceive,
		Receiving,
		Received,
		AckSending,
		AckReceiving,
		Timeout
	};

	void changeState(NodeState newState) {
		if (state != newState)
		{
			switch (newState)
			{
			case Ready:
				openButton.setEnabled(true);
				sendButton.setEnabled(false);
				receiveButton.setEnabled(false);
				receiveButton.setColour(juce::TextButton::buttonColourId, juce::Colours::blue);
				receiveButton.setButtonText("Receive");
				saveButton.setEnabled(false);
				break;

			case ToSend:
				openButton.setEnabled(true);
				sendButton.setEnabled(true);
				sendButton.setColour(juce::TextButton::buttonColourId, juce::Colours::green);
				sendButton.setButtonText("Send");
				receiveButton.setEnabled(false);
				saveButton.setEnabled(false);
				break;

			case ToReceive:
				openButton.setEnabled(true);
				sendButton.setEnabled(false);
				receiveButton.setEnabled(true);
				saveButton.setEnabled(false);
				break;

			case Sending:
				openButton.setEnabled(false);
				sendButton.setEnabled(false);
				if (state == Ready)
					receiveButton.setEnabled(false);
				saveButton.setEnabled(false);
				break;

			case Receiving:
				openButton.setEnabled(false);
				sendButton.setEnabled(false);
				receiveButton.setEnabled(true);
				saveButton.setEnabled(false);
				break;

			case Received:
				openButton.setEnabled(false);
				sendButton.setEnabled(false);
				receiveButton.setEnabled(false);
				saveButton.setEnabled(true);
				break;

			case Timeout:
				openButton.setEnabled(true);
				sendButton.setEnabled(false);
				sendButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red);
				sendButton.setButtonText("Timeout");
				receiveButton.setEnabled(false);
				receiveButton.setColour(juce::TextButton::buttonColourId, juce::Colours::blue);
				receiveButton.setButtonText("Receive");
				saveButton.setEnabled(false);
				break;

			case AckSending:
			case AckReceiving:
			default:
				break;
			}
			state = newState;
		}
	}

	void openButtonClicked()
	{
		changeState(Ready);
		sentData.clear();
		auto path = new juce::FileChooser("Select Input or Output Path", File::getSpecialLocation(File::SpecialLocationType::userDesktopDirectory));
		auto mode = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories;

		path->launchAsync(mode, [this](const juce::FileChooser& fc)
		{
			auto file = fc.getResult();
			if (file != juce::File{})
			{
				auto path = fc.getResult().getFullPathName();

				if (path.contains(".bin"))
				{
					char currentByte;
					ifstream inputFile(path.toStdString(), ios::in | ios::binary);
					sentData.clear();
					for (int i = 0; i < BIT_NUM / 8; ++i)
					{
						if (i % (PACKAGE_BITS / 8) == 0)
						{
							for (int j = 0; j < DELAY_BITS; ++j)
							{
								sentData.push_back(0);
							}
							sentData.insert(sentData.end(), preambleWave.begin(), preambleWave.end());
						}
						inputFile.get(currentByte);
						for (int j = 7; j >= 0; --j) {
							if (currentByte >> j & 1)
								sentData.insert(sentData.end(), carrierWave.begin(), carrierWave.end());
							else
								sentData.insert(sentData.end(), zeroWave.begin(), zeroWave.end());
						}
					}
					inputFile.close();
					changeState(ToSend);
				}
				else if (!path.contains(".")) {
					outpath = path.toStdString() + "/OUTPUT.bin";
					changeState(ToReceive);
				}
			}
		});
	}

	void sendButtonClicked()
	{
		isReceiving = false;
		changeState(Sending);
	}

	void receiveButtonClicked()
	{
		isReceiving = true;
		outputData.clear();
		switch (state)
		{
		case ToReceive:
		{
			receivedData.clear();
			receiveButton.setButtonText("Stop");
			receiveButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red);
			changeState(Receiving);
			break;
		}

		case Receiving:
			receiveButton.setButtonText("Receive");
			receiveButton.setColour(juce::TextButton::buttonColourId, juce::Colours::blue);
			changeState(Ready);
			break;
		}
	}

	void saveButtonClicked() {
		char outputByte = 0;
		int bits = 0;
		float totalAmp = 0;
		ofstream outputFile(outpath, ios::out | ios::binary);
		for (int idx = 0; idx < BIT_NUM; idx++) {
			totalAmp = 0;
			outputByte <<= 1;
			for (int i = 0; i < BIT_WIDTH; ++i)
				totalAmp += outputData[idx * BIT_WIDTH + i];
			outputByte += (totalAmp > 0) ? 1 : 0;
			if (++bits % 8 == 0)
			{
				output += outputByte;
				outputByte = 0;
			}
		}
		outputFile << output;
		changeState(Ready);
	}

	void setSampleBuffer(juce::AudioSampleBuffer* sampleBuffer)
	{
		this->sampleBuffer = sampleBuffer;
	}

	void setSampleRate(double sampleRate)
	{
		this->sampleRate = sampleRate;
	}

	void setReadPointer(int readPointer)
	{
		this->readPointer = readPointer;
	}

	void setWritePointer(int writePointer)
	{
		this->writePointer = writePointer;
	}

	NodeState state;
	TextButton openButton, sendButton, receiveButton, saveButton;
	AudioSampleBuffer* sampleBuffer = nullptr;
	double sampleRate = 48000;
	int readPointer = 0, writePointer = 0;

	vector<float> carrierWave, zeroWave;
	vector<float> sentData, outputData;
	deque<float> receivedData;
	float sum = 0;
	float maxSum = SUM_THRESHOLD;
	int lowerTicks = 0;
	int timeoutTicks = 0;
	int bitsReceived = 0;
	int packagesReceived = 0;
	bool begin = false;
	bool preambleSuspected = false;
	vector<float> preambleWave, ackWave;
	bool isReceiving;
	string outpath, output = "";

	std::unique_ptr<juce::FileLogger> fileLogger;
	void setupFileLogger();

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
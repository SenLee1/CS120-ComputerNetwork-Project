#include "MainComponent.h"

#define LOG(...) juce::Logger::writeToLog(juce::String::formatted(__VA_ARGS__))

void MainComponent::setupFileLogger()
{
	juce::String currentFile = __FILE__;
	juce::File sourceFile(currentFile);
	juce::File logFile = sourceFile.getParentDirectory().getChildFile("audio_debug.log");

	fileLogger.reset(new juce::FileLogger(logFile, "Audio Communication Debug"));
	juce::Logger::setCurrentLogger(fileLogger.get());

	LOG("=== Initialize ===");
	LOG("File Location: %s", logFile.getFullPathName().toRawUTF8());
}


MainComponent::MainComponent() {
	setupFileLogger();
	LOG("Application Start");

	setSize(600, 390);
	setAudioChannels(1, 1);

	openButton.setButtonText("Open");
	openButton.setSize(500, 60);
	openButton.setTopLeftPosition(50, 30);
	openButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgreen);
	openButton.onClick = [this] { openButtonClicked(); };
	addAndMakeVisible(openButton);

	sendButton.setButtonText("Send");
	sendButton.setSize(500, 60);
	sendButton.setTopLeftPosition(50, 120);
	sendButton.setColour(juce::TextButton::buttonColourId, juce::Colours::green);
	sendButton.onClick = [this] { sendButtonClicked(); };
	addAndMakeVisible(sendButton);

	receiveButton.setButtonText("Receive");
	receiveButton.setSize(500, 60);
	receiveButton.setTopLeftPosition(50, 210);
	receiveButton.setColour(juce::TextButton::buttonColourId, juce::Colours::blue);
	receiveButton.onClick = [this] { receiveButtonClicked(); };
	addAndMakeVisible(receiveButton);

	saveButton.setButtonText("Save");
	saveButton.setSize(500, 60);
	saveButton.setTopLeftPosition(50, 300);
	saveButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgreen);
	saveButton.onClick = [this] { saveButtonClicked(); };
	addAndMakeVisible(saveButton);

	changeState(Ready);
}

MainComponent::~MainComponent()
{
	LOG("=== Application Ends ===");
	juce::Logger::setCurrentLogger(nullptr);
	shutdownAudio();
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
	carrierWave.clear();
	zeroWave.clear();
	preambleWave.clear();
	ackWave.clear();
	float phase = 0;
	for (int i = 0; i < BIT_WIDTH; ++i)
	{
		carrierWave.push_back(static_cast<float>(0.5 * cos(2 * juce::MathConstants<double>::pi * FREQ * i / 48000)));
		zeroWave.push_back(static_cast<float>(0.5 * cos(2 * juce::MathConstants<double>::pi * FREQ * i / 48000 + juce::MathConstants<double>::pi)));
	}
	for (int i = 0; i < PREAMBLE_LENGTH; ++i)
	{
		preambleWave.push_back(cos(phase));
		phase += static_cast<float>(juce::MathConstants<float>::twoPi * (PREAMBLE_FREQ_BEGIN + i * (PREAMBLE_FREQ_END - PREAMBLE_FREQ_BEGIN) / (PREAMBLE_LENGTH - 1)) / 48000.0f);
	}
	for (int i = 0; i < DELAY_BITS; ++i)
	{
		ackWave.push_back(0);
	}
	for (int i = 0; i < PREAMBLE_LENGTH; ++i)
	{
		ackWave.push_back(preambleWave[PREAMBLE_LENGTH - i - 1]);
	}
	receivedData.clear();
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
	
	auto* device = deviceManager.getCurrentAudioDevice();
	static int callCount = 0;
	callCount++;
	auto bufferSize = bufferToFill.buffer->getNumSamples();
	auto activeInputChannels = device->getActiveInputChannels();
	auto activeOutputChannels = device->getActiveOutputChannels();
	auto maxInputChannels = activeInputChannels.getHighestBit() + 1;
	auto maxOutputChannels = activeOutputChannels.getHighestBit() + 1;

	for (auto actualOutputChannel = 0; actualOutputChannel < maxOutputChannels; ++actualOutputChannel)
	{

		if ((!activeInputChannels[actualOutputChannel] || !activeOutputChannels[actualOutputChannel]) || maxInputChannels == 0)
		{
			bufferToFill.buffer->clear(actualOutputChannel, bufferToFill.startSample, bufferToFill.numSamples);
		}
		else
		{
			switch (state)
			{
			case Sending:
				bufferToFill.buffer->clear();
				for (int i = 0; i < bufferSize; ++i) {
					if (readPointer < sentData.size()) {
						bufferToFill.buffer->addSample(actualOutputChannel, i, sentData[readPointer]);
						if ((++readPointer % (PACKAGE_LENGTH + DELAY_BITS)) == 0)
						{
							changeState(AckReceiving);
						}
					}
					else
					{
						setReadPointer(0);
						changeState(Ready);
						break;
					}
				}
				break;

			case Receiving:
				for (int sample = bufferToFill.startSample; sample < bufferToFill.startSample + bufferToFill.numSamples; ++sample)
				{
					float value = bufferToFill.buffer->getSample(0, sample);
					LOG("Count %d: [%d][%d] = %.6f", callCount, 0, sample, value);
				}

				if (packagesReceived == BIT_NUM / PACKAGE_BITS)
				{
					packagesReceived = 0;
					changeState(Received);
					break;
				}
				for (int i = 0; i < bufferSize; ++i)
				{
				 	receivedData.push_back(bufferToFill.buffer->getSample(actualOutputChannel, i));
				}
				while (!begin && receivedData.size() >= PREAMBLE_LENGTH) {
					sum = 0;
					for (int i = 0; i < PREAMBLE_LENGTH; ++i)
						sum += receivedData[i] * preambleWave[i];
					if (sum > maxSum) {
						maxSum = sum;
						lowerTicks = 0;
						preambleSuspected = true;
					}
					else if (preambleSuspected && ++lowerTicks >= PREAMBLE_LENGTH) {
						begin = true;
						preambleSuspected = false;
						break;
					}
					receivedData.pop_front();
				}
				while (begin && receivedData.size()) {
					outputData.push_back(receivedData.front());
					receivedData.pop_front();
					if (++bitsReceived == PACKAGE_BITS * BIT_WIDTH) {
						maxSum = SUM_THRESHOLD;
						bitsReceived = 0;
						begin = false;
						if (packagesReceived++ < BIT_NUM / PACKAGE_BITS)
							changeState(AckSending);
						break;
					}
				}
				bufferToFill.buffer->clear();
				break;

			case AckSending:
				bufferToFill.buffer->clear();
				for (int i = 0; i < bufferSize; ++i, ++readPointer) {
					if (readPointer < ackWave.size())
						bufferToFill.buffer->addSample(actualOutputChannel, i, ackWave[readPointer]);
					else {
						setReadPointer(0);
						changeState(Receiving);
						break;
					}
				}
				break;

			case AckReceiving:
				for (int i = 0; i < bufferSize; ++i)
					receivedData.push_back(bufferToFill.buffer->getSample(actualOutputChannel, i));
				while (receivedData.size() >= PREAMBLE_LENGTH) {
					sum = 0;
					for (int i = 0; i < PREAMBLE_LENGTH; ++i)
						sum += receivedData[i] * ackWave[DELAY_BITS + i];
					if (sum > maxSum) {
						maxSum = sum;
						lowerTicks = 0;
						preambleSuspected = true;
						timeoutTicks = 0;
					}
					else if (preambleSuspected && ++lowerTicks >= PREAMBLE_LENGTH) {
						preambleSuspected = false;
						maxSum = SUM_THRESHOLD;
						lowerTicks = 0;
						receivedData.clear();
						changeState(Sending);
						break;
					}
					else if (!preambleSuspected) {
						if (++timeoutTicks > 96000) {
							receivedData.clear();
							timeoutTicks = 0;
							changeState(Timeout);
							break;
						}
					}
					receivedData.pop_front();
				}
				bufferToFill.buffer->clear();
				break;

			default:
				bufferToFill.buffer->clear();
				break;
			}
		}
	}
}

void MainComponent::releaseResources()
{
	delete sampleBuffer;
}
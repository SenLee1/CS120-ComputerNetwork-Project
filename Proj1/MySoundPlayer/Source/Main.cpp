#include <JuceHeader.h>
#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cmath>

using namespace juce;

class IntegratedAudioSystem : public AudioIODeviceCallback {
private:
    std::vector<float> recordedData;    // data for recorded audio
    std::vector<float> mp3Buffer;       // data for MP3 audio
    std::vector<float> generatedWave;   // data for generated sound wave

    std::atomic<size_t> playPosition{ 0 };   // current playback position
    std::atomic<size_t> mp3PlayPosition{ 0 }; // current MP3 playback position
    std::atomic<size_t> wavePlayPosition{ 0 }; // current generated wave playback position

    // current states 
	// There are 4 states: For project 0, isRecording and isPlayingMP3 can be true at the same time.
    // when performing function 2
    // For project 1, is PlayingGeneratedWave is used for task 2
    std::atomic<bool> isRecording{ false };
    std::atomic<bool> isPlaying{ false };
    std::atomic<bool> isPlayingMP3{ false };
    std::atomic<bool> isPlayingGeneratedWave{ false };

    std::atomic<size_t> recordingDuration{ 0 };
    std::atomic<size_t> maxRecordingSamples{ 0 };
    std::atomic<bool> autoStopEnabled{ false };

    // 音频格式管理
    AudioFormatManager formatManager;

public:
    IntegratedAudioSystem() {
        formatManager.registerBasicFormats();
    }

    ~IntegratedAudioSystem() {}

    // ================== 功能1 ===============

    // 1: record for 10s
    void startRecording10Seconds(int sampleRate = 48000) {
        recordedData.clear();
        isRecording = true;
        isPlaying = false;
        isPlayingMP3 = false;
        isPlayingGeneratedWave = false;
        playPosition = 0;
        recordingDuration = 0;
        maxRecordingSamples = sampleRate * 10;
        autoStopEnabled = true;

        std::cout << "Start recording..." << std::endl;
        std::cout << "Please speak..." << std::endl;

        startTimer(10);
    }

    // ================= 功能2==================

    // load the MP3 file
    bool loadMP3File(const String& filePath) {
        mp3Buffer.clear();
        mp3PlayPosition = 0;

        File audioFile(filePath);
        if (!audioFile.existsAsFile()) {
            std::cout << "error: file not exist: " << filePath << std::endl;
            return false;
        }

        std::unique_ptr<AudioFormatReader> reader(formatManager.createReaderFor(audioFile));
        if (reader == nullptr) {
            std::cout << "error: unknown file type" << std::endl;
            return false;
        }

        std::cout << "音频文件信息:" << std::endl;
        std::cout << " - 采样率: " << reader->sampleRate << " Hz" << std::endl;
        std::cout << " - 声道数: " << reader->numChannels << std::endl;
        std::cout << " - 时长: " << (reader->lengthInSamples / reader->sampleRate) << " s" << std::endl;

        // 读取并转换为单声道
        size_t totalSamples = reader->lengthInSamples;
        mp3Buffer.resize(totalSamples);

        AudioBuffer<float> tempBuffer(reader->numChannels, totalSamples);
        reader->read(&tempBuffer, 0, totalSamples, 0, true, true);

        for (size_t i = 0; i < totalSamples; ++i) {
            float mixedSample = 0.0f;
            for (int ch = 0; ch < reader->numChannels; ++ch) {
                mixedSample += tempBuffer.getSample(ch, i);
            }
            mp3Buffer[i] = mixedSample / reader->numChannels;
        }

        std::cout << "Audio file loaded successfully" << std::endl;
        return true;
    }

    // 2: 播放MP3并同时录音10秒
    void startPlayMP3AndRecord(int sampleRate = 48000) {
        if (mp3Buffer.empty()) {
            std::cout << "error: please load the mp3 file first" << std::endl;
            return;
        }

        recordedData.clear();
        isRecording = true;
        isPlayingMP3 = true;
        isPlaying = false;
        isPlayingGeneratedWave = false;
        playPosition = 0;
        mp3PlayPosition = 0;
        recordingDuration = 0;
        maxRecordingSamples = sampleRate * 10;
        autoStopEnabled = true;

        std::cout << "Playing the MP3 audio and recording..." << std::endl;
        std::cout << "Please speak..." << std::endl;

        startTimer(10);
    }

    // ================== 功能3: 生成声波 ================

    // 生成自定义频率为 FREQ1, FREQ2 的声波
    void generateCustomWave(double freq1, double freq2, double duration = 5.0, int sampleRate = 48000) {
        generatedWave.clear();
        wavePlayPosition = 0;

        size_t totalSamples = static_cast<size_t>(duration * sampleRate);
        generatedWave.resize(totalSamples);

        std::cout << "Generating custom sound wave: f(t) = sin(2π·" << freq1 << "t) + sin(2π·" << freq2 << "t)" << std::endl;
        std::cout << "Duration: " << duration << " seconds" << std::endl;
        std::cout << "Sample rate: " << sampleRate << " Hz" << std::endl;

        for (size_t i = 0; i < totalSamples; ++i) {
            double t = static_cast<double>(i) / sampleRate;
            double sample = std::sin(2.0 * MathConstants<double>::pi * freq1 * t)
                + std::sin(2.0 * MathConstants<double>::pi * freq2 * t);
            generatedWave[i] = static_cast<float>(sample * 0.5);
        }

        isPlayingGeneratedWave = true;
        isRecording = false;
        isPlaying = false;
        isPlayingMP3 = false;

        std::cout << "Custom sound wave generated successfully" << std::endl;
        std::cout << "Playing custom sound wave..." << std::endl;

        std::thread([this, duration]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(duration * 1000)));
            if (isPlayingGeneratedWave) {
                isPlayingGeneratedWave = false;
                std::cout << "Custom wave playback finished" << std::endl;
            }
            }).detach();
    }

    // =================== 通用控制函数 ================

    // 启动定时器
    void startTimer(int seconds) {
        std::thread([this, seconds]() {
            for (int i = seconds; i > 0; --i) {
                if (!isRecording && !isPlayingMP3) break;

                std::cout << "Remaining time: " << i << " s" << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            // 时间到，自动停止
            if (isRecording) {
                stopAll();
                std::cout << "10s has been reached，recording stopped" << std::endl;
            }
            }).detach();
    }

    // 停止所有活动
    void stopAll() {
        isRecording = false;
        isPlaying = false;
        isPlayingMP3 = false;
        isPlayingGeneratedWave = false;
        autoStopEnabled = false;
        std::cout << "All activities have been stopped" << std::endl;
        std::cout << "Final sample #: " << recordedData.size() << std::endl;
    }

    // 播放录音
    void startPlayingRecording() {
        if (recordedData.empty()) {
            std::cout << "error：no record audio, please record first" << std::endl;
            return;
        }
        isPlaying = true;
        isRecording = false;
        isPlayingMP3 = false;
        isPlayingGeneratedWave = false;
        playPosition = 0;
        std::cout << "Start playing the audio..." << std::endl;
    }

    // 停止播放
    void stopPlaying() {
        isPlaying = false;
        isPlayingMP3 = false;
        isPlayingGeneratedWave = false;
        std::cout << "Stop playing the audio..." << std::endl;
    }

    // 音量限制函数
    float limitVolume(float sample, float min = -1.0f, float max = 1.0f) {
        if (sample < min) return min;
        if (sample > max) return max;
        return sample;
    }

    // ================ 音频回调核心函数 ===============

    void audioDeviceIOCallback(const float** inputChannelData,
        int numInputChannels,
        float** outputChannelData,
        int numOutputChannels,
        int numSamples) override {

        // 1. 录音部分
        if (isRecording && inputChannelData != nullptr && numInputChannels > 0) {
            for (int i = 0; i < numSamples; i++) {
                if (autoStopEnabled && recordingDuration >= maxRecordingSamples) {
                    stopAll();
                    break;
                }

                float inputSample = inputChannelData[0][i];
                recordedData.push_back(inputSample);
                recordingDuration++;
            }
        }

        // 2. 输出部分
        if (outputChannelData != nullptr && numOutputChannels > 0) {
            for (int i = 0; i < numSamples; i++) {
                float outputSample = 0.0f;

                // 播放MP3音乐
                if (isPlayingMP3 && mp3PlayPosition < mp3Buffer.size()) {
                    outputSample += mp3Buffer[mp3PlayPosition] * 0.7f;
                    mp3PlayPosition++;
                }

                // 播放录音
                if (isPlaying && playPosition < recordedData.size()) {
                    outputSample += recordedData[playPosition];
                    playPosition++;
                }

                // 播放生成的声波
                if (isPlayingGeneratedWave && wavePlayPosition < generatedWave.size()) {
                    outputSample += generatedWave[wavePlayPosition];
                    wavePlayPosition++;
                }

                // 限制音量
                outputSample = limitVolume(outputSample);

                // 写入输出通道
                for (int channel = 0; channel < numOutputChannels; channel++) {
                    if (outputChannelData[channel] != nullptr) {
                        outputChannelData[channel][i] = outputSample;
                    }
                }
            }
        }
        else {
            // 无输出
            for (int channel = 0; channel < numOutputChannels; channel++) {
                if (outputChannelData[channel] != nullptr) {
                    for (int i = 0; i < numSamples; i++) {
                        outputChannelData[channel][i] = 0.0f;
                    }
                }
            }
        }

        // 检查播放结束
        checkPlaybackCompletion();
    }

    // 检查播放是否完成
    void checkPlaybackCompletion() {
        if (isPlaying && playPosition >= recordedData.size()) {
            isPlaying = false;
            std::cout << "Record audio playing finished" << std::endl;
        }
        if (isPlayingMP3 && mp3PlayPosition >= mp3Buffer.size()) {
            isPlayingMP3 = false;
            std::cout << "MP3 playing finished" << std::endl;
        }
        if (isPlayingGeneratedWave && wavePlayPosition >= generatedWave.size()) {
            isPlayingGeneratedWave = false;
            std::cout << "Generated wave playing finished" << std::endl;
        }
    }

    void audioDeviceAboutToStart(AudioIODevice* device) override {
        std::cout << "Audio device: " << device->getName() << std::endl;
        std::cout << "Sample rate: " << device->getCurrentSampleRate() << " Hz" << std::endl;
    }

    void audioDeviceStopped() override {
        std::cout << "Audio device has stopped" << std::endl;
    }

    // ==================== 工具函数 ====================

    size_t getRecordedDataSize() const {
        return recordedData.size();
    }

    void clearRecordedData() {
        recordedData.clear();
        playPosition = 0;
        recordingDuration = 0;
        std::cout << "Audio data have been cleared" << std::endl;
    }

    void printStatus() {
        std::cout << "=== 系统状态 ===" << std::endl;
        std::cout << "录音数据: " << recordedData.size() << " 样本" << std::endl;
        std::cout << "MP3文件: " << (mp3Buffer.empty() ? "未加载" : "已加载") << std::endl;
        std::cout << "生成波形: " << (generatedWave.empty() ? "未生成" : "已生成") << std::endl;
        std::cout << "录音状态: " << (isRecording ? "进行中" : "停止") << std::endl;
        std::cout << "播放状态: " << (isPlaying ? "进行中" : "停止") << std::endl;
        std::cout << "生成波形播放: " << (isPlayingGeneratedWave ? "进行中" : "停止") << std::endl;

        if (!recordedData.empty()) {
            double duration = recordedData.size() / 48000.0;
            std::cout << "录音时长: " << duration << " 秒" << std::endl;
        }
        if (!generatedWave.empty()) {
            double duration = generatedWave.size() / 48000.0;
            std::cout << "生成波形时长: " << duration << " 秒" << std::endl;
        }
    }
};

//==============================================================================
int main(int argc, char* argv[]) {
    std::cout << "=== 集成音频系统 ===" << std::endl;
    std::cout << "功能1: 录制10秒音频并播放" << std::endl;
    std::cout << "功能2: 播放MP3文件并同时录音" << std::endl;
    std::cout << "功能3: 生成并播放指定频率的两个声波" << std::endl;
    std::cout << "=========================================" << std::endl;

    // 初始化音频设备
    AudioDeviceManager dev_manager;
    dev_manager.initialiseWithDefaultDevices(1, 2);

    AudioDeviceManager::AudioDeviceSetup dev_info;
    dev_info = dev_manager.getAudioDeviceSetup();
    dev_info.sampleRate = 48000;
    dev_manager.setAudioDeviceSetup(dev_info, false);

    // 创建音频处理器
    std::unique_ptr<IntegratedAudioSystem> audioSystem;
    audioSystem.reset(new IntegratedAudioSystem());
    dev_manager.addAudioCallback(audioSystem.get());

    // 用户界面
    std::cout << "\n=== 功能菜单 ===" << std::endl;
    std::cout << "1 - 功能1: 录制10秒（自动停止）" << std::endl;
    std::cout << "2 - 功能2: 播放MP3并录音10秒" << std::endl;
    std::cout << "3 - 生成两个指定频率的声波, 输入声波频率(Hz):" << std::endl;
    std::cout << "l <路径> - 加载MP3文件" << std::endl;
    std::cout << "p - 播放录音" << std::endl;
    std::cout << "x - 停止播放" << std::endl;
    std::cout << "s - 停止所有活动" << std::endl;
    std::cout << "c - 清空录音数据" << std::endl;
    std::cout << "i - 显示状态" << std::endl;
    std::cout << "q - 退出程序" << std::endl;
    std::cout << "==================" << std::endl;
    std::cout << "MP3文件示例: l C:/preaudio.mp3" << std::endl;
    std::cout << "自定义频率示例: 3 1000 10000 (生成1kHz和10kHz的声波)" << std::endl;

    bool running = true;
    while (running) {
        std::cout << "\n请输入命令: ";
        std::string input;
        std::getline(std::cin, input);

        if (input.empty()) continue;

        std::string command = input.substr(0, 1);
        std::string argument = input.length() > 2 ? input.substr(2) : "";

        if (command == "1") {  // 功能1
            audioSystem->startRecording10Seconds();
        }
        else if (command == "2") {  // 功能2
            audioSystem->startPlayMP3AndRecord();
        }
        else if (command == "3") {  // 自定义频率声波
            double freq1, freq2;
            if (sscanf(argument.c_str(), "%lf %lf", &freq1, &freq2) == 2) {
                audioSystem->generateCustomWave(freq1, freq2, 5.0, 48000);
            }
            else {
                std::cout << "错误: 请提供两个频率参数，例如: 4 1000 2000" << std::endl;
            }
        }
        else if (command == "l") {  // 加载MP3
            if (argument.empty()) {
                std::cout << "请提供文件路径:" << std::endl;
            }
            else {
                String filePath = argument;
                filePath = filePath.replace("\\", "/");
                audioSystem->loadMP3File(filePath);
            }
        }
        else if (command == "p") {  // 播放
            audioSystem->startPlayingRecording();
        }
        else if (command == "x") {  // 停止播放
            audioSystem->stopPlaying();
        }
        else if (command == "s") {  // 停止所有
            audioSystem->stopAll();
        }
        else if (command == "c") {  // 清空
            audioSystem->clearRecordedData();
        }
        else if (command == "i") {  // 状态
            audioSystem->printStatus();
        }
        else if (command == "q") {  // 退出
            running = false;
        }
        else {
            std::cout << "未知命令" << std::endl;
        }
    }

    // 清理资源
    dev_manager.removeAudioCallback(audioSystem.get());
    std::cout << "程序结束" << std::endl;

    return 0;
}
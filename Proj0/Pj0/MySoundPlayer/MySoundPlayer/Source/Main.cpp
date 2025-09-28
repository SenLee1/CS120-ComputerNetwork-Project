#include <JuceHeader.h>
#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <algorithm>

using namespace juce;

class IntegratedAudioSystem : public AudioIODeviceCallback {
private:
    // 录音数据存储
    std::vector<float> recordedData;
    std::vector<float> predefinedWave;  // 预定义波形
    std::vector<float> mp3Buffer;       // MP3音频数据缓存

    // 状态控制
    std::atomic<size_t> playPosition{ 0 };
    std::atomic<size_t> mp3PlayPosition{ 0 };
    std::atomic<bool> isRecording{ false };
    std::atomic<bool> isPlaying{ false };
    std::atomic<bool> isPlayingPredefined{ false };
    std::atomic<bool> isPlayingMP3{ false };
    std::atomic<size_t> recordingDuration{ 0 };
    std::atomic<size_t> maxRecordingSamples{ 0 };
    std::atomic<bool> autoStopEnabled{ false };

    // 音频格式管理
    AudioFormatManager formatManager;

public:
    IntegratedAudioSystem() {
        std::cout << "集成音频系统初始化" << std::endl;
        formatManager.registerBasicFormats();
        generatePredefinedWave();  // 生成预定义声音
    }

    ~IntegratedAudioSystem() {
        std::cout << "集成音频系统析构" << std::endl;
    }

    // ==================== 功能1: 预定义声音相关 ====================

    // 生成预定义声音（和弦）
    void generatePredefinedWave() {
        size_t sampleRate = 48000;
        size_t waveDuration = sampleRate * 10;  // 10秒

        predefinedWave.resize(waveDuration);

        double freq1 = 440.0;   // A4
        double freq2 = 523.25;  // C5
        double freq3 = 659.25;  // E5
        double amp = 0.3;

        for (size_t i = 0; i < waveDuration; i++) {
            double t = (double)i / sampleRate;

            double sample1 = amp * 0.5 * sin(2.0 * 3.141592653589793 * freq1 * t);
            double sample2 = amp * 0.3 * sin(2.0 * 3.141592653589793 * freq2 * t);
            double sample3 = amp * 0.2 * sin(2.0 * 3.141592653589793 * freq3 * t);

            // 包络控制
            double envelope = 1.0;
            if (t < 0.1) envelope = t / 0.1;
            if (t > 9.9) envelope = (10.0 - t) / 0.1;

            predefinedWave[i] = (sample1 + sample2 + sample3) * envelope;
        }

        std::cout << "预定义声音生成完成" << std::endl;
    }

    // 功能1: 录制10秒（自动停止）
    void startRecording10Seconds(int sampleRate = 48000) {
        recordedData.clear();
        isRecording = true;
        isPlaying = false;
        isPlayingPredefined = false;
        isPlayingMP3 = false;
        playPosition = 0;
        recordingDuration = 0;
        maxRecordingSamples = sampleRate * 10;
        autoStopEnabled = true;

        std::cout << "开始10秒录音..." << std::endl;
        std::cout << "请说话或制造声音..." << std::endl;

        startTimer(10);
    }

    // ==================== 功能2: MP3文件相关 ====================

    // 加载MP3文件
    bool loadMP3File(const String& filePath) {
        mp3Buffer.clear();
        mp3PlayPosition = 0;

        File audioFile(filePath);
        if (!audioFile.existsAsFile()) {
            std::cout << "错误: 文件不存在: " << filePath << std::endl;
            return false;
        }

        std::unique_ptr<AudioFormatReader> reader(formatManager.createReaderFor(audioFile));
        if (reader == nullptr) {
            std::cout << "错误: 无法读取音频文件或格式不支持" << std::endl;
            return false;
        }

        std::cout << "音频文件信息:" << std::endl;
        std::cout << " - 采样率: " << reader->sampleRate << " Hz" << std::endl;
        std::cout << " - 声道数: " << reader->numChannels << std::endl;
        std::cout << " - 时长: " << (reader->lengthInSamples / reader->sampleRate) << " 秒" << std::endl;

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

        std::cout << "音频文件加载成功" << std::endl;
        return true;
    }

    // 功能2: 播放MP3并同时录音10秒
    void startPlayMP3AndRecord(int sampleRate = 48000) {
        if (mp3Buffer.empty()) {
            std::cout << "错误: 请先加载MP3文件" << std::endl;
            return;
        }

        recordedData.clear();
        isRecording = true;
        isPlayingMP3 = true;
        isPlaying = false;
        isPlayingPredefined = false;
        playPosition = 0;
        mp3PlayPosition = 0;
        recordingDuration = 0;
        maxRecordingSamples = sampleRate * 10;
        autoStopEnabled = true;

        std::cout << "开始播放MP3并同时录音..." << std::endl;
        std::cout << "请在此期间说话..." << std::endl;

        startTimer(10);
    }

    // ==================== 通用控制函数 ====================

    // 启动定时器
    void startTimer(int seconds) {
        std::thread([this, seconds]() {
            for (int i = seconds; i > 0; --i) {
                if (!isRecording && !isPlayingMP3 && !isPlayingPredefined) break;

                std::cout << "剩余时间: " << i << " 秒" << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            // 时间到，自动停止
            if (isRecording) {
                stopAll();
                std::cout << "时间到，自动停止" << std::endl;
            }
            }).detach();
    }

    // 停止所有活动
    void stopAll() {
        isRecording = false;
        isPlaying = false;
        isPlayingPredefined = false;
        isPlayingMP3 = false;
        autoStopEnabled = false;
        std::cout << "所有活动已停止" << std::endl;
        std::cout << "最终录制样本数: " << recordedData.size() << std::endl;
    }

    // 播放录音
    void startPlayingRecording() {
        if (recordedData.empty()) {
            std::cout << "错误：没有录音数据可播放" << std::endl;
            return;
        }
        isPlaying = true;
        isRecording = false;
        isPlayingPredefined = false;
        isPlayingMP3 = false;
        playPosition = 0;
        std::cout << "开始播放录音..." << std::endl;
    }

    // 停止播放
    void stopPlaying() {
        isPlaying = false;
        isPlayingPredefined = false;
        isPlayingMP3 = false;
        std::cout << "停止播放" << std::endl;
    }

    // 音量限制函数
    float limitVolume(float sample, float min = -1.0f, float max = 1.0f) {
        if (sample < min) return min;
        if (sample > max) return max;
        return sample;
    }

    // ==================== 音频回调核心函数 ====================

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

                // 播放预定义声音（功能1）
                if (isPlayingPredefined && playPosition < predefinedWave.size()) {
                    outputSample += predefinedWave[playPosition];
                    playPosition++;
                }

                // 播放MP3音乐（功能2）
                if (isPlayingMP3 && mp3PlayPosition < mp3Buffer.size()) {
                    outputSample += mp3Buffer[mp3PlayPosition] * 0.7f;
                    mp3PlayPosition++;
                }

                // 播放录音（通用）
                if (isPlaying && playPosition < recordedData.size()) {
                    outputSample += recordedData[playPosition];
                    playPosition++;
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
            // 静音输出
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
            std::cout << "录音播放完毕" << std::endl;
        }
        if (isPlayingPredefined && playPosition >= predefinedWave.size()) {
            isPlayingPredefined = false;
            std::cout << "预定义声音播放完毕" << std::endl;
        }
        if (isPlayingMP3 && mp3PlayPosition >= mp3Buffer.size()) {
            isPlayingMP3 = false;
            std::cout << "MP3播放完毕" << std::endl;
        }
    }

    void audioDeviceAboutToStart(AudioIODevice* device) override {
        std::cout << "音频设备: " << device->getName() << std::endl;
        std::cout << "采样率: " << device->getCurrentSampleRate() << " Hz" << std::endl;
    }

    void audioDeviceStopped() override {
        std::cout << "音频设备已停止" << std::endl;
    }

    // ==================== 工具函数 ====================

    size_t getRecordedDataSize() const {
        return recordedData.size();
    }

    void clearRecordedData() {
        recordedData.clear();
        playPosition = 0;
        recordingDuration = 0;
        std::cout << "已清空录音数据" << std::endl;
    }

    void printStatus() {
        std::cout << "=== 系统状态 ===" << std::endl;
        std::cout << "录音数据: " << recordedData.size() << " 样本" << std::endl;
        std::cout << "预定义声音: " << (predefinedWave.empty() ? "未就绪" : "就绪") << std::endl;
        std::cout << "MP3文件: " << (mp3Buffer.empty() ? "未加载" : "已加载") << std::endl;
        std::cout << "录音状态: " << (isRecording ? "进行中" : "停止") << std::endl;
        std::cout << "播放状态: " << (isPlaying ? "进行中" : "停止") << std::endl;

        if (!recordedData.empty()) {
            double duration = recordedData.size() / 48000.0;
            std::cout << "录音时长: " << duration << " 秒" << std::endl;
        }
    }
};

//==============================================================================
int main(int argc, char* argv[]) {
    std::cout << "=== 集成音频系统 ===" << std::endl;
    std::cout << "功能1: 录制10秒音频并播放" << std::endl;
    std::cout << "功能2: 播放MP3文件并同时录音" << std::endl;
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
    std::cout << "l <路径> - 加载MP3文件" << std::endl;
    std::cout << "p - 播放录音" << std::endl;
    std::cout << "x - 停止播放" << std::endl;
    std::cout << "s - 停止所有活动" << std::endl;
    std::cout << "c - 清空录音数据" << std::endl;
    std::cout << "i - 显示状态" << std::endl;
    std::cout << "q - 退出程序" << std::endl;
    std::cout << "==================" << std::endl;
    std::cout << "MP3文件示例: l C:/preaudio.mp3" << std::endl;

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
        else if (command == "l") {  // 加载MP3
            if (argument.empty()) {
                std::cout << "请提供文件路径，例如: l C:/preaudio.mp3" << std::endl;
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
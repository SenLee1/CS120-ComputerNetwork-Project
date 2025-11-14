#include <JuceHeader.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>

using namespace juce;

// ==============================================================================
// Config of physical layer and frame
// ==============================================================================
struct PhyConfig {
    // bit rate (bps)
    static constexpr int BIT_RATE = 500;

    static constexpr float SIGNAL_AMPLITUDE = 0.5f;

    static constexpr float ENERGY_THRESHOLD = 0.001f;
};

struct FrameConfig {
    // Preamble byte
    static constexpr uint8_t PREAMBLE_BYTE = 0x55; // 01010101
    // Length of Preamble (in byte)
    static constexpr int PREAMBLE_LENGTH_BYTES = 4;

    // Start symbol of Frame
    static constexpr uint8_t SOF_BYTE = 0x7E; // 01111110
};

// ==============================================================================
// NODE 1 Transmitter
// ==============================================================================

class Node1Sender : public AudioIODeviceCallback {
  public:
    Node1Sender()
        : senderState(SenderState::Idle), samplesPerBit(0),
          samplesPerHalfBit(0), currentSampleRate(0.0), sendBufferPosition(0),
          sendSampleBufferEnd(0), completionFlag(0) {
        // register format of all audios (wav, mp3, ogg...)
        formatManager.registerBasicFormats();

        // find the path to desktop
        File desktop = File::getSpecialLocation(File::userDesktopDirectory);
        inputFile = desktop.getChildFile("INPUT.bin");
        outputWavFile = desktop.getChildFile("generated_signal.wav");

        std::cout << "input file: " << inputFile.getFullPathName() << std::endl;
        std::cout << ".wav saved to: " << outputWavFile.getFullPathName()
                  << std::endl;
    }

    ~Node1Sender() {}

    // ==========================================================================
    // JUCE AudioIODeviceCallback
    // ==========================================================================

    void audioDeviceAboutToStart(AudioIODevice *device) override {
        currentSampleRate = device->getCurrentSampleRate();

        std::cout << "\nAudio device started " << device->getName()
                  << std::endl;
        std::cout << "sampleRate: " << currentSampleRate << " Hz" << std::endl;

        // check if sampleRate can be divided by bit rate
        if (fmod(currentSampleRate, PhyConfig::BIT_RATE) != 0.0) {
            std::cerr << "!!!! Error:sampleRate(" << currentSampleRate
                      << ") cannot be divided by (" << PhyConfig::BIT_RATE
                      << ") !" << std::endl;
            std::cerr << "!!!! ensure your sampleRate is 48000 Hz。"
                      << std::endl;
            return;
        }

        // calculate samples for each bit and half bit
        samplesPerBit = (int)(currentSampleRate / PhyConfig::BIT_RATE);
        samplesPerHalfBit = samplesPerBit / 2;

        std::cout << "bitrate: " << PhyConfig::BIT_RATE << " bps" << std::endl;
        std::cout << "sample for each bit: " << samplesPerBit << std::endl;
    }

    void audioDeviceStopped() override {
        std::cout << "\nAudio device has stopped." << std::endl;
    }

    void audioDeviceIOCallback(const float **inputChannelData,
                               int numInputChannels, float **outputChannelData,
                               int numOutputChannels, int numSamples) override {
        // --- logic for Transmitter ---
        float *outBuffer =
            (numOutputChannels > 0) ? outputChannelData[0] : nullptr;
        if (senderState == SenderState::Sending && outBuffer != nullptr) {
            for (int i = 0; i < numSamples; ++i) {
                if (sendBufferPosition < sendSampleBufferEnd) {
                    // copy samples from sendbuffer
                    outBuffer[i] = sendSampleBuffer[sendBufferPosition++];
                } else {
                    // sending finished
                    outBuffer[i] = 0.0f; // send 0 (silence)
                }
            }

            // All samples in sendbuffer has been transmitted
            if (sendBufferPosition >= sendSampleBufferEnd) {
                senderState = SenderState::Idle;
                sendBufferPosition = 0;
                sendSampleBufferEnd = 0;
                completionFlag.store(1); // 1 = sending successfully
            }
        } else if (outBuffer != nullptr) {
            // if not sending, output slience
            std::memset(outBuffer, 0, numSamples * sizeof(float));
        }
    }

    // ==========================================================================
    // 公共控制函数 (由 main() 调用)
    // ==========================================================================

    void startSendFile() {
        if (senderState != SenderState::Idle) {
            std::cout << "Error: is sending." << std::endl;
            return;
        }

        std::cout << "Start sending file: " << inputFile.getFullPathName()
                  << std::endl;

        if (!inputFile.existsAsFile()) {
            std::cerr << "Error: file " << inputFile.getFullPathName()
                      << " Not found!" << std::endl;
            return;
        }

        // 1. read file  
        MemoryBlock fileData;
        juce::FileInputStream inputStream(inputFile);
        if (!inputStream.openedOk()) {
            std::cerr << "Error: Unable to open file!" << std::endl;
            return;
        }
        inputStream.readIntoMemoryBlock(fileData);

        // 2. set up frame (Preamble + SOF + Length + Payload + Checksum)
        std::vector<uint8_t> frame;
        uint16_t payloadLength = (uint16_t)fileData.getSize();

        // -- Preamble --
        for (int i = 0; i < FrameConfig::PREAMBLE_LENGTH_BYTES; ++i) {
            frame.push_back(FrameConfig::PREAMBLE_BYTE);
        }

        // -- SOF --
        frame.push_back(FrameConfig::SOF_BYTE);

        // -- Length (2 bytes, Big Endian) --
        frame.push_back((uint8_t)(payloadLength >> 8));   // high bit
        frame.push_back((uint8_t)(payloadLength & 0xFF)); // low bit

        // -- Payload & Checksum --
        uint8_t checksum = 0;
        const uint8_t *data = (const uint8_t *)fileData.getData();
        for (size_t i = 0; i < payloadLength; ++i) {
            frame.push_back(data[i]);
            checksum ^= data[i]; // calculate XOR checksum
        }

        // -- Checksum --
        frame.push_back(checksum);

        std::cout << "Frame built successfully: " << frame.size() << " Byte (include overhead)"
                  << std::endl;
        std::cout << "overload: " << payloadLength << " byte" << std::endl;

        // 3. Modulate frame(translate byte to audio samples)
        modulateFrame(frame);

        // saveSendBufferToWav(outputWavFile);

        // 5. set state to Sending
        sendBufferPosition = 0;
        completionFlag.store(0);            // 清除完成标志
        senderState = SenderState::Sending; // 音频线程将从此开始发送
    }

    void stopAll() {
        senderState = SenderState::Idle;
        completionFlag.store(9); // 9 = 手动停止
        std::cout << "已停止发送。" << std::endl;
    }

    int getCompletionFlag() const { return completionFlag.load(); }
    void clearCompletionFlag() { completionFlag.store(0); }

  private:

    /**
     * @brief [Main 线程] 将整个帧调制为音频样本
     */
    void modulateFrame(const std::vector<uint8_t> &frame) {
        // 预分配足够的空间
        size_t totalSamples = frame.size() * 8 * samplesPerBit;
        sendSampleBuffer.assign(totalSamples, 0.0f);
        size_t currentSampleIndex = 0;

        for (uint8_t byte : frame) {
            // 从高位 (MSB) 到低位 (LSB)
            for (int i = 7; i >= 0; --i) {
                bool bit = (byte >> i) & 1;

                // 调制这个比特 (曼彻斯特编码)
                // '1' = H-L (高-低)
                // '0' = L-H (低-高)

                float firstHalf = bit ? PhyConfig::SIGNAL_AMPLITUDE
                                      : -PhyConfig::SIGNAL_AMPLITUDE;
                float secondHalf = bit ? -PhyConfig::SIGNAL_AMPLITUDE
                                       : PhyConfig::SIGNAL_AMPLITUDE;

                for (int s = 0; s < samplesPerHalfBit; ++s) {
                    if (currentSampleIndex < sendSampleBuffer.size())
                        sendSampleBuffer[currentSampleIndex++] = firstHalf;
                }
                for (int s = 0; s < samplesPerHalfBit; ++s) {
                    if (currentSampleIndex < sendSampleBuffer.size())
                        sendSampleBuffer[currentSampleIndex++] = secondHalf;
                }
            }
        }

        // 记录发送缓冲区的实际结束位置
        sendSampleBufferEnd = currentSampleIndex;
        std::cout << "modulation finished: " << sendSampleBufferEnd << " samples"
                  << std::endl;

        // 调整缓冲区大小以匹配
        sendSampleBuffer.resize(sendSampleBufferEnd);
    }

    /**
     * @brief [Main 线程] (调试) 将 sendSampleBuffer 保存到 .wav 文件
     */
    void saveSendBufferToWav(const File &fileToSave) {
        // 1. 删除旧文件，创建一个新的文件输出流
        fileToSave.deleteFile();
        std::unique_ptr<FileOutputStream> fileStream(
            fileToSave.createOutputStream());

        if (fileStream == nullptr) {
            std::cerr << "Error: Unable to construct a wav output stream. " << std::endl;
            return;
        }

        // 自动查找已注册的 "wav" 格式处理器
        AudioFormat *wavFormat =
            formatManager.findFormatForFileExtension("wav");
        if (wavFormat == nullptr) {
            std::cerr << "Error: 'wav' format manager not found"
                      << std::endl;
            return;
        }

        std::unique_ptr<AudioFormatWriter> writer(wavFormat->createWriterFor(
            fileStream.get(), currentSampleRate, 1, 16, {}, 0));
        // ==================================================================

        if (writer == nullptr) {
            std::cerr << "Error: Unable to consstruct wav writer" << std::endl;
            return;
        }

        // 2. 将std::vector<float> 转换为 JUCE 的 AudioBuffer
        AudioBuffer<float> buffer(1, (int)sendSampleBuffer.size());
        buffer.copyFrom(0, 0, sendSampleBuffer.data(),
                        (int)sendSampleBuffer.size());

        // 3. 写入文件
        writer->writeFromFloatArrays(buffer.getArrayOfReadPointers(),
                                     buffer.getNumChannels(),
                                     buffer.getNumSamples());

        // std::cout << "\n[Debug] 发送波形已成功保存到: "
        //           << fileToSave.getFullPathName() << std::endl;
    }

    // ==========================================================================
    // 成员变量
    // ==========================================================================

    // 状态
    enum class SenderState { Idle, Sending };
    std::atomic<SenderState> senderState;

    // 音频格式管理器 (用于保存 .wav)
    AudioFormatManager formatManager;

    // 音频参数
    double currentSampleRate;
    int samplesPerBit;
    int samplesPerHalfBit;

    // 文件
    File inputFile;
    File outputWavFile;

    // 发送缓冲区
    std::vector<float> sendSampleBuffer;
    std::atomic<size_t> sendBufferPosition;
    std::atomic<size_t> sendSampleBufferEnd;

    // 线程通信标志 (用于 main() 轮询)
    // 0 = 空闲/进行中
    // 1 = 发送完成
    // 9 = 手动停止
    std::atomic<int> completionFlag;
};

//==============================================================================
// Main 函数
//==============================================================================
int main(int argc, char *argv[]) {
    // 这个 Initialiser 是 JUCE 控制台应用所必需的
    juce::ScopedJuceInitialiser_GUI libraryInitialiser;

    std::cout << "========================================" << std::endl;
    std::cout << "== NODE 1 (Transmitter) ==" << std::endl;
    std::cout << "========================================" << std::endl;

    // 1. 初始化音频设备
    AudioDeviceManager dev_manager;
    // !! 关键: 只请求输出 !!
    juce::String initError = dev_manager.initialiseWithDefaultDevices(0, 1);

    if (initError.isNotEmpty()) {
        // 如果 (0, 1) 失败了, 打印错误并退出
        std::cerr << "!!! Error: Unable to inilize audio device!!!" << std::endl;
        std::cerr << "!!! Error information: " << initError.toStdString() << std::endl;
        std::cout << "press Enter to exit..." << std::endl;
        std::string dummyInput;
        std::getline(std::cin, dummyInput);
        return 1;
    }

    AudioDeviceManager::AudioDeviceSetup dev_info;
    dev_manager.getAudioDeviceSetup(dev_info);

    // !! 强制 48000 Hz !!
    if (dev_info.sampleRate != 48000.0) {
        std::cout << "currentSampleRate is not 48000 Hz, trying setting..." << std::endl;
        dev_info.sampleRate = 48000.0;
        dev_manager.setAudioDeviceSetup(dev_info, false); // false = 不要重启

        // 再次检查
        dev_manager.getAudioDeviceSetup(dev_info);
        if (dev_info.sampleRate != 48000.0) {
            std::cerr << "!! warning: cannot set sampleRate to 48000 Hz." << std::endl;
            std::cerr << "!! erroor may happen. set up you sound card by hand" << std::endl;
        }
    }

    // 2. 创建音频处理器
    std::unique_ptr<Node1Sender> audioSender;
    audioSender.reset(new Node1Sender());

    // 3. 注册回调
    dev_manager.addAudioCallback(audioSender.get());

    // 4. 用户界面 (控制台)
    std::cout << "\n=== Functions Menu ===" << std::endl;
    std::cout << " send    - transmit INPUT.bin" << std::endl;
    std::cout << " stop    - stop transmitting" << std::endl;
    std::cout << " quit    - exit" << std::endl;
    std::cout << "==================" << std::endl;
    std::cout << "ensure INPUT.bin is on your desktop" << std::endl;

    bool running = true;
    while (running) {
        std::cout << "\nPlease input command(send, stop, quit): ";
        std::string input;
        std::getline(std::cin, input);

        if (input == "send") {
            // 开始发送 (调制和保存 .wav 都在这里)
            audioSender->startSendFile();

            // 在主线程中等待，直到发送完成
            std::cout << "Sending... ";
            while (audioSender->getCompletionFlag() == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                std::cout << ".";
                std::cout.flush(); // 确保 '.' 立即打印
            }
            std::cout << "\nSending finished" << std::endl;
            audioSender->clearCompletionFlag();
        } else if (input == "stop") {
            audioSender->stopAll();
        } else if (input == "quit") {
            running = false;
        } else {
            std::cout << "Unknown command" << std::endl;
        }
    }

    // 5. 清理资源
    dev_manager.removeAudioCallback(audioSender.get());
    std::cout << "program fished." << std::endl;

    return 0;
}

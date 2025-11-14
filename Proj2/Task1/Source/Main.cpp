#include <JuceHeader.h>
#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <cmath>
#include <queue>
#include <deque>

using namespace juce;

// ==============================================================================
// Physical Layer and Frame Settings
// ==============================================================================

// Arguments
struct PhyConfig
{
    // 6,000 bps @ 48,000 Hz = 8 samplesPerBit
    static constexpr int BIT_RATE = 6000;

    // Signal amplitude
    static constexpr float SIGNAL_AMPLITUDE = 0.5f;

    // Signal Threshold for energy when receiving
    static constexpr float ENERGY_THRESHOLD = 0.001f;
};

// Frame Structure
struct FrameConfig
{
    // preamble byte
    static constexpr uint8_t PREAMBLE_BYTE = 0x55; // 01010101

    // preamble length
    static constexpr int PREAMBLE_LENGTH_BYTES = 4;

    // Start of Frame
    static constexpr uint8_t SOF_BYTE = 0x7E; // 01111110
};

class AudioNetworkSystem : public AudioIODeviceCallback
{
public:
    AudioNetworkSystem()
        : senderState(SenderState::Idle),
        receiverState(ReceiverState::Idle),
        samplesPerBit(0),
        samplesPerHalfBit(0),
        currentSampleRate(0.0),
        sendBufferPosition(0),
        sendSampleBufferEnd(0),
        receiverBufferPos(0),
        preambleBytesFound(0),
        receiverBitCount(0),
        currentByte(0),
        receiverByteCount(0),
        expectedPayloadLength(0),
        calculatedChecksum(0),
        completionFlag(0)
    {
        // Input and Output File Path
        File desktop = File::getSpecialLocation(File::userDesktopDirectory);
        inputFile = desktop.getChildFile("INPUT.bin");
        outputFile = desktop.getChildFile("OUTPUT.bin");

        // set Path for Saving WAV File
        outputWavFile = outputFile.getSiblingFile("generated_signal.wav");

        std::cout << "Input file: " << inputFile.getFullPathName() << std::endl;
        std::cout << "Output file: " << outputFile.getFullPathName() << std::endl;
        std::cout << "Waves Saved At: " << outputWavFile.getFullPathName() << std::endl;

        // Register WAV format for saving
        formatManager.registerBasicFormats();
    }

    ~AudioNetworkSystem() {}

    void audioDeviceAboutToStart(AudioIODevice* device) override
    {
        currentSampleRate = device->getCurrentSampleRate();
        std::cout << "Devices Start:" << device->getName() << std::endl;
        std::cout << "Sample Rate" << currentSampleRate << "Hz" << std::endl;

        // Check SampleRate
        if (fmod(currentSampleRate, PhyConfig::BIT_RATE) != 0.0)
        {
            std::cerr << "Error: SampleRate (" << currentSampleRate << ") can not exactly divided by (" << PhyConfig::BIT_RATE << ")" << std::endl;
            std::cerr << "Ensure SampleRate 48000 Hz." << std::endl;
            return;
        }

        // 计算每个比特和半个比特的采样点数
        samplesPerBit = (int)(currentSampleRate / PhyConfig::BIT_RATE);
        samplesPerHalfBit = samplesPerBit / 2;

        std::cout << "BitRate: " << PhyConfig::BIT_RATE << " bps" << std::endl;
        std::cout << "SamplePerBit: " << samplesPerBit << std::endl;
    }

    // 音频设备关闭时调用
    void audioDeviceStopped() override
    {
        std::cout << "\nDevice Stopped" << std::endl;
    }

    // 核心音频处理回调（高优先级音频线程）
    void audioDeviceIOCallback(const float** inputChannelData, int numInputChannels, float** outputChannelData, int numOutputChannels, int numSamples) override
    {
        // 获取主输入/输出缓冲区,假定为通道0
        const float* inBuffer = (numInputChannels > 0) ? inputChannelData[0] : nullptr;
        float* outBuffer = (numOutputChannels > 0) ? outputChannelData[0] : nullptr;

        // --- 发送端逻辑 ---
        if (senderState == SenderState::Sending && outBuffer != nullptr)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                if (sendBufferPosition < sendSampleBufferEnd)
                {
                    // 从发送缓冲区复制样本
                    outBuffer[i] = sendSampleBuffer[sendBufferPosition++];
                }
                else
                {
                    // 发送完毕
                    outBuffer[i] = 0.0f; // 发送静音
                }
            }

            // 如果缓冲区中的所有样本都已发送
            if (sendBufferPosition >= sendSampleBufferEnd)
            {
                senderState = SenderState::Idle;
                sendBufferPosition = 0;
                sendSampleBufferEnd = 0;
                completionFlag.store(1); // 1 = 发送完成
            }
        }
        else if (outBuffer != nullptr)
        {
            // 如果不发送，则输出静音
            std::memset(outBuffer, 0, numSamples * sizeof(float));
        }

        // --- 接收端逻辑 ---
        if (receiverState != ReceiverState::Idle && inBuffer != nullptr)
        {
            // 在这个音频块中处理每个采样点
            for (int i = 0; i < numSamples; ++i)
            {
                processReceivedSample(inBuffer[i]);
            }
           
        }
    }


    // ==========================================================================
    // 公共控制函数 (由 main() 调用)
    // ==========================================================================

    // [Main 线程] 开始发送文件
    void startSendFile()
    {
        if (senderState != SenderState::Idle)
        {
            std::cout << "Invalid Operation: Device is Sending" << std::endl;
            return;
        }

        std::cout << "Start Sending: " << inputFile.getFullPathName() << std::endl;

        if (!inputFile.existsAsFile())
        {
            std::cerr << "Error: File " << inputFile.getFullPathName() << " is not found" << std::endl;
            return;
        }

        // 1. 读取文件内容
        MemoryBlock fileData;
        juce::FileInputStream inputStream(inputFile);

        if (!inputStream.openedOk())
        {
            std::cerr << "Error: Failed to Open File" << std::endl;
            return;
        }

        inputStream.readIntoMemoryBlock(fileData);

        // 2. 构建帧 (Preamble + SOF + Length + Payload + Checksum)
        std::vector<uint8_t> frame;
        uint16_t payloadLength = (uint16_t)fileData.getSize();

        // -- Preamble --
        for (int i = 0; i < FrameConfig::PREAMBLE_LENGTH_BYTES; ++i)
        {
            frame.push_back(FrameConfig::PREAMBLE_BYTE);
        }

        // -- SOF --
        frame.push_back(FrameConfig::SOF_BYTE);

        // -- Length (2 bytes, Big Endian) --
        frame.push_back((uint8_t)(payloadLength >> 8)); // 高位字节
        frame.push_back((uint8_t)(payloadLength & 0xFF)); // 低位字节

        // -- Payload & Checksum --
        uint8_t checksum = 0;
        const uint8_t* data = (const uint8_t*)fileData.getData();
        for (size_t i = 0; i < payloadLength; ++i)
        {
            frame.push_back(data[i]);
            checksum ^= data[i]; // 计算 XOR 校验和
        }

        // -- Checksum --
        frame.push_back(checksum);

        std::cout << "Frame Construction Completed. Total byte: " << frame.size() << std::endl;
        std::cout << "Payload Byte: " << payloadLength << std::endl;

        // 3. 调制帧 (将字节转换为音频样本)
        modulateFrame(frame);

        // (新功能) 调制完成后立即保存到 WAV 文件
        if (currentSampleRate > 0 && !sendSampleBuffer.empty())
        {
            saveSendBufferToWav(outputWavFile);
        }
        else
        {
            std::cerr << "Warning: Failed to Save WAV File (SampleRate=" << currentSampleRate << ", BufferEmpty=" << sendSampleBuffer.empty() << ")" << std::endl;
        }

        // 4. 设置状态为 Sending
        sendBufferPosition = 0;
        completionFlag.store(0); // 清除完成标志
        senderState = SenderState::Sending; // 音频线程将从此开始发送
    }

    // [Main 线程] 开始接收
    void startReceiving()
    {
        if (receiverState != ReceiverState::Idle)
        {
            std::cout << "Invalid Operation: Device is Receiving" << std::endl;
            return;
        }

        // 重置所有接收状态机变量
        receiverState = ReceiverState::Idle; // 将在 processReceivedSample 中切换到 Syncing
        receiverBufferPos = 0;
        preambleBytesFound = 0;
        receiverBitCount = 0;
        currentByte = 0;
        receiverByteCount = 0;
        expectedPayloadLength = 0;
        calculatedChecksum = 0;
        receivedPayload.clear();
        completionFlag.store(0); // 清除完成标志

        receiverState = ReceiverState::Idle; // 等待能量检测
        std::cout << "Receving Mode: Waiting For Signal" << std::endl;
    }

    // [Main 线程] 停止所有活动
    void stopAll()
    {
        senderState = SenderState::Idle;
        receiverState = ReceiverState::Idle;
        completionFlag.store(9); // 9 = 手动停止
        std::cout << "Succeed to Stop All Activity" << std::endl;
    }

    // 用于 main() 轮询
    int getCompletionFlag() const { return completionFlag.load(); }
    void clearCompletionFlag() { completionFlag.store(0); }


private:
    // ==========================================================================
    // 内部逻辑 (私有)
    // ==========================================================================

    // [Main 线程] 将整个帧调制为音频样本
    void modulateFrame(const std::vector<uint8_t>& frame)
    {
        // 预分配足够的空间
        size_t totalSamples = frame.size() * 8 * samplesPerBit;
        sendSampleBuffer.assign(totalSamples, 0.0f);
        size_t currentSampleIndex = 0;

        for (uint8_t byte : frame)
        {
            // 从高位 (MSB) 到低位 (LSB)
            for (int i = 7; i >= 0; --i)
            {
                bool bit = (byte >> i) & 1;

                // 调制这个比特 (曼彻斯特编码)
                // '1' = H-L (高-低)
                // '0' = L-H (低-高)

                float firstHalf = bit ? PhyConfig::SIGNAL_AMPLITUDE : -PhyConfig::SIGNAL_AMPLITUDE;
                float secondHalf = bit ? -PhyConfig::SIGNAL_AMPLITUDE : PhyConfig::SIGNAL_AMPLITUDE;

                for (int s = 0; s < samplesPerHalfBit; ++s)
                {
                    if (currentSampleIndex < sendSampleBuffer.size())
                        sendSampleBuffer[currentSampleIndex++] = firstHalf;
                }
                for (int s = 0; s < samplesPerHalfBit; ++s)
                {
                    if (currentSampleIndex < sendSampleBuffer.size())
                        sendSampleBuffer[currentSampleIndex++] = secondHalf;
                }
            }
        }

        // 记录发送缓冲区的实际结束位置
        sendSampleBufferEnd = currentSampleIndex;
        std::cout << "Modulate Completed, Total Sample: " << sendSampleBufferEnd << std::endl;

        // 调整缓冲区大小以匹配
        sendSampleBuffer.resize(sendSampleBufferEnd);
    }


    // [音频线程] 接收状态机 - 处理单个采样点
    void processReceivedSample(float sample)
    {
        // 状态机核心
        switch (receiverState)
        {
        case ReceiverState::Idle:
        {
            // 状态: 空闲
            // 目标: 检测信号能量，寻找前导码

            // 简易能量检测
            float energy = sample * sample;
            if (energy > PhyConfig::ENERGY_THRESHOLD)
            {
                // 可能有信号了，开始填充半比特缓冲区
                receiverBitBuffer[receiverBufferPos++] = sample;

                // 切换到同步状态
                receiverState = ReceiverState::Syncing;
            }
            break;
        }

        // Syncing, ReadingLength, ReadingPayload, ReadingChecksum 共享相同的采样逻辑
        case ReceiverState::Syncing:
        case ReceiverState::ReadingLength:
        case ReceiverState::ReadingPayload:
        case ReceiverState::ReadingChecksum:
        {
            receiverBitBuffer[receiverBufferPos++] = sample;

            // 检查是否收集满了半个比特的采样点
            if (receiverBufferPos == samplesPerHalfBit)
            {
                // 计算前半个比特的平均值
                receiverHalfBitAvg[0] = 0.0f;
                for (int i = 0; i < samplesPerHalfBit; ++i)
                {
                    receiverHalfBitAvg[0] += receiverBitBuffer[i];
                }
                receiverHalfBitAvg[0] /= samplesPerHalfBit;
            }
            // 检查是否收集满了一个比特的采样点
            else if (receiverBufferPos == samplesPerBit)
            {
                // 计算后半个比特的平均值
                receiverHalfBitAvg[1] = 0.0f;
                for (int i = samplesPerHalfBit; i < samplesPerBit; ++i)
                {
                    receiverHalfBitAvg[1] += receiverBitBuffer[i];
                }
                receiverHalfBitAvg[1] /= samplesPerHalfBit;

                // 重置缓冲区位置
                receiverBufferPos = 0;

                // 解码这个比特
                // '1' = H-L (avg[0] > 0, avg[1] < 0)
                // '0' = L-H (avg[0] < 0, avg[1] > 0)

                if (receiverHalfBitAvg[0] > 0.01f && receiverHalfBitAvg[1] < -0.01f)
                {
                    processReceivedBit(true); // 收到 1
                }
                else if (receiverHalfBitAvg[0] < -0.01f && receiverHalfBitAvg[1] > 0.01f)
                {
                    processReceivedBit(false); // 收到 0
                }
                else
                {
                    // 无效的曼彻斯特码 (可能是噪音)
                    if (receiverState == ReceiverState::Syncing)
                    {
                        // 如果在同步时出错，重置
                        preambleBytesFound = 0;
                        receiverState = ReceiverState::Idle; // 回到空闲状态
                    }
                    else
                    {
                        // 如果在数据中出错，我们记录一个 "坏" 比特 (例如 0)
                        // 校验和最终会使这个帧失效
                        processReceivedBit(false);
                    }
                }
            }
            break;
        }
        } // end switch(receiverState)
    }

    // [音频线程] 接收状态机 - 处理一个解码后的比特
    void processReceivedBit(bool bit)
    {
        // 将比特推入当前字节
        currentByte = (currentByte << 1) | (bit ? 1 : 0);
        receiverBitCount++;

        // 当我们收集满 8 个比特时
        if (receiverBitCount == 8)
        {
            receiverBitCount = 0;
            processReceivedByte(currentByte); // 处理这个字节
            currentByte = 0;
        }
    }

    // [音频线程] 接收状态机 - 处理一个完整的字节
    void processReceivedByte(uint8_t byte)
    {
        // 这是字节级别的状态机
        switch (receiverState)
        {
        case ReceiverState::Syncing:
        {
            // 状态: 同步中
            // 目标: 寻找前导码 (0x55) 和 SOF (0x7E)

            if (byte == FrameConfig::PREAMBLE_BYTE)
            {
                preambleBytesFound++;
            }
            else if (byte == FrameConfig::SOF_BYTE && preambleBytesFound >= FrameConfig::PREAMBLE_LENGTH_BYTES)
            {
                // 找到了 SOF! 准备读取长度
                std::cout << "\n[RX] SOF Detected，Reading Length..." << std::endl;
                receiverState = ReceiverState::ReadingLength;
                receiverByteCount = 0; // 重置字节计数器
                expectedPayloadLength = 0;
            }
            else
            {
                // 模式被破坏，重置
                preambleBytesFound = 0;
            }
            break;
        }

        case ReceiverState::ReadingLength:
        {
            // 状态: 读取长度 (2 字节)
            if (receiverByteCount == 0)
            {
                expectedPayloadLength = (uint16_t)byte << 8; // 高位字节
                receiverByteCount = 1;
            }
            else
            {
                expectedPayloadLength |= byte; // 低位字节

                std::cout << "[RX] Reading Completed: " << expectedPayloadLength << " bytes is detected" << std::endl;

                // 安全检查
                if (expectedPayloadLength == 0 || expectedPayloadLength > 10000) // 10k 字节
                {
                    std::cerr << "[RX] Error: Invalid Length " << expectedPayloadLength << std::endl;
                    receiverState = ReceiverState::Idle; // 重置
                    preambleBytesFound = 0;
                    completionFlag.store(3); // 3 = 接收失败
                    break;
                }

                // 准备读取有效载荷
                receiverState = ReceiverState::ReadingPayload;
                receivedPayload.clear();
                receivedPayload.reserve(expectedPayloadLength);
                calculatedChecksum = 0;
                receiverByteCount = 0;
            }
            break;
        }

        case ReceiverState::ReadingPayload:
        {
            // 状态: 读取有效载荷
            receivedPayload.push_back(byte);
            calculatedChecksum ^= byte; // 实时计算校验和
            receiverByteCount++;

            if (receiverByteCount == expectedPayloadLength)
            {
                // 有效载荷读取完毕，准备读取校验和
                std::cout << "[RX] Receving Payload Completed，Start Reading and Checking..." << std::endl;
                receiverState = ReceiverState::ReadingChecksum;
            }
            break;
        }

        case ReceiverState::ReadingChecksum:
        {
            // 状态: 读取校验和
            if (byte == calculatedChecksum)
            {
                // 校验和匹配! 成功!
                std::cout << "[RX] Check Sum Successfully!" << std::endl;

                // 保存文件
                outputFile.deleteFile(); // 删除旧文件
                FileOutputStream stream(outputFile);
                if (stream.openedOk())
                {
                    stream.write(receivedPayload.data(), receivedPayload.size());
                    std::cout << "[RX] File Saved At: " << outputFile.getFullPathName() << std::endl;
                    completionFlag.store(2); // 2 = 接收成功
                }
                else
                {
                    std::cerr << "[RX] 错误: 无法写入输出文件!" << std::endl;
                    completionFlag.store(3); // 3 = 接收失败
                }
            }
            else
            {
                // 校验和失败
                std::cerr << "[RX] Check Sum Failed! Expectation: " << (int)calculatedChecksum
                    << ", 收到: " << (int)byte << std::endl;
                completionFlag.store(3); // 3 = 接收失败
            }

            // 无论成功与否，都返回空闲状态
            receiverState = ReceiverState::Idle;
            preambleBytesFound = 0;
            receiverBufferPos = 0;
            break;
        }

        case ReceiverState::Idle:
            break;
        }
    }

    // [Main 线程] 将发送缓冲区的内容保存为 WAV 文件
    void saveSendBufferToWav(const File& fileToSave)
    {
        // 1. 确保旧文件被删除
        fileToSave.deleteFile();

        // 2. 尝试创建一个文件输出流
        std::unique_ptr<FileOutputStream> fileStream(fileToSave.createOutputStream());
        if (fileStream == nullptr)
        {
            std::cerr << "Error: Fail to Create WAV OutputStream." << std::endl;
            return;
        }

        // 3. 找到一个 WAV 格式的写入器
        WavAudioFormat wavFormat;
        std::unique_ptr<AudioFormatWriter> writer(
            wavFormat.createWriterFor(fileStream.get(),
                currentSampleRate, // 采样率
                1,                 // 1 个通道 (单声道)
                16,                // 16 位深度 (标准CD质量)
                {},                // 元数据 (无)
                0                  // 质量 (0 = 默认)
            ));

        if (writer == nullptr)
        {
            std::cerr << "Error: Fail to Create WAV FormatWriter" << std::endl;
            return;
        }

        // 4. JUCE AudioFormatWriter 需要一个 `AudioBuffer<float>`
        // 我们的数据在 `std::vector<float>` 中，需要转换

        // 创建一个临时的 AudioBuffer
        AudioBuffer<float> buffer(1, (int)sendSampleBuffer.size());

        // 将 vector 数据复制到 AudioBuffer 的通道 0
        buffer.copyFrom(0, 0, sendSampleBuffer.data(), (int)sendSampleBuffer.size());

        // 5. 写入文件
        // 'writeFromAudioBuffer' 在 JUCE 6.0.8 中不存在。
        // 我们使用 JUCE 6.0.8 兼容的 'writeFromFloatArrays'
        writer->writeFromFloatArrays(buffer.getArrayOfReadPointers(),
            buffer.getNumChannels(),
            buffer.getNumSamples());

        // 6. 写入器析构时会自动 flush 和关闭流

        std::cout << "\n[Debug] Sending WAV Saved At: " << fileToSave.getFullPathName() << std::endl;
    }


    // ==========================================================================
    // 成员变量
    // ==========================================================================

    // 状态
    enum class SenderState { Idle, Sending };
    enum class ReceiverState { Idle, Syncing, ReadingLength, ReadingPayload, ReadingChecksum };

    std::atomic<SenderState> senderState;
    std::atomic<ReceiverState> receiverState;

    // 音频参数
    double currentSampleRate;
    int samplesPerBit;
    int samplesPerHalfBit;

    // 文件
    File inputFile;
    File outputFile;
    File outputWavFile; // (新功能) 用于保存WAV文件的路径

    // (新功能) 用于创建WAV写入器
    AudioFormatManager formatManager;

    // 发送缓冲区
    std::vector<float> sendSampleBuffer;
    std::atomic<size_t> sendBufferPosition;
    std::atomic<size_t> sendSampleBufferEnd;

    // 接收缓冲区 (用于重组比特)
    std::array<float, 256> receiverBitBuffer; // 应该足够大 (e.g. 8 samples/bit * 2)
    int receiverBufferPos;
    float receiverHalfBitAvg[2] = { 0.0f, 0.0f };

    // 接收状态机（字节级别）
    int preambleBytesFound;
    int receiverBitCount;
    uint8_t currentByte;

    int receiverByteCount;
    uint16_t expectedPayloadLength;
    std::vector<uint8_t> receivedPayload;
    uint8_t calculatedChecksum;

    // 线程通信标志 (用于 main() 轮询)
    // 0 = 空闲/进行中
    // 1 = 发送完成
    // 2 = 接收成功
    // 3 = 接收失败
    // 9 = 手动停止
    std::atomic<int> completionFlag;
};


//==============================================================================
// Main 函数 (你的 Project 1 框架)
//==============================================================================
int main(int argc, char* argv[])
{
    // 这个 Initialiser 是 JUCE 控制台应用所必需的
    juce::ScopedJuceInitialiser_GUI libraryInitialiser;

    std::cout << "========================================" << std::endl;
    std::cout << "== Computer Network Project2 Controller ==" << std::endl;
    std::cout << "========================================" << std::endl;

    // 1. 初始化音频设备
    AudioDeviceManager dev_manager;
    dev_manager.initialiseWithDefaultDevices(1, 1); // 1 输入, 1 输出

    AudioDeviceManager::AudioDeviceSetup dev_info;
    dev_manager.getAudioDeviceSetup(dev_info);

    // !! 强制 48000 Hz !!
    if (dev_info.sampleRate != 48000.0)
    {
        std::cout << "SampleRate is not 48000HZ. Attempting to Set it." << std::endl;
        dev_info.sampleRate = 48000.0;
        dev_manager.setAudioDeviceSetup(dev_info, false); // false = 不要重启
         
        // 再次检查
        dev_manager.getAudioDeviceSetup(dev_info);
        if (dev_info.sampleRate != 48000.0)
        {
            std::cerr << "!! Warning: Fail to Set SampleRate As 48000 Hz." << std::endl;
            std::cerr << "!! It may lead to errors. Please configure your sound card manually." << std::endl;
        }
    }

    // 2. 创建音频处理器
    std::unique_ptr<AudioNetworkSystem> audioSystem;
    audioSystem.reset(new AudioNetworkSystem());

    // 3. 注册回调
    dev_manager.addAudioCallback(audioSystem.get());

    // 4. 用户界面 (控制台)
    std::cout << "\n=== Function Menu ===" << std::endl;
    std::cout << " send    - (NODE 1) send INPUT.bin (save file as .wav)" << std::endl;
    std::cout << " receive - (NODE 2) receive and save file as OUTPUT.bin" << std::endl;
    std::cout << " stop    - stop activity" << std::endl;
    std::cout << " quit    - exit the program" << std::endl;
    std::cout << "==================" << std::endl;
    std::cout << "Ensure INPUT.bin is on your desktop" << std::endl;
    bool running = true;
    while (running)
    {
        std::cout << "\nPlease Enter: (send, receive, stop, quit): ";
        std::string input;
        std::getline(std::cin, input);
        if (input == "send")
        {
            // 开始发送 (这个函数现在内部包含了保存WAV的逻辑)
            audioSystem->startSendFile();

            // 在主线程中等待，直到发送完成
            std::cout << "正在发送... ";
            while (audioSystem->getCompletionFlag() == 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                std::cout << ".";
            }
            std::cout << "\nSent Completed" << std::endl;
            audioSystem->clearCompletionFlag();
        }
        else if (input == "receive")
        {
            // 开始接收
            audioSystem->startReceiving();

            // 在主线程中等待，直到接收完成
            std::cout << "Waiting For Signal... (Enter 'stop' to Stop)" << std::endl;
            int flag = 0;
            while ((flag = audioSystem->getCompletionFlag()) == 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                // 检查是否有 'stop' 输入
                // (为简单起见，这里省略了非阻塞输入，
                // 你需要按 Enter 键输入 'stop')
            }

            // 处理结果
            if (flag == 2)
            {
                std::cout << "\nReceived Successfully！File saved at." << std::endl;
            }
            else if (flag == 3)
            { 
                std::cout << "\nReceived Failed！CheckSum Error or Invalid Length" << std::endl;
            }
            else if (flag == 9)
            {
                std::cout << "\nReceiving is stopped manually" << std::endl;
            }
            audioSystem->clearCompletionFlag();
        }
        else if (input == "stop")
        {
            audioSystem->stopAll();
        }
        else if (input == "quit")
        {
            running = false;
        }
        else
        {
            std::cout << "Invalid Command" << std::endl;
        }
    }

    // 5. 清理资源
    dev_manager.removeAudioCallback(audioSystem.get());
    std::cout << "Program Ends" << std::endl;

    return 0;
}
#include <JuceHeader.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <iostream>
#include <queue>
#include <thread>
#include <vector>

// 使用 juce 命名空间
using namespace juce;

// ==============================================================================
// 物理层和帧配置
// (与发送端保持一致)
// ==============================================================================
struct PhyConfig {
    static constexpr int BIT_RATE = 500;
    static constexpr float SIGNAL_AMPLITUDE =
        0.5f; // (虽然不用，但保持定义一致)
    static constexpr float ENERGY_THRESHOLD = 0.001f;
};

struct FrameConfig {
    static constexpr uint8_t PREAMBLE_BYTE = 0x55;
    static constexpr int PREAMBLE_LENGTH_BYTES = 4;
    static constexpr uint8_t SOF_BYTE = 0x7E;
};

// ==============================================================================
// NODE 2 接收端核心类
// ==============================================================================

class Node2Receiver : public AudioIODeviceCallback {
  public:
    Node2Receiver()
        : receiverState(ReceiverState::Idle), samplesPerBit(0),
          samplesPerHalfBit(0), currentSampleRate(0.0), receiverBufferPos(0),
          preambleBytesFound(0), receiverBitCount(0), currentByte(0),
          receiverByteCount(0), expectedPayloadLength(0), calculatedChecksum(0),
          completionFlag(0) {
        // 找到桌面路径
        File desktop = File::getSpecialLocation(File::userDesktopDirectory);
        outputFile = desktop.getChildFile("OUTPUT.bin");
        std::cout << "OUTPUT.bin will be saved in: "
                  << outputFile.getFullPathName() << std::endl;
    }

    ~Node2Receiver() {}

    // ==========================================================================
    // JUCE 音频回调
    // ==========================================================================

    void audioDeviceAboutToStart(AudioIODevice *device) override {
        currentSampleRate = device->getCurrentSampleRate();

        std::cout << "\nAudio device started " << device->getName()
                  << std::endl;
        std::cout << "sampleRate: " << currentSampleRate << " Hz" << std::endl;

        if (fmod(currentSampleRate, PhyConfig::BIT_RATE) != 0.0) {
            std::cerr << "!!!! Error:sampleRate(" << currentSampleRate
                      << ") cannot be divided by (" << PhyConfig::BIT_RATE
                      << ") !" << std::endl;
            std::cerr << "!!!! ensure your sampleRate is 48000 Hz。"
                      << std::endl;
            return;
        }

        samplesPerBit = (int)(currentSampleRate / PhyConfig::BIT_RATE);
        samplesPerHalfBit = samplesPerBit / 2;

        std::cout << "bitrate: " << PhyConfig::BIT_RATE << " bps" << std::endl;
        std::cout << "sample for each bit: " << samplesPerBit << std::endl;
    }

    void audioDeviceStopped() override {
        std::cout << "\nAudio device has stopped." << std::endl;
    }
    // ==========================================================================
    // JUCE 音频回调 (!! 已修改，加入能量计 !!)
    // ==========================================================================
    void audioDeviceIOCallback(const float **inputChannelData,
                               int numInputChannels, float **outputChannelData,
                               int numOutputChannels, int numSamples) override {
        const float *inBuffer =
            (numInputChannels > 0) ? inputChannelData[0] : nullptr;

        // --- 接收端逻辑 ---
        if (inBuffer != nullptr) {
            // === 调试用的能量计 ===
            if (receiverState == ReceiverState::Idle) {
                float maxEnergyInBlock = 0.0f;
                for (int i = 0; i < numSamples; ++i) {
                    float energy = inBuffer[i] * inBuffer[i];
                    if (energy > maxEnergyInBlock) {
                        maxEnergyInBlock = energy;
                    }
                }

                // 只在能量超过一个很小的值时才打印，避免刷屏
                if (maxEnergyInBlock > 0.000001f) {
                    // 在 Idle 状态时，打印我们检测到的最大能量
                    // 注意：std::cout 在音频回调中不好，但对于调试是必要的
                    // std::cout << "Idle - Max Energy Detected: " <<
                    // maxEnergyInBlock << std::endl;
                }
            }
            // === 能量计结束 ===

            // 在这个音频块中处理每个采样点
            for (int i = 0; i < numSamples; ++i) {
                // (!! 我们把 Idle 状态的启动逻辑移到下面 !!)
                if (receiverState != ReceiverState::Idle) {
                    processReceivedSample(inBuffer[i]);
                } else {
                    // 在 Idle 状态，只检查能量阈值
                    float energy = inBuffer[i] * inBuffer[i];
                    if (energy > PhyConfig::ENERGY_THRESHOLD) {
                        std::cout << "\n!!! Energy threshold trigger (Energy: "
                                  << energy << ") !!!" << std::endl;
                        // 现在调用 processReceivedSample 来启动状态机
                        processReceivedSample(inBuffer[i]);
                    }
                }
            }
        }

        // --- 确保输出静音 ---
        if (numOutputChannels > 0 && outputChannelData[0] != nullptr) {
            std::memset(outputChannelData[0], 0, numSamples * sizeof(float));
        }
    }

    // ==========================================================================
    // 公共控制函数 (由 main() 调用)
    // ==========================================================================

    void startReceiving() {
        if (receiverState != ReceiverState::Idle) {
            std::cout << "Error: is being receiving already" << std::endl;
            return;
        }

        // 重置所有接收状态机变量
        receiverState = ReceiverState::Idle;
        receiverBufferPos = 0;
        preambleBytesFound = 0;
        receiverBitCount = 0;
        currentByte = 0;
        receiverByteCount = 0;
        expectedPayloadLength = 0;
        calculatedChecksum = 0;
        receivedPayload.clear();
        completionFlag.store(0);

        receiverState = ReceiverState::Idle; // 等待能量检测
        std::cout << "switched to receiving now. Waiting for signal..."
                  << std::endl;
    }

    void stopAll() {
        receiverState = ReceiverState::Idle;
        completionFlag.store(9); // 9 = 手动停止
        std::cout << "Stopped receiving" << std::endl;
    }

    int getCompletionFlag() const { return completionFlag.load(); }
    void clearCompletionFlag() { completionFlag.store(0); }

  private:
    // ==========================================================================
    // 内部逻辑 (私有) - (只保留接收部分)
    // ==========================================================================

    /**
     * @brief [音频线程] 接收状态机 - 处理单个采样点
     */
    void processReceivedSample(float sample) {
        // 状态机核心
        switch (receiverState) {
        case ReceiverState::Idle: {
            // 状态: 空闲
            // 目标: 检测信号能量，寻找前导码

            // 简易能量检测
            float energy = sample * sample;
            if (energy > PhyConfig::ENERGY_THRESHOLD) {
                // 可能有信号了，开始填充半比特缓冲区
                receiverBitBuffer[receiverBufferPos++] = sample;

                // 切换到同步状态
                receiverState = ReceiverState::Syncing;
            }
            break;
        }

        // Syncing, ReadingLength, ReadingPayload, ReadingChecksum
        // 共享相同的采样逻辑
        case ReceiverState::Syncing:
        case ReceiverState::ReadingLength:
        case ReceiverState::ReadingPayload:
        case ReceiverState::ReadingChecksum: {
            receiverBitBuffer[receiverBufferPos++] = sample;

            // 检查是否收集满了半个比特的采样点
            if (receiverBufferPos == samplesPerHalfBit) {
                // 计算前半个比特的平均值
                receiverHalfBitAvg[0] = 0.0f;
                for (int i = 0; i < samplesPerHalfBit; ++i) {
                    receiverHalfBitAvg[0] += receiverBitBuffer[i];
                }
                receiverHalfBitAvg[0] /= samplesPerHalfBit;
            }
            // 检查是否收集满了一个比特的采样点
            else if (receiverBufferPos == samplesPerBit) {
                // 计算后半个比特的平均值
                receiverHalfBitAvg[1] = 0.0f;
                for (int i = samplesPerHalfBit; i < samplesPerBit; ++i) {
                    receiverHalfBitAvg[1] += receiverBitBuffer[i];
                }
                receiverHalfBitAvg[1] /= samplesPerHalfBit;

                // 重置缓冲区位置
                receiverBufferPos = 0;

                // 解码这个比特
                // '1' = H-L (avg[0] > 0, avg[1] < 0)
                // '0' = L-H (avg[0] < 0, avg[1] > 0)

                if (receiverHalfBitAvg[0] > 0.01f &&
                    receiverHalfBitAvg[1] < -0.01f) {
                    processReceivedBit(true); // 收到 1
                } else if (receiverHalfBitAvg[0] < -0.01f &&
                           receiverHalfBitAvg[1] > 0.01f) {
                    processReceivedBit(false); // 收到 0
                } else {
                    // 无效的曼彻斯特码 (可能是噪音)
                    if (receiverState == ReceiverState::Syncing) {
                        // 如果在同步时出错，重置
                        std::cout << "Syncing: Bad Manchester (L/H: "
                                  << receiverHalfBitAvg[0] << ", "
                                  << receiverHalfBitAvg[1]
                                  << "). Resetting state." << std::endl;
                        preambleBytesFound = 0;
                        receiverState = ReceiverState::Idle; // 回到空闲状态
                    } else {
                        // 如果在数据中出错，我们记录一个 "坏" 比特 (例如 0)
                        // 校验和最终会使这个帧失效
                        processReceivedBit(false);
                    }
                }
            }
            break;
        }
        } // end switch
    }

    /**
     * @brief [音频线程] 接收状态机 - 处理一个解码后的比特
     */
    void processReceivedBit(bool bit) {
        // 将比特推入当前字节
        currentByte = (currentByte << 1) | (bit ? 1 : 0);
        receiverBitCount++;

        // 当我们收集满 8 个比特时
        if (receiverBitCount == 8) {
            receiverBitCount = 0;
            processReceivedByte(currentByte); // 处理这个字节
            currentByte = 0;
        }
    }

    /**
     * @brief [音频线程] 接收状态机 - 处理一个完整的字节
     */
    void processReceivedByte(uint8_t byte) {
        // 这是字节级别的状态机
        switch (receiverState) {
        case ReceiverState::Syncing: {
            // 状态: 同步中
            // 目标: 寻找前导码 (0x55) 和 SOF (0x7E)

            if (byte == FrameConfig::PREAMBLE_BYTE) {
                preambleBytesFound++;
                std::cout << "Syncing: Preamble byte 0x55 found ("
                          << preambleBytesFound << ")" << std::endl;
            } else if (byte == FrameConfig::SOF_BYTE &&
                       preambleBytesFound >=
                           FrameConfig::PREAMBLE_LENGTH_BYTES) {
                // 找到了 SOF! 准备读取长度
                std::cout << "\n[Receiver] detected(SOF)，reading length..."
                          << std::endl;
                receiverState = ReceiverState::ReadingLength;
                receiverByteCount = 0; // 重置字节计数器
                expectedPayloadLength = 0;
            } else {
                // 模式被破坏，重置
                std::cout << "Syncing: Expected 0x55, got 0x" << std::hex
                          << (int)byte << std::dec << ". Resetting."
                          << std::endl;
                preambleBytesFound = 0;
            }
            break;
        }

        case ReceiverState::ReadingLength: {
            // 状态: 读取长度 (2 字节)
            if (receiverByteCount == 0) {
                expectedPayloadLength = (uint16_t)byte << 8; // 高位字节
                receiverByteCount = 1;
            } else {
                expectedPayloadLength |= byte; // 低位字节

                std::cout << "[receiver] length detected: "
                          << expectedPayloadLength << " byte" << std::endl;

                // 安全检查
                if (expectedPayloadLength == 0 ||
                    expectedPayloadLength > 10000) // 10k 字节
                {
                    std::cerr << "[receiver] Error: wrong length "
                              << expectedPayloadLength << std::endl;
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

        case ReceiverState::ReadingPayload: {
            // 状态: 读取有效载荷
            receivedPayload.push_back(byte);
            calculatedChecksum ^= byte; // 实时计算校验和
            receiverByteCount++;

            if (receiverByteCount == expectedPayloadLength) {
                // 有效载荷读取完毕，准备读取校验和
                std::cout << "[receiver] overload received, reading checksum..."
                          << std::endl;
                receiverState = ReceiverState::ReadingChecksum;
            }
            break;
        }

        case ReceiverState::ReadingChecksum: {
            // 状态: 读取校验和
            if (byte == calculatedChecksum) {
                // 校验和匹配! 成功!
                std::cout << "[receiver] checksum right!" << std::endl;

                // 保存文件
                outputFile.deleteFile(); // 删除旧文件
                FileOutputStream stream(outputFile);
                if (stream.openedOk()) {
                    stream.write(receivedPayload.data(),
                                 receivedPayload.size());
                    std::cout << "[receiver] file saved to: "
                              << outputFile.getFullPathName() << std::endl;
                    completionFlag.store(2); // 2 = 接收成功
                } else {
                    std::cerr << "[receiver] Error: Unable to write into file!"
                              << std::endl;
                    completionFlag.store(3); // 3 = 接收失败
                }
            } else {
                // 校验和失败
                std::cerr << "[receiver] Checksum wrong! expected: "
                          << (int)calculatedChecksum
                          << ", received: " << (int)byte << std::endl;
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

    // ==========================================================================
    // 成员变量
    // ==========================================================================

    enum class ReceiverState {
        Idle,
        Syncing,
        ReadingLength,
        ReadingPayload,
        ReadingChecksum
    };
    std::atomic<ReceiverState> receiverState;

    double currentSampleRate;
    int samplesPerBit;
    int samplesPerHalfBit;

    File outputFile;

    // 接收缓冲区 (用于重组比特)
    std::array<float, 256>
        receiverBitBuffer; // 应该足够大 (e.g. 8 samples/bit * 2)
    int receiverBufferPos;
    float receiverHalfBitAvg[2] = {0.0f, 0.0f};

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
    // 1 = (未使用)
    // 2 = 接收成功
    // 3 = 接收失败
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
    std::cout << "== NODE 2 (Receiver) ==" << std::endl;
    std::cout << "========================================" << std::endl;

    AudioDeviceManager dev_manager;
    // !! 关键: 只请求输入 !!
    // (注意: JUCE 可能会强制要求 1 个输出, 但我们尝试 1,0)
    juce::String initError = dev_manager.initialiseWithDefaultDevices(1, 0);

    if (initError.isNotEmpty()) {
        // 尝试回退到 (1, 1)，某些驱动程序需要这个
        std::cerr << "Warning: Cannot start in (1, 0), Trying (1, 1)..."
                  << std::endl;
        initError = dev_manager.initialiseWithDefaultDevices(1, 1);
    }

    if (initError.isNotEmpty()) {
        // 如果 (1, 1) 也失败了，则退出
        std::cerr << "!!! Error: Unable to inilize audio device!!!"
                  << std::endl;
        std::cerr << "!!! Error information: " << initError.toStdString()
                  << std::endl;
        std::cout << "press Enter to exit..." << std::endl;
        std::string dummyInput;
        std::getline(std::cin, dummyInput);
        return 1;
    }

    AudioDeviceManager::AudioDeviceSetup dev_info;
    dev_manager.getAudioDeviceSetup(dev_info);

    if (dev_info.sampleRate != 48000.0) {
        std::cout << "currentSampleRate is not 48000 Hz, trying setting..."
                  << std::endl;
        dev_info.sampleRate = 48000.0;
        dev_manager.setAudioDeviceSetup(dev_info, false);
        dev_manager.getAudioDeviceSetup(dev_info);
        if (dev_info.sampleRate != 48000.0) {
            std::cerr << "!! warning: cannot set sampleRate to 48000 Hz."
                      << std::endl;
            std::cerr << "!! erroor may happen. set up you sound card by hand"
                      << std::endl;
        }
    }

    std::unique_ptr<Node2Receiver> audioReceiver;
    audioReceiver.reset(new Node2Receiver());

    dev_manager.addAudioCallback(audioReceiver.get());

    std::cout << "\n=== 功能菜单 ===" << std::endl;
    std::cout << " receive - receive and save into OUTPUT.bin" << std::endl;
    std::cout << " stop    - stop receiving" << std::endl;
    std::cout << " quit    - exit" << std::endl;
    std::cout << "==================" << std::endl;

    bool running = true;
    while (running) {
        std::cout << "\nPlease input command (receive, stop, quit): ";
        std::string input;
        std::getline(std::cin, input);

        if (input == "receive") {
            audioReceiver->startReceiving();

            std::cout << "正在等待信号... (按 'stop' 可手动停止)" << std::endl;
            int flag = 0;
            // 等待，直到 completionFlag 不再是 0
            while ((flag = audioReceiver->getCompletionFlag()) == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            // 处理结果
            if (flag == 2) {
                std::cout << "\nReceive successfully!file has been saved."
                          << std::endl;
            } else if (flag == 3) {
                std::cout << "\nReceiving failed!(wrong checksum or length)"
                          << std::endl;
            } else if (flag == 9) {
                std::cout << "\nReceiving stopped by hand" << std::endl;
            }
            audioReceiver->clearCompletionFlag(); // 重置标志以便下次接收
        } else if (input == "stop") {
            audioReceiver->stopAll();
        } else if (input == "quit") {
            running = false;
        } else {
            std::cout << "Unknow command." << std::endl;
        }
    }

    // 5. 清理资源
    dev_manager.removeAudioCallback(audioReceiver.get());
    std::cout << "Program finished." << std::endl;

    return 0;
}

#include "MainComponent.h"
#include <fstream>
#include <iostream>

MainComponent::MainComponent() {
  setSize(400, 200);
  addAndMakeVisible(sendButton);
  sendButton.onClick = [this] {
    generateSignal();
    isTransmitting = true;
    playPosition = 0;
  };
  setAudioChannels(0, 2); // 无输入，立体声输出
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
      right[i] = val; // 双声道复制
    } else {
      isTransmitting = false;
      break;
    }
  }
}

// 辅助：QPSK 映射 (00, 01, 10, 11 -> 复平面四个象限)
std::complex<float> MainComponent::mapQPSK(int twoBits) {
  // 00 -> 1 + j
  // 01 -> -1 + j
  // 10 -> -1 - j
  // 11 -> 1 - j
  // 归一化幅度为 1/sqrt(2) 保持能量一致，或者直接用 +/- 1
  float i = ((twoBits & 2) == 0) ? 1.0f : -1.0f;
  float q = ((twoBits & 1) == 0) ? 1.0f : -1.0f;
  return std::complex<float>(i, q) * 0.707f;
}

void MainComponent::generateSignal() {
  transmissionSignal.clear();

  // 1. 读取文件
  std::ifstream inFile("D:\\Courses\\4.1\\CN\\Projs\\Proj1\\task5\\INPUT.txt");
  std::string allDataBits;
  if (inFile)
    inFile >> allDataBits;
  else
    allDataBits = std::string(10000, '1'); // Fallback

  // 截断或补齐
  if (allDataBits.length() > Params::TOTAL_BITS)
    allDataBits = allDataBits.substr(0, Params::TOTAL_BITS);
  while (allDataBits.length() < Params::TOTAL_BITS)
    allDataBits += '0';

  std::cout << "Original Bits: " << allDataBits.length() << std::endl;

  // 2. 生成 Preamble (帧头)
  auto preamble = Params::getPreamble();

  // 3. 处理数据分块
  int processedBits = 0;

  // 临时缓冲区用于 FFT
  // 大小为 fftSize * 2 因为 JUCE perform() 处理复数 (real, imag, real, imag...)
  std::vector<float> fftWorkBuffer(Params::fftSize * 2);
  std::vector<float> timeDomainSymbol(Params::fftSize);

  while (processedBits < allDataBits.length()) {
    // --- 组装一个 Chunk ---
    // 为了演示清晰，我们这里不分大的 Chunk (500 bits)，而是流式处理所有 bits
    // 或者保留你的 Chunk 结构，每个 Chunk 前面加一次 Preamble

    // 简单起见，我们每 500 bits 发送一次 Preamble 作为同步记号
    // 或者如果信道稳定，只发一次 Preamble。这里沿用你的逻辑：分块发。

    std::string chunkBits =
        allDataBits.substr(processedBits, Params::CHUNK_SIZE);
    processedBits += chunkBits.length();

    // 对这个 Chunk 进行 Hamming 编码 -> 得到编码后的比特流
    std::string encodedBits = "";
    // 补齐 4 的倍数以便 Hamming 编码
    std::string tempChunk = chunkBits;
    while (tempChunk.length() % 4 != 0)
      tempChunk += '0';

    for (size_t i = 0; i < tempChunk.length(); i += 4) {
      uint8_t nibble = 0;
      for (int b = 0; b < 4; ++b) {
        if (tempChunk[i + b] == '1')
          nibble |= (1 << b);
      }
      uint8_t encoded7 = Params::encodeHamming(nibble);
      // 7 bits output
      for (int b = 0; b < 7; ++b) {
        encodedBits += ((encoded7 >> b) & 1) ? '1' : '0';
      }
    }

    // 现在的 encodedBits 是我们要通过 OFDM 发送的数据

    // --- A. 插入静音和 Preamble ---
    for (int i = 0; i < 4800; ++i)
      transmissionSignal.push_back(0.0f); // 0.1s Gap
    transmissionSignal.insert(transmissionSignal.end(), preamble.begin(),
                              preamble.end());
    for (int i = 0; i < 480; ++i)
      transmissionSignal.push_back(0.0f); // 10ms Guard

    // --- B. OFDM 符号生成 ---
    int bitIndex = 0;
    int totalEncodedLen = encodedBits.length();

    // 循环直到发完这个 Chunk 的所有 bits
    while (bitIndex < totalEncodedLen) {
      // 清空频域 buffer (Real, Imag 交替)
      std::fill(fftWorkBuffer.begin(), fftWorkBuffer.end(), 0.0f);

      // 1. 映射比特到子载波 (Mapping)
      // 我们有 numCarriers 个可用载波，每个载波带 bitsPerSymbol (2) bits
      for (int k = 0; k < Params::numCarriers; ++k) {
        int binIndex = Params::startBin + k;

        // 取出 2 bits
        int twoBits = 0;
        if (bitIndex < totalEncodedLen) {
          if (encodedBits[bitIndex++] == '1')
            twoBits |= 2; // High bit
        }
        if (bitIndex < totalEncodedLen) {
          if (encodedBits[bitIndex++] == '1')
            twoBits |= 1; // Low bit
        }

        // QPSK 映射
        std::complex<float> sym = mapQPSK(twoBits);

        // 填充频域数据 (Hermitian Symmetry 以保证时域为实数)
        // JUCE FFT 输入格式：index i*2 = real, i*2+1 = imag

        // 正频率 binIndex
        fftWorkBuffer[binIndex * 2] = sym.real();
        fftWorkBuffer[binIndex * 2 + 1] = sym.imag();

        // 负频率 (镜像位置) fftSize - binIndex
        // 值必须是共轭 (Conjugate): real 相同, imag 相反
        int mirrorBin = Params::fftSize - binIndex;
        fftWorkBuffer[mirrorBin * 2] = sym.real();
        fftWorkBuffer[mirrorBin * 2 + 1] = -sym.imag();
      }

      // 2. IFFT (频域 -> 时域)
      auto *complexPtr =
          reinterpret_cast<std::complex<float> *>(fftWorkBuffer.data());
      forwardFFT.perform(complexPtr, complexPtr, true);
      // 注意：perform 第三个参数 true 代表 inverse FFT (从频域到时域)

      // 3. 添加循环前缀 (Cyclic Prefix) 并序列化
      // 时域数据现在在 fftWorkBuffer 中
      // (复数形式，但因为共轭对称，虚部应极小接近0)

      // 提取 CP 部分：取 FFT 结果的最后 cpSize 个样本
      // 并放到前面。

      // 将 fftWorkBuffer (交织的复数) 转为纯实数 vector 方便处理
      for (int i = 0; i < Params::fftSize; ++i) {
        timeDomainSymbol[i] = fftWorkBuffer[i * 2]; // 只取实部
      }

      // 写入 CP (后 cpSize 个点)
      for (int i = 0; i < Params::cpSize; ++i) {
        transmissionSignal.push_back(
            timeDomainSymbol[Params::fftSize - Params::cpSize + i]);
      }

      // 写入 OFDM Body
      for (int i = 0; i < Params::fftSize; ++i) {
        transmissionSignal.push_back(timeDomainSymbol[i]);
      }
    }
  }

  // 尾部静音
  for (int i = 0; i < 48000; ++i)
    transmissionSignal.push_back(0.0f);

  std::cout << "OFDM Signal Generated. Size: " << transmissionSignal.size()
            << std::endl;
}

void MainComponent::paint(juce::Graphics &g) {
  g.fillAll(juce::Colours::black);
  g.setColour(juce::Colours::white);
  g.drawText("OFDM Transmitter Ready", getLocalBounds(),
             juce::Justification::centred, true);
}

void MainComponent::resized() { sendButton.setBounds(10, 10, 150, 40); }

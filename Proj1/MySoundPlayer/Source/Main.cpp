#include <JuceHeader.h>
#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <bitset>
#include <queue>
#include <deque>
#include <fstream>

using namespace juce;

// Some Const Variable
#define BIT_WIDTH 60        // Samples For Each bit
#define PREAMBLE_LENGTH 480 // The number of preamble sampling points
#define BIT_NUM 10000       // The number of bit to be sent
#define SUM_THRESHOLD 10    // Preamble detection threshold
#define FREQ 600            // Carrier frequency

class IntegratedAudioSystem : public AudioIODeviceCallback
{
private:
    // Device and Format Managers
    AudioDeviceManager& audioDeviceManager;
    AudioFormatManager formatManager;

    // project 1 task 1 & 2 data variables
    std::vector<float> recordedData;  // data for recorded audio
    std::vector<float> mp3Buffer;     // data for MP3 audio
    std::vector<float> generatedWave; // data for generated sound wave

    // project 1 task 3 data variables
    std::vector<float> carrierWave; // 1-bit wave
    std::vector<float> zeroWave;    // 0-bit wave
    const std::vector<float> preambleWave = { 1, 0.4981031, -0.5037913, -0.9999783, -0.4923696, 0.5094954, 0.9999127, 0.4865715,
                                             -0.5152195, -0.999802, -0.4807087, 0.5209628, 0.9996455, 0.474781, -0.5267245,
                                             -0.999442, -0.4687881, 0.5325038, 0.9991905, 0.4627298, -0.5382999, -0.9988901,
                                             -0.456606, 0.544112, 0.9985398, 0.4504165, -0.5499393, -0.9981386, -0.4441611,
                                             0.555781, 0.9976854, 0.4378397, -0.5616361, -0.9971793, -0.4314521, 0.5675038,
                                             0.9966191, 0.4249981, -0.5733832, -0.996004, -0.4184777, 0.5792734, 0.9953329,
                                             0.4118908, -0.5851735, -0.9946046, -0.4052371, 0.5910825, 0.9938183, 0.3985168,
                                             -0.5969995, -0.9929729, -0.3917296, 0.6029236, 0.9920673, 0.3848756, -0.6088537,
                                             -0.9911006, -0.3779546, 0.6147888, 0.9900715, 0.3709666, -0.620728, -0.9889792,
                                             -0.3639117, 0.6266702, 0.9878226, 0.3567898, -0.6326143, -0.9866006, -0.3496009,
                                             0.6385593, 0.9853122, 0.3423451, -0.6445042, -0.9839563, -0.3350223, 0.6504478,
                                             0.9825318, 0.3276327, -0.656389, -0.9810379, -0.3201763, 0.6623267, 0.9794732,
                                             0.3126531, -0.6682598, -0.977837, -0.3050634, 0.6741871, 0.976128, 0.2974072,
                                             -0.6801074, -0.9743452, -0.2896846, 0.6860195, 0.9724877, 0.2818958, -0.6919223,
                                             -0.9705542, -0.274041, 0.6978145, 0.9685439, 0.2661204, -0.7036949, -0.9664557,
                                             -0.2581341, 0.7095622, 0.9642884, 0.2500824, -0.7154152, -0.9620412, -0.2419656,
                                             0.7212525, 0.9597128, 0.2337839, -0.7270729, -0.9573025, -0.2255375, 0.732875,
                                             0.954809, 0.2172269, -0.7386575, -0.9522313, -0.2088522, 0.7444191, 0.9495686,
                                             0.200414, -0.7501584, -0.9468196, -0.1919124, 0.755874, 0.9439835, 0.183348,
                                             -0.7615646, -0.9410593, -0.1747211, 0.7672286, 0.9380458, 0.1660322, -0.7728647,
                                             -0.9349422, -0.1572817, 0.7784714, 0.9317475, 0.14847, -0.7840473, -0.9284607,
                                             -0.1395978, 0.7895908, 0.9250808, 0.1306655, -0.7951006, -0.9216068, -0.1216736,
                                             0.800575, 0.9180379, 0.1126227, -0.8060126, -0.914373, -0.1035135, 0.8114118,
                                             0.9106113, 0.0943465, -0.8167711, -0.9067517, -0.0851223, 0.8220889, 0.9027935,
                                             0.0758417, -0.8273636, -0.8987357, -0.0665054, 0.8325937, 0.8945774, 0.057114,
                                             -0.8377774, -0.8903177, -0.0476684, 0.8429133, 0.8859558, 0.0381692, -0.8479996,
                                             -0.8814907, -0.0286173, 0.8530347, 0.8769218, 0.0190135, -0.8580169, -0.8722481,
                                             -0.0093586, 0.8629446, 0.8674688, -0.0003464, -0.867816, -0.8625832, 0.0101007,
                                             0.8726295, 0.8575904, -0.0199033, -0.8773834, -0.8524897, 0.0297533, 0.8820758,
                                             0.8575904, -0.0396496, -0.8867052, -0.8419616, 0.0495912, 0.8912696, 0.8365327,
                                             -0.0595772, -0.8957674, -0.830993, 0.0696064, 0.9001968, 0.8253418, -0.0796777,
                                             -0.904556, -0.8195785, 0.0897899, 0.9088432, 0.8137024, -0.0999419, -0.9130566,
                                             -0.8077128, 0.1101326, 0.9171943, 0.8016093, -0.1203606, -0.9212546, -0.7953911,
                                             0.1306247, 0.9252356, 0.7890578, -0.1409237, -0.9291355, -0.7826088, 0.1512561,
                                             0.9329525, 0.7760437, -0.1616207, -0.9366846, -0.7693618, 0.172016, 0.94033,
                                             0.7625628, -0.1824407, -0.9438868, -0.7556463, 0.1928933, 0.9473532, 0.7486117,
                                             -0.2033723, -0.9507272, -0.7414588, 0.2138762, 0.954007, 0.7341871, -0.2244035,
                                             -0.9571908, -0.7267964, 0.2349526, 0.9602765, 0.7192863, -0.2455218, -0.9632622,
                                             -0.7116566, 0.2561096, 0.9661462, 0.703907, -0.2667143, -0.9689264, -0.6960372,
                                             0.2773341, 0.971601, 0.6880472, -0.2879673, -0.974168, -0.6799367, 0.2986122,
                                             0.9766256, 0.6717057, -0.309267, -0.9789718, -0.663354, 0.3199298, 0.9812047,
                                             0.6548816, -0.3305987, -0.9833224, -0.6462885, 0.3412719, 0.9853229, 0.6375746,
                                             -0.3519474, -0.9872044, -0.6287401, 0.3626233, 0.988965, 0.619785, -0.3732975,
                                             -0.9906028, -0.6107094, 0.383968, 0.9921158, 0.6015135, -0.3946328, -0.9935022,
                                             -0.5921974, 0.4052897, 0.99476, 0.5827615, -0.4159367, -0.9958875, -0.5732059,
                                             0.4265715, 0.9968827, 0.5635311, -0.4371921, -0.9977437, -0.5537372, 0.447796,
                                             0.9984687, 0.5438248, -0.4583812, -0.9990559, -0.5337942, 0.4689453, 0.9995034,
                                             0.523646, -0.479486, -0.9998093, -0.5133806, 0.4900009, 0.999972, 0.5029986,
                                             -0.5004877, -0.9999894, -0.4925007, 0.5109439, 0.99986, 0.4818873, -0.5213671,
                                             -0.9995818, -0.4711594, 0.5317549, 0.9991532, 0.4603175, -0.5421047, -0.9998093,
                                             -0.4493624, 0.5524139, 0.9978376, 0.4382951, -0.5626802, -0.9969472, -0.4271164,
                                             0.5729007, 0.9958994, 0.4158271, -0.583073, -0.9946926, -0.4044283, 0.5931944,
                                             0.9933252, 0.3929211, -0.6032622, -0.9917955, -0.3813064, 0.6132736, 0.9917955,
                                             0.3695853, -0.6232261, -0.9882429, -0.3577592, 0.6331168, 0.9862168, 0.3458291,
                                             -0.6429429, -0.9840222, -0.3337964, 0.6527017, 0.9816575, 0.3216624, -0.6623903,
                                             -0.9816575, -0.3094285, 0.6720059, 0.976412, 0.297096, -0.6815455, -0.9735285,
                                             -0.2846666, 0.6910064, 0.9704691, 0.2721416, -0.7003855, -0.9672326, -0.2595228,
                                             0.7096799, 0.9638176, 0.2468118, -0.7188867, -0.9602229, -0.2340102, 0.7280029,
                                             0.9564473, 0.2211198, -0.7370254, -0.9524894, -0.2081425, 0.7459513, 0.9483481,
                                             0.1950801, -0.7547775, -0.9440224, -0.1819345, 0.763501, 0.9440224, 0.1687077,
                                             -0.7721186, -0.9348129, -0.1554018, 0.7806274, 0.9299272, 0.1420189, -0.7890242,
                                             -0.9299272, -0.128561, 0.7973058, 0.9195889, 0.1150305, -0.8054693, -0.9141345,
                                             -0.1014296, 0.8135113, 0.9084887, 0.0877605, -0.8214288, -0.902651, -0.0740258,
                                             0.8292187, 0.8966203, 0.0602278, -0.8368776, -0.8903962, -0.046369, 0.8444026,
                                             0.883978, 0.0324521, -0.8517904, -0.877365, -0.0184795, 0.8590377, 0.8705568,
                                             0.0044541, -0.8661415, -0.8635528, 0.0096215, 0.8730986, 0.8563527, -0.0237445,
                                             -0.8799056, -0.8489561, 0.037912, 0.8865595, 0.8413627, -0.0521211, -0.8930571,
                                             -0.8413627, 0.0663688, 0.8993951, 0.8255845, -0.0806521, -0.9055704, -0.8173995,
                                             0.094968, 0.9115798, 0.809017 };
    std::vector<float> sentData;    // data buffer to be sent
    std::deque<float> receivedData; // data buffer to be received
    File outputFile;                // output file

    // project 1 task 1 & 2 Pointer
    std::atomic<size_t> playPosition{ 0 };     // current playback position
    std::atomic<size_t> mp3PlayPosition{ 0 };  // current MP3 playback position
    std::atomic<size_t> wavePlayPosition{ 0 }; // current generated wave playback position

    // project 1 task 3 Pointer
    std::atomic<int> sendReadPointer{ 0 };    // data buffer to be sent Read Pointer
    std::atomic<int> receiveReadPointer{ 0 }; // data buffer to be received Read Pointer

    // project 1 task 3 Variables Related to Receiving
    std::atomic<float> maxSum{ SUM_THRESHOLD }; // Correlation calculation variable
    std::atomic<int> lowerTicks{ 0 };           // The count of detected preamble
    std::atomic<int> bitsReceived{ 0 };
    std::atomic<bool> begin{ false };             // The marking of Start Demodulating
    std::atomic<bool> preambleSuspected{ false }; // The marking of suspected Preamble

    // project 1 task 1 & 2 current states
    // For project 0, isRecording and isPlayingMP3 can be true at the same time when performing function 2
    // For project 1, is PlayingGeneratedWave is used for task 2
    std::atomic<bool> isRecording{ false };
    std::atomic<bool> isPlaying{ false };
    std::atomic<bool> isPlayingMP3{ false };
    std::atomic<bool> isPlayingGeneratedWave{ false };

    std::atomic<size_t> recordingDuration{ 0 };
    std::atomic<size_t> maxRecordingSamples{ 0 };
    std::atomic<bool> autoStopEnabled{ false };

    // project 1 task 3 states
    enum NodeState
    {
        IDLE,
        ToSend,
        Sending,
        ToReceive,
        Receiving
    };
    std::atomic<NodeState> nodeState{ IDLE };

    AudioIODevice* getCurrentAudioDevice() const
    {
        return audioDeviceManager.getCurrentAudioDevice();
    }

public:
    IntegratedAudioSystem(AudioDeviceManager& devManager) : audioDeviceManager(devManager)
    {
        formatManager.registerBasicFormats();
        generateCarrierWave();
    }

    ~IntegratedAudioSystem() {}

    void generateCarrierWave()
    {
        carrierWave.clear();
        zeroWave.clear();
        receivedData.clear();

        for (int i = 0; i < BIT_WIDTH; ++i)
        {
            // 1bit: cos(2π*FREQ*i/48000)
            carrierWave.push_back(static_cast<float>(cos(2 * MathConstants<double>::pi * FREQ * i / 48000)));
            // 0bit: cos(2π*FREQ*i/48000 + π)
            zeroWave.push_back(static_cast<float>(cos(2 * MathConstants<double>::pi * FREQ * i / 48000 + MathConstants<double>::pi)));
        }
    }

    // ================== 功能1 ===============

    // 录音10s
    void startRecording10Seconds(int sampleRate = 48000)
    {
        stopAllTransmission();
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

    // 加载MP3文件
    bool loadMP3File(const String& filePath)
    {
        mp3Buffer.clear();
        mp3PlayPosition = 0;

        File audioFile(filePath);
        if (!audioFile.existsAsFile())
        {
            std::cout << "error: file not exist: " << filePath << std::endl;
            return false;
        }

        std::unique_ptr<AudioFormatReader> reader(formatManager.createReaderFor(audioFile));
        if (reader == nullptr)
        {
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

        for (size_t i = 0; i < totalSamples; ++i)
        {
            float mixedSample = 0.0f;
            for (int ch = 0; ch < reader->numChannels; ++ch)
            {
                mixedSample += tempBuffer.getSample(ch, i);
            }
            mp3Buffer[i] = mixedSample / reader->numChannels;
        }

        std::cout << "Audio file loaded successfully" << std::endl;
        return true;
    }

    // 播放MP3并同时录音10秒
    void startPlayMP3AndRecord(int sampleRate = 48000)
    {
        stopAllTransmission();
        if (mp3Buffer.empty())
        {
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
    void generateCustomWave(double freq1, double freq2, double duration = 5.0, int sampleRate = 48000)
    {
        stopAllTransmission();
        generatedWave.clear();
        wavePlayPosition = 0;

        size_t totalSamples = static_cast<size_t>(duration * sampleRate);
        generatedWave.resize(totalSamples);

        std::cout << "Generating custom sound wave: f(t) = sin(2π·" << freq1 << "t) + sin(2π·" << freq2 << "t)" << std::endl;
        std::cout << "Duration: " << duration << " seconds" << std::endl;
        std::cout << "Sample rate: " << sampleRate << " Hz" << std::endl;

        for (size_t i = 0; i < totalSamples; ++i)
        {
            double t = static_cast<double>(i) / sampleRate;
            double sample = std::sin(2.0 * MathConstants<double>::pi * freq1 * t) + std::sin(2.0 * MathConstants<double>::pi * freq2 * t);
            generatedWave[i] = static_cast<float>(sample * 0.5);
        }

        isPlayingGeneratedWave = true;
        isRecording = false;
        isPlaying = false;
        isPlayingMP3 = false;

        std::cout << "Custom sound wave generated successfully" << std::endl;
        std::cout << "Playing custom sound wave..." << std::endl;

        std::thread([this, duration]()
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(duration * 1000)));
                if (isPlayingGeneratedWave)
                {
                    isPlayingGeneratedWave = false;
                    std::cout << "Custom wave playback finished" << std::endl;
                } })
            .detach();
    }

    // ================== 功能4: 二进制声波传输系统 ================

    // 打开加载Input.txt或存储Output.txt的存储路径
    void openFileForTransmission(const String& filePath)
    {
        stopAllTransmission();
        sentData.clear();

        File file(filePath);
        // 处理输入文件，进入准备发送模式
        auto path = file.getFullPathName();
        if (path.contains(".txt"))
        {
            FileInputStream iStream(file);
            String iString = iStream.readString();
            sentData.insert(sentData.end(), preambleWave.begin(), preambleWave.end());
            for (char c : iString)
            {
                if (c == '1')
                {
                    sentData.insert(sentData.end(), carrierWave.begin(), carrierWave.end());
                }
                else if (c == '0')
                {
                    sentData.insert(sentData.end(), zeroWave.begin(), zeroWave.end());
                }
            }
            nodeState = ToSend;
            std::cout << "The file has been loaded. It is ready to be sent" << std::endl;
        }

        // 处理输出目录, 进入准备接收模式
        else if (!path.contains("."))
        {
            outputFile = File(path + "/OUTPUT.txt");
            nodeState = ToReceive;
            std::cout << "Ready to receive. The output file will be saved to: " << outputFile.getFullPathName() << std::endl;
        }
    }

    // =================== 通用控制函数 ================

    // 启动定时器
    void startTimer(int seconds)
    {
        std::thread([this, seconds]()
            {
                for (int i = seconds; i > 0; --i) {
                    if (!isRecording && !isPlayingMP3) break;

                    std::cout << "Remaining time: " << i << " s" << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }

                // 时间到，自动停止
                if (isRecording) {
                    stopAll();
                    std::cout << "10s has been reached, recording stopped" << std::endl;
                } })
            .detach();
    }

    // 停止传输或者播放
    void stopAllTransmission()
    {
        nodeState = IDLE;
        sentData.clear();
        receivedData.clear();
        sendReadPointer = 0;
    }

    void stopAll()
    {
        isRecording = false;
        isPlaying = false;
        isPlayingMP3 = false;
        isPlayingGeneratedWave = false;
        stopAllTransmission();
        autoStopEnabled = false;
        std::cout << "All activities have been stopped" << std::endl;
        std::cout << "Final sample #: " << recordedData.size() << std::endl;
    }

    // 播放录音
    void startPlayingRecording()
    {
        stopAllTransmission();
        if (recordedData.empty())
        {
            std::cout << "error: no record audio, please record first" << std::endl;
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
    void stopPlaying()
    {
        isPlaying = false;
        isPlayingMP3 = false;
        isPlayingGeneratedWave = false;
        std::cout << "Stop playing the audio..." << std::endl;
    }

    // 开始发送
    void startSending()
    {
        if (nodeState != ToSend)
        {
            std::cout << "The node is not ready to send." << std::endl;
            return;
        }

        sendReadPointer = 0;
        nodeState = Sending;
        std::cout << "Start Sending..." << std::endl;
    }

    // 开始接收
    void startReceiving()
    {
        if (nodeState != ToReceive)
        {
            std::cout << "The node is not ready to receive." << std::endl;
            return;
        }

        receivedData.clear();
        maxSum = SUM_THRESHOLD;
        lowerTicks = 0;
        bitsReceived = 0;
        begin = false;
        preambleSuspected = false;

        nodeState = Receiving;
        std::cout << "Start Receiving..." << std::endl;
    }

    // 停止接收
    void stopReceiving()
    {
        if (nodeState != Receiving)
        {
            std::cout << "The node is not receiving." << std::endl;
            return;
        }
        nodeState = IDLE;
        std::cout << "Receiving has stopped, Received in total " << bitsReceived << " bit" << std::endl;
    }

    // 音量限制函数
    float limitVolume(float sample, float min = -1.0f, float max = 1.0f)
    {
        if (sample < min)
            return min;
        if (sample > max)
            return max;
        return sample;
    }

    // ================ 音频回调核心函数 ===============
    void audioDeviceIOCallback(const float** inputChannelData,
        int numInputChannels,
        float** outputChannelData,
        int numOutputChannels,
        int numSamples) override
    {
        // 音频处理设备 音频处理通道 和 采样等参数
        AudioIODevice* currentDevice = getCurrentAudioDevice();
        auto activeInputChannels = currentDevice->getActiveInputChannels();
        auto activeOutputChannels = currentDevice->getActiveOutputChannels();
        auto maxInputChannels = activeInputChannels.getHighestBit() + 1;
        auto maxOutputChannels = activeOutputChannels.getHighestBit() + 1;

        // 预处理
        for (auto channel = 0; channel < maxOutputChannels; ++channel)
        {
            if ((!activeInputChannels[channel] || !activeOutputChannels[channel]) || maxInputChannels == 0)
            {
                for (int ch = 0; ch < numOutputChannels; ++ch)
                {
                    if (outputChannelData[ch] != nullptr)
                    {
                        std::memset(outputChannelData[ch], 0, numSamples * sizeof(float));
                    }
                }
            }
            else
            {
                auto actualInputChannel = channel % maxInputChannels;
                const float* inBuffer = (inputChannelData[actualInputChannel] != nullptr) ? inputChannelData[actualInputChannel] : nullptr;
                float* outBuffer = (outputChannelData[channel] != nullptr) ? outputChannelData[channel] : nullptr;
                switch (nodeState)
                {
                    // 1. 发送部分
                case Sending:
                    if (outBuffer != nullptr)
                    {
                        std::memset(outBuffer, 0, numSamples * sizeof(float));

                        for (int i = 0; i < numSamples; ++i, ++sendReadPointer)
                        {
                            if (sendReadPointer < sentData.size())
                            {
                                outBuffer[i] += sentData[sendReadPointer];
                            }
                            else
                            {
                                sendReadPointer = 0;
                                nodeState = IDLE;
                                break;
                            }
                        }
                    }
                    break;

                    // 2. 接收部分
                case Receiving:
                    if (inBuffer != nullptr)
                    {
                        for (int i = 0; i < numSamples; ++i)
                        {
                            receivedData.push_back(inBuffer[i]);
                        }
                        // 检测前导码,用sum评估两端编码的相似程度，用maxsum检测最相似情况
                        while (receivedData.size() >= PREAMBLE_LENGTH && !begin)
                        {
                            float sum = 0;
                            for (int j = 0; j < PREAMBLE_LENGTH; ++j)
                            {
                                sum += receivedData[j] * preambleWave[j];
                            }

                            if (sum > maxSum)
                            {
                                maxSum = sum;
                                lowerTicks = 0;
                                preambleSuspected = true;
                            }
                            else if (preambleSuspected && ++lowerTicks >= 480)
                            {
                                begin = true;
                            }
                            receivedData.pop_front();
                        }

                        // 解调
                        while (begin && receivedData.size() >= BIT_WIDTH)
                        {
                            float sum = 0;
                            for (int i = 0; i < BIT_WIDTH; ++i)
                            {
                                sum += receivedData[0] * carrierWave[i];
                                receivedData.pop_front();
                            }

                            // 根据相关性判断比特值并写入文件
                            outputFile.appendText(sum < 0 ? "0" : "1");

                            // 接收完所有比特后切换状态
                            if (++bitsReceived == BIT_NUM)
                            {
                                nodeState = IDLE;
                                begin = false;
                                break;
                            }
                        }
                    }
                    if (outBuffer != nullptr)
                    {
                        std::memset(outBuffer, 0, numSamples * sizeof(float));
                    }
                    break;
                default:
                    if (outBuffer != nullptr)
                    {
                        std::memset(outBuffer, 0, numSamples * sizeof(float));
                    }
                    break;
                }
            }
        }

        // 3. 录音部分
        if (isRecording && inputChannelData != nullptr && numInputChannels > 0)
        {
            for (int i = 0; i < numSamples; i++)
            {
                if (autoStopEnabled && recordingDuration >= maxRecordingSamples)
                {
                    stopAll();
                    break;
                }

                float inputSample = inputChannelData[0][i];
                recordedData.push_back(inputSample);
                recordingDuration++;
            }
        }

        // 4. 输出部分
        if (outputChannelData != nullptr && numOutputChannels > 0)
        {
            for (int i = 0; i < numSamples; i++)
            {
                float outputSample = 0.0f;

                // 播放MP3音乐
                if (isPlayingMP3 && mp3PlayPosition < mp3Buffer.size())
                {
                    outputSample += mp3Buffer[mp3PlayPosition] * 0.7f;
                    mp3PlayPosition++;
                }

                // 播放录音
                if (isPlaying && playPosition < recordedData.size())
                {
                    outputSample += recordedData[playPosition];
                    playPosition++;
                }

                // 播放生成的声波
                if (isPlayingGeneratedWave && wavePlayPosition < generatedWave.size())
                {
                    outputSample += generatedWave[wavePlayPosition];
                    wavePlayPosition++;
                }

                // 限制音量
                outputSample = limitVolume(outputSample);

                // 写入输出通道
                for (int channel = 0; channel < numOutputChannels; channel++)
                {
                    if (outputChannelData[channel] != nullptr)
                    {
                        outputChannelData[channel][i] = outputSample;
                    }
                }
            }
        }
        else
        {
            // 无输出
            for (int channel = 0; channel < numOutputChannels; channel++)
            {
                if (outputChannelData[channel] != nullptr)
                {
                    for (int i = 0; i < numSamples; i++)
                    {
                        outputChannelData[channel][i] = 0.0f;
                    }
                }
            }
        }
        // 检查播放结束
        checkPlaybackCompletion();
    }

    // 检查播放是否完成
    void checkPlaybackCompletion()
    {
        if (isPlaying && playPosition >= recordedData.size())
        {
            isPlaying = false;
            std::cout << "Record audio playing finished" << std::endl;
        }
        if (isPlayingMP3 && mp3PlayPosition >= mp3Buffer.size())
        {
            isPlayingMP3 = false;
            std::cout << "MP3 playing finished" << std::endl;
        }
        if (isPlayingGeneratedWave && wavePlayPosition >= generatedWave.size())
        {
            isPlayingGeneratedWave = false;
            std::cout << "Generated wave playing finished" << std::endl;
        }
    }

    void audioDeviceAboutToStart(AudioIODevice* device) override
    {
        std::cout << "Audio device: " << device->getName() << std::endl;
        std::cout << "Sample rate: " << device->getCurrentSampleRate() << " Hz" << std::endl;
    }

    void audioDeviceStopped() override
    {
        std::cout << "Audio device has stopped" << std::endl;
    }

    // ==================== 工具函数 ====================

    size_t getRecordedDataSize() const
    {
        return recordedData.size();
    }

    void clearRecordedData()
    {
        recordedData.clear();
        playPosition = 0;
        recordingDuration = 0;
        std::cout << "Audio data have been cleared" << std::endl;
    }

    void printStatus()
    {
        std::cout << "=== 系统状态 ===" << std::endl;
        std::cout << "录音数据: " << recordedData.size() << " 样本" << std::endl;
        std::cout << "MP3文件: " << (mp3Buffer.empty() ? "未加载" : "已加载") << std::endl;
        std::cout << "生成波形: " << (generatedWave.empty() ? "未生成" : "已生成") << std::endl;
        std::cout << "录音状态: " << (isRecording ? "进行中" : "停止") << std::endl;
        std::cout << "播放状态: " << (isPlaying ? "进行中" : "停止") << std::endl;
        std::cout << "生成波形播放: " << (isPlayingGeneratedWave ? "进行中" : "停止") << std::endl;
        std::cout << "传输状态: ";
        switch (nodeState)
        {
        case IDLE:
            std::cout << "就绪";
            break;
        case ToSend:
            std::cout << "准备发送";
            break;
        case Sending:
            std::cout << "发送中";
            break;
        case ToReceive:
            std::cout << "准备接收";
            break;
        case Receiving:
            std::cout << "接收中";
            break;
        }
        std::cout << std::endl;

        if (!recordedData.empty())
        {
            double duration = recordedData.size() / 48000.0;
            std::cout << "录音时长: " << duration << " 秒" << std::endl;
        }
        if (!generatedWave.empty())
        {
            double duration = generatedWave.size() / 48000.0;
            std::cout << "生成波形时长: " << duration << " 秒" << std::endl;
        }
    }
};

//==============================================================================
int main(int argc, char* argv[])
{
    std::cout << "=== 集成音频系统 ===" << std::endl;
    std::cout << "功能1: 录制10秒音频并播放" << std::endl;
    std::cout << "功能2: 播放MP3文件并同时录音" << std::endl;
    std::cout << "功能3: 生成并播放指定频率的两个声波" << std::endl;
    std::cout << "功能4: 二进制声波传输系统" << std::endl;
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
    audioSystem.reset(new IntegratedAudioSystem(dev_manager));
    dev_manager.addAudioCallback(audioSystem.get());

    // 用户界面
    std::cout << "\n=== 功能菜单 ===" << std::endl;
    std::cout << "1 - 功能1: 录制10秒(自动停止)" << std::endl;
    std::cout << "2 - 功能2: 播放MP3并录音10秒" << std::endl;
    std::cout << "3 - 生成两个指定频率的声波, 输入声波频率(Hz):" << std::endl;
    std::cout << "l <路径> - 加载MP3文件/TXT文件或者传输用的目录" << std::endl;
    std::cout << "t - 发送数据(需先加载TXT文件)" << std::endl;
    std::cout << "r - 接收数据(需先指定输出目录)" << std::endl;
    std::cout << "p - 播放录音" << std::endl;
    std::cout << "x - 停止播放" << std::endl;
    std::cout << "s - 停止所有活动" << std::endl;
    std::cout << "c - 清空录音数据" << std::endl;
    std::cout << "i - 显示状态" << std::endl;
    std::cout << "q - 退出程序" << std::endl;
    std::cout << "==================" << std::endl;
    std::cout << "MP3文件示例: l C:/preaudio.mp3" << std::endl;
    std::cout << "自定义频率示例: 3 1000 10000 (生成1kHz和10kHz的声波)" << std::endl;
    std::cout << "传输文件示例: l C:/data.txt (发送) 或 l C:/outputdir (接收)" << std::endl;

    bool running = true;
    while (running)
    {
        std::cout << "\n请输入命令: ";
        std::string input;
        std::getline(std::cin, input);

        if (input.empty())
            continue;

        std::string command = input.substr(0, 1);
        std::string argument = input.length() > 2 ? input.substr(2) : "";

        if (command == "1")
        { // 功能1
            audioSystem->startRecording10Seconds();
        }
        else if (command == "2")
        { // 功能2
            audioSystem->startPlayMP3AndRecord();
        }
        else if (command == "3")
        { // 自定义频率声波
            double freq1, freq2;
            if (sscanf(argument.c_str(), "%lf %lf", &freq1, &freq2) == 2)
            {
                audioSystem->generateCustomWave(freq1, freq2, 5.0, 48000);
            }
            else
            {
                std::cout << "错误: 请提供两个频率参数，例如: 4 1000 2000" << std::endl;
            }
        }
        else if (command == "l")
        {
            if (argument.empty())
            {
                std::cout << "请提供文件路径:" << std::endl;
            }
            else
            {
                String filePath = argument;
                filePath = filePath.replace("\\", "/");
                if (filePath.endsWithIgnoreCase(".mp3"))
                {
                    audioSystem->loadMP3File(filePath);
                }
                else
                {
                    audioSystem->openFileForTransmission(filePath);
                }
            }
        }
        else if (command == "t")
        { // 发送数据
            audioSystem->startSending();
        }
        else if (command == "r")
        { // 接收数据
            audioSystem->startReceiving();
        }
        else if (command == "p")
        { // 播放
            audioSystem->startPlayingRecording();
        }
        else if (command == "x")
        { // 停止播放
            audioSystem->stopPlaying();
        }
        else if (command == "s")
        { // 停止所有
            audioSystem->stopAll();
        }
        else if (command == "c")
        { // 清空
            audioSystem->clearRecordedData();
        }
        else if (command == "i")
        { // 状态
            audioSystem->printStatus();
        }
        else if (command == "q")
        { // 退出
            running = false;
        }
        else
        {
            std::cout << "未知命令" << std::endl;
        }
    }

    // 清理资源
    dev_manager.removeAudioCallback(audioSystem.get());
    std::cout << "程序结束" << std::endl;

    return 0;
}
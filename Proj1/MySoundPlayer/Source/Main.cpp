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
    const std::vector<float> preambleWave = { 1.0f, 0.4981031f, -0.5037913f, -0.9999783f, -0.4923696f, 0.5094954f, 0.9999127f, 0.4865715f,
                                         -0.5152195f, -0.999802f, -0.4807087f, 0.5209628f, 0.9996455f, 0.474781f, -0.5267245f,
                                         -0.999442f, -0.4687881f, 0.5325038f, 0.9991905f, 0.4627298f, -0.5382999f, -0.9988901f,
                                         -0.456606f, 0.544112f, 0.9985398f, 0.4504165f, -0.5499393f, -0.9981386f, -0.4441611f,
                                         0.555781f, 0.9976854f, 0.4378397f, -0.5616361f, -0.9971793f, -0.4314521f, 0.5675038f,
                                         0.9966191f, 0.4249981f, -0.5733832f, -0.996004f, -0.4184777f, 0.5792734f, 0.9953329f,
                                         0.4118908f, -0.5851735f, -0.9946046f, -0.4052371f, 0.5910825f, 0.9938183f, 0.3985168f,
                                         -0.5969995f, -0.9929729f, -0.3917296f, 0.6029236f, 0.9920673f, 0.3848756f, -0.6088537f,
                                         -0.9911006f, -0.3779546f, 0.6147888f, 0.9900715f, 0.3709666f, -0.620728f, -0.9889792f,
                                         -0.3639117f, 0.6266702f, 0.9878226f, 0.3567898f, -0.6326143f, -0.9866006f, -0.3496009f,
                                         0.6385593f, 0.9853122f, 0.3423451f, -0.6445042f, -0.9839563f, -0.3350223f, 0.6504478f,
                                         0.9825318f, 0.3276327f, -0.656389f, -0.9810379f, -0.3201763f, 0.6623267f, 0.9794732f,
                                         0.3126531f, -0.6682598f, -0.977837f, -0.3050634f, 0.6741871f, 0.976128f, 0.2974072f,
                                         -0.6801074f, -0.9743452f, -0.2896846f, 0.6860195f, 0.9724877f, 0.2818958f, -0.6919223f,
                                         -0.9705542f, -0.274041f, 0.6978145f, 0.9685439f, 0.2661204f, -0.7036949f, -0.9664557f,
                                         -0.2581341f, 0.7095622f, 0.9642884f, 0.2500824f, -0.7154152f, -0.9620412f, -0.2419656f,
                                         0.7212525f, 0.9597128f, 0.2337839f, -0.7270729f, -0.9573025f, -0.2255375f, 0.732875f,
                                         0.954809f, 0.2172269f, -0.7386575f, -0.9522313f, -0.2088522f, 0.7444191f, 0.9495686f,
                                         0.200414f, -0.7501584f, -0.9468196f, -0.1919124f, 0.755874f, 0.9439835f, 0.183348f,
                                         -0.7615646f, -0.9410593f, -0.1747211f, 0.7672286f, 0.9380458f, 0.1660322f, -0.7728647f,
                                         -0.9349422f, -0.1572817f, 0.7784714f, 0.9317475f, 0.14847f, -0.7840473f, -0.9284607f,
                                         -0.1395978f, 0.7895908f, 0.9250808f, 0.1306655f, -0.7951006f, -0.9216068f, -0.1216736f,
                                         0.800575f, 0.9180379f, 0.1126227f, -0.8060126f, -0.914373f, -0.1035135f, 0.8114118f,
                                         0.9106113f, 0.0943465f, -0.8167711f, -0.9067517f, -0.0851223f, 0.8220889f, 0.9027935f,
                                         0.0758417f, -0.8273636f, -0.8987357f, -0.0665054f, 0.8325937f, 0.8945774f, 0.057114f,
                                         -0.8377774f, -0.8903177f, -0.0476684f, 0.8429133f, 0.8859558f, 0.0381692f, -0.8479996f,
                                         -0.8814907f, -0.0286173f, 0.8530347f, 0.8769218f, 0.0190135f, -0.8580169f, -0.8722481f,
                                         -0.0093586f, 0.8629446f, 0.8674688f, -0.0003464f, -0.867816f, -0.8625832f, 0.0101007f,
                                         0.8726295f, 0.8575904f, -0.0199033f, -0.8773834f, -0.8524897f, 0.0297533f, 0.8820758f,
                                         0.8575904f, -0.0396496f, -0.8867052f, -0.8419616f, 0.0495912f, 0.8912696f, 0.8365327f,
                                         -0.0595772f, -0.8957674f, -0.830993f, 0.0696064f, 0.9001968f, 0.8253418f, -0.0796777f,
                                         -0.904556f, -0.8195785f, 0.0897899f, 0.9088432f, 0.8137024f, -0.0999419f, -0.9130566f,
                                         -0.8077128f, 0.1101326f, 0.9171943f, 0.8016093f, -0.1203606f, -0.9212546f, -0.7953911f,
                                         0.1306247f, 0.9252356f, 0.7890578f, -0.1409237f, -0.9291355f, -0.7826088f, 0.1512561f,
                                         0.9329525f, 0.7760437f, -0.1616207f, -0.9366846f, -0.7693618f, 0.172016f, 0.94033f,
                                         0.7625628f, -0.1824407f, -0.9438868f, -0.7556463f, 0.1928933f, 0.9473532f, 0.7486117f,
                                         -0.2033723f, -0.9507272f, -0.7414588f, 0.2138762f, 0.954007f, 0.7341871f, -0.2244035f,
                                         -0.9571908f, -0.7267964f, 0.2349526f, 0.9602765f, 0.7192863f, -0.2455218f, -0.9632622f,
                                         -0.7116566f, 0.2561096f, 0.9661462f, 0.703907f, -0.2667143f, -0.9689264f, -0.6960372f,
                                         0.2773341f, 0.971601f, 0.6880472f, -0.2879673f, -0.974168f, -0.6799367f, 0.2986122f,
                                         0.9766256f, 0.6717057f, -0.309267f, -0.9789718f, -0.663354f, 0.3199298f, 0.9812047f,
                                         0.6548816f, -0.3305987f, -0.9833224f, -0.6462885f, 0.3412719f, 0.9853229f, 0.6375746f,
                                         -0.3519474f, -0.9872044f, -0.6287401f, 0.3626233f, 0.988965f, 0.619785f, -0.3732975f,
                                         -0.9906028f, -0.6107094f, 0.383968f, 0.9921158f, 0.6015135f, -0.3946328f, -0.9935022f,
                                         -0.5921974f, 0.4052897f, 0.99476f, 0.5827615f, -0.4159367f, -0.9958875f, -0.5732059f,
                                         0.4265715f, 0.9968827f, 0.5635311f, -0.4371921f, -0.9977437f, -0.5537372f, 0.447796f,
                                         0.9984687f, 0.5438248f, -0.4583812f, -0.9990559f, -0.5337942f, 0.4689453f, 0.9995034f,
                                         0.523646f, -0.479486f, -0.9998093f, -0.5133806f, 0.4900009f, 0.999972f, 0.5029986f,
                                         -0.5004877f, -0.9999894f, -0.4925007f, 0.5109439f, 0.99986f, 0.4818873f, -0.5213671f,
                                         -0.9995818f, -0.4711594f, 0.5317549f, 0.9991532f, 0.4603175f, -0.5421047f, -0.9998093f,
                                         -0.4493624f, 0.5524139f, 0.9978376f, 0.4382951f, -0.5626802f, -0.9969472f, -0.4271164f,
                                         0.5729007f, 0.9958994f, 0.4158271f, -0.583073f, -0.9946926f, -0.4044283f, 0.5931944f,
                                         0.9933252f, 0.3929211f, -0.6032622f, -0.9917955f, -0.3813064f, 0.6132736f, 0.9917955f,
                                         0.3695853f, -0.6232261f, -0.9882429f, -0.3577592f, 0.6331168f, 0.9862168f, 0.3458291f,
                                         -0.6429429f, -0.9840222f, -0.3337964f, 0.6527017f, 0.9816575f, 0.3216624f, -0.6623903f,
                                         -0.9816575f, -0.3094285f, 0.6720059f, 0.976412f, 0.297096f, -0.6815455f, -0.9735285f,
                                         -0.2846666f, 0.6910064f, 0.9704691f, 0.2721416f, -0.7003855f, -0.9672326f, -0.2595228f,
                                         0.7096799f, 0.9638176f, 0.2468118f, -0.7188867f, -0.9602229f, -0.2340102f, 0.7280029f,
                                         0.9564473f, 0.2211198f, -0.7370254f, -0.9524894f, -0.2081425f, 0.7459513f, 0.9483481f,
                                         0.1950801f, -0.7547775f, -0.9440224f, -0.1819345f, 0.763501f, 0.9440224f, 0.1687077f,
                                         -0.7721186f, -0.9348129f, -0.1554018f, 0.7806274f, 0.9299272f, 0.1420189f, -0.7890242f,
                                         -0.9299272f, -0.128561f, 0.7973058f, 0.9195889f, 0.1150305f, -0.8054693f, -0.9141345f,
                                         -0.1014296f, 0.8135113f, 0.9084887f, 0.0877605f, -0.8214288f, -0.902651f, -0.0740258f,
                                         0.8292187f, 0.8966203f, 0.0602278f, -0.8368776f, -0.8903962f, -0.046369f, 0.8444026f,
                                         0.883978f, 0.0324521f, -0.8517904f, -0.877365f, -0.0184795f, 0.8590377f, 0.8705568f,
                                         0.0044541f, -0.8661415f, -0.8635528f, 0.0096215f, 0.8730986f, 0.8563527f, -0.0237445f,
                                         -0.8799056f, -0.8489561f, 0.037912f, 0.8865595f, 0.8413627f, -0.0521211f, -0.8930571f,
                                         -0.8413627f, 0.0663688f, 0.8993951f, 0.8255845f, -0.0806521f, -0.9055704f, -0.8173995f,
                                         0.094968f, 0.9115798f, 0.809017f };
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
            for (size_t ch = 0; ch < reader->numChannels; ++ch)
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
            for (juce_wchar  c : iString)
            {
                if (c == L'1')
                {
                    sentData.insert(sentData.end(), carrierWave.begin(), carrierWave.end());
                }
                else if (c == L'0')
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
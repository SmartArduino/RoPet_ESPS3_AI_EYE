#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <esp_timer.h>

#include <string>
#include <mutex>
#include <list>
#include <vector>
#include <condition_variable>
#include <memory>

#include <opus_encoder.h>
#include <opus_decoder.h>
#include <opus_resampler.h>

#include "protocol.h"
#include "ota.h"
#include "background_task.h"
#include "audio_processor.h"
#include "wake_word.h"
#include "audio_debugger.h"

#if (CONFIG_LCD_GC9A01_240X240 || CONFIG_LCD_ST7796_240X240) && CONFIG_USE_EYE_STYLE_VB6824
#include "eye_data/240_240/blood.h"
#include "eye_data/240_240/cospa.h"
#include "eye_data/240_240/default.h"
#include "eye_data/240_240/sclera_common.h"
#include "eye_data/240_240/spikes.h"
#include "eye_data/240_240/ribbon.h"
#include "eye_data/240_240/black_star.h"
#include "eye_data/240_240/straw.h"
#include "eye_data/240_240/upper_lower_common.h"
#elif CONFIG_LCD_GC9A01_160X160 && CONFIG_USE_EYE_STYLE_VB6824
#include "eye_data/160_160/blood.h"
#include "eye_data/160_160/cospa.h"
#include "eye_data/160_160/default.h"
#include "eye_data/160_160/sclera_common.h"
#include "eye_data/160_160/spikes.h"
#include "eye_data/160_160/ribbon.h"
#include "eye_data/160_160/black_star.h"
#include "eye_data/160_160/straw.h"
#include "eye_data/160_160/upper_lower_common.h"
#else
#include "eye_data/240_240/blood.h"
#include "eye_data/240_240/cospa.h"
#include "eye_data/240_240/default.h"
#include "eye_data/240_240/sclera_common.h"
#include "eye_data/240_240/spikes.h"
#include "eye_data/240_240/ribbon.h"
#include "eye_data/240_240/black_star.h"
#include "eye_data/240_240/straw.h"
#include "eye_data/240_240/upper_lower_common.h"
#endif

#if CONFIG_USE_EYE_STYLE_ES8311 || CONFIG_BOARD_TYPE_DOIT_ESP32S3_EYE_6824_QSPI
#include "touch_button.h"
#endif

#define SCHEDULE_EVENT (1 << 0)
#define SEND_AUDIO_EVENT (1 << 1)
#define CHECK_NEW_VERSION_DONE_EVENT (1 << 2)

enum AecMode
{
    kAecOff,
    kAecOnDeviceSide,
    kAecOnServerSide,
};

enum DeviceState
{
    kDeviceStateUnknown,
    kDeviceStateStarting,
    kDeviceStateWifiConfiguring,
    kDeviceStateIdle,
    kDeviceStateConnecting,
    kDeviceStateListening,
    kDeviceStateSpeaking,
    kDeviceStateUpgrading,
    kDeviceStateActivating,
    kDeviceStateFatalError
};

#define OPUS_FRAME_DURATION_MS 60
#define MAX_AUDIO_PACKETS_IN_QUEUE (2400 / OPUS_FRAME_DURATION_MS)

#if CONFIG_USE_EYE_STYLE_ES8311 || CONFIG_USE_EYE_STYLE_VB6824
#define IRIS_MIN 300 // Clip lower analogRead() range from IRIS_PIN
#define IRIS_MAX 700 // Clip upper "

#if !defined(IRIS_MAX)
#define MACRO
#define IRIS_MAX 280
#endif // MACRO
#if !defined(IRIS_MIN)
#define MACRO
#define IRIS_MIN 180
#endif // MACRO

#define LINES_PER_BATCH 10 // 缓冲区的行数为10行

#define NOBLINK 0        // Not currently engaged in a blink
#define ENBLINK 1        // Eyelid is currently closing
#define DEBLINK 2        // Eyelid is currently opening
#define BUFFER_SIZE 1024 // 64 to 512 seems optimum = 30 fps for default eye

#define NUM_EYES (1) // 定义眼睛数量
#if CONFIG_LCD_GC9A01_240X240
#define DISPLAY_SIZE 240 // 显示尺寸
#elif CONFIG_LCD_GC9A01_160X160
#define DISPLAY_SIZE 160 //
#else
#define DISPLAY_SIZE 240 // 显示尺寸
#endif

// 跟动画有关
const uint8_t ease[] = {                                                             // Ease in/out curve for eye movements 3*t^2-2*t^3
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 3,                                  // T
    3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 7, 8, 9, 9, 10, 10,                                // h
    11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 22, 23,                  // x
    24, 25, 26, 27, 27, 28, 29, 30, 31, 33, 34, 35, 36, 37, 38, 39,                  // 2
    40, 41, 42, 44, 45, 46, 47, 48, 50, 51, 52, 53, 54, 56, 57, 58,                  // A
    60, 61, 62, 63, 65, 66, 67, 69, 70, 72, 73, 74, 76, 77, 78, 80,                  // l
    81, 83, 84, 85, 87, 88, 90, 91, 93, 94, 96, 97, 98, 100, 101, 103,               // e
    104, 106, 107, 109, 110, 112, 113, 115, 116, 118, 119, 121, 122, 124, 125, 127,  // c
    128, 130, 131, 133, 134, 136, 137, 139, 140, 142, 143, 145, 146, 148, 149, 151,  // J
    152, 154, 155, 157, 158, 159, 161, 162, 164, 165, 167, 168, 170, 171, 172, 174,  // a
    175, 177, 178, 179, 181, 182, 183, 185, 186, 188, 189, 190, 192, 193, 194, 195,  // c
    197, 198, 199, 201, 202, 203, 204, 205, 207, 208, 209, 210, 211, 213, 214, 215,  // o
    216, 217, 218, 219, 220, 221, 222, 224, 225, 226, 227, 228, 228, 229, 230, 231,  // b
    232, 233, 234, 235, 236, 237, 237, 238, 239, 240, 240, 241, 242, 243, 243, 244,  // s
    245, 245, 246, 246, 247, 248, 248, 249, 249, 250, 250, 251, 251, 251, 252, 252,  // o
    252, 253, 253, 253, 254, 254, 254, 254, 254, 255, 255, 255, 255, 255, 255, 255}; // n
#endif

class Application
{
public:
    static Application &GetInstance()
    {
        static Application instance;
        return instance;
    }
    // 删除拷贝构造函数和赋值运算符
    Application(const Application &) = delete;
    Application &operator=(const Application &) = delete;

    void Start();
    DeviceState GetDeviceState() const { return device_state_; }
    bool IsVoiceDetected() const { return voice_detected_; }
    void Schedule(std::function<void()> callback);
    void SetDeviceState(DeviceState state);
    void Alert(const char *status, const char *message, const char *emotion = "", const std::string_view &sound = "");
    void DismissAlert();
    void AbortSpeaking(AbortReason reason);
    void ToggleChatState();
    void StartListening();
    void StopListening();
    void UpdateIotStates();
    void Reboot();
    void WakeWordInvoke(const std::string &wake_word);
    void PlaySound(const std::string_view &sound);
    bool CanEnterSleepMode();
    void SendMcpMessage(const std::string &payload);
    void SetAecMode(AecMode mode);
    AecMode GetAecMode() const { return aec_mode_; }
    BackgroundTask *GetBackgroundTask() const { return background_task_; }
    void PushMuteAudio(int mute_time);

#if defined(CONFIG_VB6824_OTA_SUPPORT) && CONFIG_VB6824_OTA_SUPPORT == 1
    void ReleaseDecoder();
    void ShowOtaInfo(const std::string &code, const std::string &ip = "");
#endif

#if CONFIG_USE_EYE_STYLE_ES8311 || CONFIG_USE_EYE_STYLE_VB6824 // 如果开启魔眼显示
    // 眼睛状态和位置
    bool is_blink;
    bool is_track;
    int16_t eyeNewX;       // 新眼睛位置
    int16_t eyeNewY;       // 新眼睛位置
    uint8_t eye_style_num; // 眼睛样式
    // 眼睛图形数据指针
    const uint16_t *sclera;
    const uint8_t *upper;
    const uint8_t *lower;
    const uint16_t *polar;
    const uint16_t *iris;
    void eye_style(uint8_t eye_style);
    int linear_map(int x, int in_min, int in_max, int out_min, int out_max);
    int random_range(int min, int max);
    int random_max(int max);

#endif

private:
    Application();
    ~Application();

    std::unique_ptr<WakeWord> wake_word_;
    std::unique_ptr<AudioProcessor> audio_processor_;
    std::unique_ptr<AudioDebugger> audio_debugger_;
    Ota ota_;
    std::mutex mutex_;
    std::list<std::function<void()>> main_tasks_;
    std::unique_ptr<Protocol> protocol_;
    EventGroupHandle_t event_group_ = nullptr;
    esp_timer_handle_t clock_timer_handle_ = nullptr;
    volatile DeviceState device_state_ = kDeviceStateUnknown;
    ListeningMode listening_mode_ = kListeningModeAutoStop;
    AecMode aec_mode_ = kAecOff;

    bool aborted_ = false;
    bool voice_detected_ = false;
    bool busy_decoding_audio_ = false;
    int clock_ticks_ = 0;
    TaskHandle_t check_new_version_task_handle_ = nullptr;

    // Audio encode / decode
    TaskHandle_t audio_loop_task_handle_ = nullptr;
    BackgroundTask *background_task_ = nullptr;
    std::chrono::steady_clock::time_point last_output_time_;
    std::list<AudioStreamPacket> audio_send_queue_;
    std::list<AudioStreamPacket> audio_decode_queue_;
    std::condition_variable audio_decode_cv_;

    // 新增：用于维护音频包的timestamp队列
    std::list<uint32_t> timestamp_queue_;
    std::mutex timestamp_mutex_;

    std::unique_ptr<OpusEncoderWrapper> opus_encoder_;
    std::unique_ptr<OpusDecoderWrapper> opus_decoder_;

    OpusResampler input_resampler_;
    OpusResampler reference_resampler_;
    OpusResampler output_resampler_;

#if CONFIG_USE_EYE_STYLE_ES8311 || CONFIG_USE_EYE_STYLE_VB6824 // 如果开启魔眼显示
    // 声明眼睛状态相关变量
    typedef struct
    {                       // 眨眼状态
        uint8_t state;      // NOBLINK/ENBLINK/DEBLINK
        int32_t duration;   // Duration of blink state (micros)
        uint32_t startTime; // Time (micros) of last state change
    } eyeBlink;
    struct
    {                   // 存放所有眼睛的数组
        eyeBlink blink; // Current blink state
    } eye[1];

    uint16_t oldIris;
    uint16_t newIris;

    // 时间相关变量，按逻辑顺序声明
    uint32_t startTime;       // For FPS indicator
    uint32_t timeOfLastBlink; // 记录上一次眨眼事件的开始时间（以微秒为单位）
    uint32_t timeToNextBlink; // 记录下一次眨眼事件的时间间隔（以微秒为单位）

    TaskHandle_t eye_loop_task_handle_ = NULL; // 魔眼更新任务的句柄
    // static const uint8_t ease[];

#if CONFIG_USE_EYE_STYLE_ES8311 || CONFIG_BOARD_TYPE_DOIT_ESP32S3_EYE_6824_QSPI
    std::unique_ptr<TouchButton> touch_button_; // 按键
#endif

#endif

    void MainEventLoop();
    void OnAudioInput();
    void OnAudioOutput();
    bool ReadAudio(std::vector<int16_t> &data, int sample_rate, int samples);
#ifdef CONFIG_USE_AUDIO_CODEC_ENCODE_OPUS
    bool ReadAudio(std::vector<uint8_t> &opus, int sample_rate, int samples);
#endif
    void WriteAudio(std::vector<int16_t> &data, int sample_rate);
#ifdef CONFIG_USE_AUDIO_CODEC_DECODE_OPUS
    void WriteAudio(std::vector<uint8_t> &opus, int sample_rate);
#endif
    void ResetDecoder();
    void SetDecodeSampleRate(int sample_rate, int frame_duration);
    void CheckNewVersion();
    void ShowActivationCode();
    void OnClockTimer();
    void SetListeningMode(ListeningMode mode);
    void AudioLoop();
#if CONFIG_USE_EYE_STYLE_ES8311 || CONFIG_USE_EYE_STYLE_VB6824 // 如果开启魔眼显示
    void EyeLoop();
    void drawEye(uint8_t e, uint32_t iScale, uint32_t scleraX, uint32_t scleraY, uint32_t uT, uint32_t lT);
    void frame(uint16_t iScale);
    void split(int16_t startValue, // 虹膜缩放的起始值
               int16_t endValue,   // 虹膜缩放的结束值
               uint64_t startTime, // 开始时间（使用`esp_timer_get_time()`获取）
               int32_t duration,   // 动画持续时间（微秒）
               int16_t range);

#endif
};

#endif // _APPLICATION_H_

#include "application.h"
#include "board.h"
#include "display.h"
#include "display/lcd_display.h"
#include "system_info.h"
#include "ml307_ssl_transport.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "font_awesome_symbols.h"
#include "iot/thing_manager.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "audio_debugger.h"

#if CONFIG_USE_AUDIO_PROCESSOR
#include "afe_audio_processor.h"
#else
#include "no_audio_processor.h"
#endif

#if CONFIG_USE_AFE_WAKE_WORD
#include "afe_wake_word.h"
#elif CONFIG_USE_ESP_WAKE_WORD
#include "esp_wake_word.h"
#else
#include "no_wake_word.h"
#endif

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>

#define TAG "Application"

#if defined(CONFIG_USE_AUDIO_CODEC_ENCODE_OPUS) && (CONFIG_USE_WAKE_WORD_DETECT || CONFIG_USE_AUDIO_PROCESSOR)
#error "audoio_processor or wake_word_detect need opus encoder"
#endif

#ifndef CONFIG_TACKGROUND_TASK_STACK_SIZE
#define CONFIG_TACKGROUND_TASK_STACK_SIZE (4096 * 8)
#endif

#ifndef CONFIG_AUDIO_LOOP_TASK_STACK_SIZE
#define CONFIG_AUDIO_LOOP_TASK_STACK_SIZE (4096 * 2)
#endif

static const char *const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "fatal_error",
    "invalid_state"};

Application::Application()
{
    event_group_ = xEventGroupCreate();
    background_task_ = new BackgroundTask(CONFIG_TACKGROUND_TASK_STACK_SIZE);

#if CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

#if CONFIG_USE_AUDIO_PROCESSOR
    audio_processor_ = std::make_unique<AfeAudioProcessor>();
#else
    audio_processor_ = std::make_unique<NoAudioProcessor>();
#endif

#if CONFIG_USE_AFE_WAKE_WORD
    wake_word_ = std::make_unique<AfeWakeWord>();
#elif CONFIG_USE_ESP_WAKE_WORD
    wake_word_ = std::make_unique<EspWakeWord>();
#else
    wake_word_ = std::make_unique<NoWakeWord>();
#endif

#if CONFIG_USE_EYE_STYLE_ES8311 || CONFIG_USE_EYE_STYLE_VB6824 // 如果开启魔眼显示
    is_blink = true;
    is_track = true;
    eyeNewX = 512;
    eyeNewY = 512;
    eye_style_num = 0;
    oldIris = (IRIS_MIN + IRIS_MAX) / 2;
    newIris = 0;
    startTime = 0;
    timeOfLastBlink = 0;
    timeToNextBlink = 0;
    // sclera = sclera_style_ocean_girl;
    sclera = sclera_default;
    upper = upper_default;
    lower = lower_default;
    polar = polar_default;
    iris = iris_default;
    // iris = iris_style_ribbon;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void *arg)
        {
            Application *app = (Application *)arg;
            app->OnClockTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true};
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application()
{
    if (clock_timer_handle_ != nullptr)
    {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    if (background_task_ != nullptr)
    {
        delete background_task_;
    }
    vEventGroupDelete(event_group_);
}

void Application::CheckNewVersion()
{
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // 初始重试延迟为10秒

    while (true)
    {
        SetDeviceState(kDeviceStateActivating);
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        if (!ota_.CheckVersion())
        {
            retry_count++;
            if (retry_count >= MAX_RETRY)
            {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char buffer[128];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, ota_.GetCheckVersionUrl().c_str());
            Alert(Lang::Strings::ERROR, buffer, "sad", Lang::Sounds::P3_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++)
            {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (device_state_ == kDeviceStateIdle)
                {
                    break;
                }
            }
            retry_delay *= 2; // 每次重试后延迟时间翻倍
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // 重置重试延迟时间

        if (ota_.HasNewVersion())
        {
            Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "happy", Lang::Sounds::P3_UPGRADE);

            vTaskDelay(pdMS_TO_TICKS(3000));

            SetDeviceState(kDeviceStateUpgrading);

            display->SetIcon(FONT_AWESOME_DOWNLOAD);
            std::string message = std::string(Lang::Strings::NEW_VERSION) + ota_.GetFirmwareVersion();
            display->SetChatMessage("system", message.c_str());

            auto &board = Board::GetInstance();
            board.SetPowerSaveMode(false);
            wake_word_->StopDetection();
            // 预先关闭音频输出，避免升级过程有音频操作
            auto codec = board.GetAudioCodec();
            codec->EnableInput(false);
            codec->EnableOutput(false);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                audio_decode_queue_.clear();
            }
            background_task_->WaitForCompletion();
            delete background_task_;
            background_task_ = nullptr;
            vTaskDelay(pdMS_TO_TICKS(1000));

            ota_.StartUpgrade([display](int progress, size_t speed)
                              {
                    char buffer[64];
                    snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
                    display->SetChatMessage("system", buffer); });

            // If upgrade success, the device will reboot and never reach here
            display->SetStatus(Lang::Strings::UPGRADE_FAILED);
            ESP_LOGI(TAG, "Firmware upgrade failed...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            Reboot();
            return;
        }

        // No new version, mark the current version as valid
        ota_.MarkCurrentVersionValid();
        if (!ota_.HasActivationCode() && !ota_.HasActivationChallenge())
        {
            xEventGroupSetBits(event_group_, CHECK_NEW_VERSION_DONE_EVENT);
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota_.HasActivationCode())
        {
            ShowActivationCode();
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i)
        {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota_.Activate();
            if (err == ESP_OK)
            {
                xEventGroupSetBits(event_group_, CHECK_NEW_VERSION_DONE_EVENT);
                break;
            }
            else if (err == ESP_ERR_TIMEOUT)
            {
                vTaskDelay(pdMS_TO_TICKS(3000));
            }
            else
            {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (device_state_ == kDeviceStateIdle)
            {
                break;
            }
        }
    }
}

void Application::ShowActivationCode()
{
    auto &message = ota_.GetActivationMessage();
    auto &code = ota_.GetActivationCode();

    struct digit_sound
    {
        char digit;
        const std::string_view &sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{digit_sound{'0', Lang::Sounds::P3_0},
                                                           digit_sound{'1', Lang::Sounds::P3_1},
                                                           digit_sound{'2', Lang::Sounds::P3_2},
                                                           digit_sound{'3', Lang::Sounds::P3_3},
                                                           digit_sound{'4', Lang::Sounds::P3_4},
                                                           digit_sound{'5', Lang::Sounds::P3_5},
                                                           digit_sound{'6', Lang::Sounds::P3_6},
                                                           digit_sound{'7', Lang::Sounds::P3_7},
                                                           digit_sound{'8', Lang::Sounds::P3_8},
                                                           digit_sound{'9', Lang::Sounds::P3_9}}};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "happy", Lang::Sounds::P3_ACTIVATION);

    for (const auto &digit : code)
    {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
                               [digit](const digit_sound &ds)
                               { return ds.digit == digit; });
        if (it != digit_sounds.end())
        {
            PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char *status, const char *message, const char *emotion, const std::string_view &sound)
{
    ESP_LOGW(TAG, "Alert %s: %s [%s]", status, message, emotion);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty())
    {
        ResetDecoder();
        PlaySound(sound);
    }
}

void Application::DismissAlert()
{
    if (device_state_ == kDeviceStateIdle)
    {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::PlaySound(const std::string_view &sound)
{
    // Wait for the previous sound to finish
    {
        std::unique_lock<std::mutex> lock(mutex_);
        audio_decode_cv_.wait(lock, [this]()
                              { return audio_decode_queue_.empty(); });
    }
    background_task_->WaitForCompletion();

#if (defined(CONFIG_SPEAKING_FIRST_MUTE_TIME) && CONFIG_SPEAKING_FIRST_MUTE_TIME > 0)
    PushMuteAudio(CONFIG_SPEAKING_FIRST_MUTE_TIME);
#endif

    const char *data = sound.data();
    size_t size = sound.size();
    for (const char *p = data; p < data + size;)
    {
        auto p3 = (BinaryProtocol3 *)p;
        p += sizeof(BinaryProtocol3);

        auto payload_size = ntohs(p3->payload_size);
        AudioStreamPacket packet;
        packet.sample_rate = 16000;
        packet.frame_duration = 60;
        packet.payload.resize(payload_size);
        memcpy(packet.payload.data(), p3->payload, payload_size);
        p += payload_size;

        std::lock_guard<std::mutex> lock(mutex_);
        audio_decode_queue_.emplace_back(std::move(packet));
    }
}

void Application::ToggleChatState()
{
    if (device_state_ == kDeviceStateActivating)
    {
        SetDeviceState(kDeviceStateIdle);
        return;
    }

    if (!protocol_)
    {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle)
    {
        Schedule([this]()
                 {
                if (!protocol_->IsAudioChannelOpened()) {
                    SetDeviceState(kDeviceStateConnecting);
                    if (!protocol_->OpenAudioChannel()) {
                        return;
                    }
                }

                SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime); });
    }
    else if (device_state_ == kDeviceStateSpeaking)
    {
        Schedule([this]()
                 { AbortSpeaking(kAbortReasonNone); });
    }
    else if (device_state_ == kDeviceStateListening)
    {
        Schedule([this]()
                 { protocol_->CloseAudioChannel(); });
    }
}

void Application::StartListening()
{
    if (device_state_ == kDeviceStateActivating)
    {
        SetDeviceState(kDeviceStateIdle);
        return;
    }

    if (!protocol_)
    {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle)
    {
        Schedule([this]()
                 {
                if (!protocol_->IsAudioChannelOpened()) {
                    SetDeviceState(kDeviceStateConnecting);
                    if (!protocol_->OpenAudioChannel()) {
                        return;
                    }
                }

                SetListeningMode(kListeningModeManualStop); });
    }
    else if (device_state_ == kDeviceStateSpeaking)
    {
        Schedule([this]()
                 {
                AbortSpeaking(kAbortReasonNone);
                SetListeningMode(kListeningModeManualStop); });
    }
}

void Application::StopListening()
{
    const std::array<int, 3> valid_states = {
        kDeviceStateListening,
        kDeviceStateSpeaking,
        kDeviceStateIdle,
    };
    // If not valid, do nothing
    if (std::find(valid_states.begin(), valid_states.end(), device_state_) == valid_states.end())
    {
        return;
    }

    Schedule([this]()
             {
            if (device_state_ == kDeviceStateListening) {
                protocol_->SendStopListening();
                SetDeviceState(kDeviceStateIdle);
            } });
}

void Application::Start()
{
    auto &board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    /* Setup the display */
    auto display = board.GetDisplay();

    /* Setup the audio codec */
    auto codec = board.GetAudioCodec();
#ifdef CONFIG_USE_AUDIO_CODEC_DECODE_OPUS
#else
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(codec->output_sample_rate(), 1, OPUS_FRAME_DURATION_MS);
#endif

#ifdef CONFIG_USE_AUDIO_CODEC_ENCODE_OPUS
#else
    opus_encoder_ = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
    if (aec_mode_ != kAecOff)
    {
        ESP_LOGI(TAG, "AEC mode: %d, setting opus encoder complexity to 0", aec_mode_);
        opus_encoder_->SetComplexity(0);
    }
    else if (board.GetBoardType() == "ml307")
    {
        ESP_LOGI(TAG, "ML307 board detected, setting opus encoder complexity to 5");
        opus_encoder_->SetComplexity(5);
    }
    else
    {
        ESP_LOGI(TAG, "WiFi board detected, setting opus encoder complexity to 0");
        opus_encoder_->SetComplexity(0);
    }
#endif

    if (codec->input_sample_rate() != 16000)
    {
        input_resampler_.Configure(codec->input_sample_rate(), 16000);
        reference_resampler_.Configure(codec->input_sample_rate(), 16000);
    }
    codec->Start();

#if CONFIG_USE_AUDIO_PROCESSOR
    xTaskCreatePinnedToCore([](void *arg)
                            {
            Application* app = (Application*)arg;
            app->AudioLoop();
            vTaskDelete(NULL); }, "audio_loop", CONFIG_AUDIO_LOOP_TASK_STACK_SIZE, this, 8, &audio_loop_task_handle_, 1);
#else
    xTaskCreate([](void *arg)
                {
            Application* app = (Application*)arg;
            app->AudioLoop();
            vTaskDelete(NULL); }, "audio_loop", CONFIG_AUDIO_LOOP_TASK_STACK_SIZE, this, 8, &audio_loop_task_handle_);
#endif
#if CONFIG_USE_EYE_STYLE_ES8311 || CONFIG_USE_EYE_STYLE_VB6824 // 如果开启魔眼显示
    xTaskCreatePinnedToCore([](void *arg)
                            {
            Application* app = (Application*)arg;
            app->EyeLoop();
            vTaskDelete(NULL); }, "eye_loop", 1024 * 4, this, 4, &eye_loop_task_handle_, 0);
#endif

    /* Start the clock timer to update the status bar */
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    /* Wait for the network to be ready */
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);
    // Check for new firmware version or get the MQTT broker address
    CheckNewVersion();
    // Initialize the protocol
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    // Add MCP common tools before initializing the protocol
#if CONFIG_IOT_PROTOCOL_MCP
    McpServer::GetInstance().AddCommonTools();
#endif

    if (ota_.HasMqttConfig())
    {
        protocol_ = std::make_unique<MqttProtocol>();
    }
    else if (ota_.HasWebsocketConfig())
    {
        protocol_ = std::make_unique<WebsocketProtocol>();
    }
    else
    {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnNetworkError([this](const std::string &message)
                              {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, message.c_str(), "sad", Lang::Sounds::P3_EXCLAMATION); });
    protocol_->OnIncomingAudio([this](AudioStreamPacket &&packet)
                               {
            std::lock_guard<std::mutex> lock(mutex_);
            if (device_state_ == kDeviceStateSpeaking && audio_decode_queue_.size() < MAX_AUDIO_PACKETS_IN_QUEUE) {
                audio_decode_queue_.emplace_back(std::move(packet));
            } });
    protocol_->OnAudioChannelOpened([this, codec, &board]()
                                    {
                                        board.SetPowerSaveMode(false);
                                        if (protocol_->server_sample_rate() != codec->output_sample_rate())
                                        {
                                            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                                                     protocol_->server_sample_rate(), codec->output_sample_rate());
                                        }

#if CONFIG_IOT_PROTOCOL_XIAOZHI
                                        auto &thing_manager = iot::ThingManager::GetInstance();
                                        protocol_->SendIotDescriptors(thing_manager.GetDescriptorsJson());
                                        std::string states;
                                        if (thing_manager.GetStatesJson(states, false))
                                        {
                                            protocol_->SendIotStates(states);
                                        }
#endif
                                    });
    protocol_->OnAudioChannelClosed([this, &board]()
                                    {
            board.SetPowerSaveMode(true);
            Schedule([this]() {
                auto display = Board::GetInstance().GetDisplay();
                display->SetChatMessage("system", "");
                SetDeviceState(kDeviceStateIdle);
            }); });
    protocol_->OnIncomingJson([this, display](const cJSON *root)
                              {
            // Parse JSON data
            auto type = cJSON_GetObjectItem(root, "type");
            if (strcmp(type->valuestring, "tts") == 0) {
                auto state = cJSON_GetObjectItem(root, "state");
                if (strcmp(state->valuestring, "start") == 0) {
                    Schedule([this]() {
                        aborted_ = false;
                        if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
                            SetDeviceState(kDeviceStateSpeaking);
                        // 解决第一个字卡顿的问题
#if (defined(CONFIG_SPEAKING_FIRST_MUTE_TIME) && CONFIG_SPEAKING_FIRST_MUTE_TIME > 0)
                            PushMuteAudio(CONFIG_SPEAKING_FIRST_MUTE_TIME);
#endif
                        }
                    });
                } else if (strcmp(state->valuestring, "stop") == 0) {
                    Schedule([this]() {
                        background_task_->WaitForCompletion();
                        if (device_state_ == kDeviceStateSpeaking) {
                            if (listening_mode_ == kListeningModeManualStop) {
                                SetDeviceState(kDeviceStateIdle);
                            } else {
                                SetDeviceState(kDeviceStateListening);
                            }
                        }
                    });
                } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                    auto text = cJSON_GetObjectItem(root, "text");
                    if (cJSON_IsString(text)) {
                        ESP_LOGI(TAG, "<< %s", text->valuestring);
                        Schedule([this, display, message = std::string(text->valuestring)]() {
                            display->SetChatMessage("assistant", message.c_str());
                        });
                    }
                }
            } else if (strcmp(type->valuestring, "stt") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, ">> %s", text->valuestring);
                    Schedule([this, display, message = std::string(text->valuestring)]() {
                        display->SetChatMessage("user", message.c_str());
                    });
                }
            } else if (strcmp(type->valuestring, "llm") == 0) {
                auto emotion = cJSON_GetObjectItem(root, "emotion");
                if (cJSON_IsString(emotion)) {
                    Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                        display->SetEmotion(emotion_str.c_str());
                    });
                }
#if CONFIG_IOT_PROTOCOL_MCP
            } else if (strcmp(type->valuestring, "mcp") == 0) {
                auto payload = cJSON_GetObjectItem(root, "payload");
                if (cJSON_IsObject(payload)) {
                    McpServer::GetInstance().ParseMessage(payload);
                }
#endif
#if CONFIG_IOT_PROTOCOL_XIAOZHI
            } else if (strcmp(type->valuestring, "iot") == 0) {
                auto commands = cJSON_GetObjectItem(root, "commands");
                if (cJSON_IsArray(commands)) {
                    auto& thing_manager = iot::ThingManager::GetInstance();
                    for (int i = 0; i < cJSON_GetArraySize(commands); ++i) {
                        auto command = cJSON_GetArrayItem(commands, i);
                        thing_manager.Invoke(command);
                    }
                }
#endif
            } else if (strcmp(type->valuestring, "system") == 0) {
                auto command = cJSON_GetObjectItem(root, "command");
                if (cJSON_IsString(command)) {
                    ESP_LOGI(TAG, "System command: %s", command->valuestring);
                    if (strcmp(command->valuestring, "reboot") == 0) {
                        // Do a reboot if user requests a OTA update
                        Schedule([this]() {
                            Reboot();
                        });
                    } else {
                        ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                    }
                }
            } else if (strcmp(type->valuestring, "alert") == 0) {
                auto status = cJSON_GetObjectItem(root, "status");
                auto message = cJSON_GetObjectItem(root, "message");
                auto emotion = cJSON_GetObjectItem(root, "emotion");
                if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                    Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::P3_VIBRATION);
                } else {
                    ESP_LOGW(TAG, "Alert command requires status, message and emotion");
                }
            } else {
                ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
            } });
    bool protocol_started = protocol_->Start();
#if CONFIG_USE_EYE_STYLE_ES8311 || CONFIG_BOARD_TYPE_DOIT_ESP32S3_EYE_6824_QSPI
    // 按键初始化
    // touch_button_ = std::make_unique<TouchButton>();
#endif

    audio_debugger_ = std::make_unique<AudioDebugger>();
    audio_processor_->Initialize(codec);
#ifndef CONFIG_USE_AUDIO_CODEC_ENCODE_OPUS
    audio_processor_->OnOutput([this](std::vector<int16_t> &&data)
                               {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (audio_send_queue_.size() >= MAX_AUDIO_PACKETS_IN_QUEUE) {
                    ESP_LOGW(TAG, "Too many audio packets in queue, drop the newest packet");
                    return;
                }
            }
            background_task_->Schedule([this, data = std::move(data)]() mutable {
                opus_encoder_->Encode(std::move(data), [this](std::vector<uint8_t>&& opus) {
                    AudioStreamPacket packet;
                    packet.payload = std::move(opus);
#ifdef CONFIG_USE_SERVER_AEC
                    {
                        std::lock_guard<std::mutex> lock(timestamp_mutex_);
                        if (!timestamp_queue_.empty()) {
                            packet.timestamp = timestamp_queue_.front();
                            timestamp_queue_.pop_front();
                        } else {
                            packet.timestamp = 0;
                        }

                        if (timestamp_queue_.size() > 3) { // 限制队列长度3
                            timestamp_queue_.pop_front(); // 该包发送前先出队保持队列长度
                            return;
                        }
                    }
#endif
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (audio_send_queue_.size() >= MAX_AUDIO_PACKETS_IN_QUEUE) {
                        ESP_LOGW(TAG, "Too many audio packets in queue, drop the oldest packet");
                        audio_send_queue_.pop_front();
                    }
                    audio_send_queue_.emplace_back(std::move(packet));
                    xEventGroupSetBits(event_group_, SEND_AUDIO_EVENT);
                });
            }); });
#endif
    audio_processor_->OnVadStateChange([this](bool speaking)
                                       {
            if (device_state_ == kDeviceStateListening) {
                Schedule([this, speaking]() {
                    if (speaking) {
                        voice_detected_ = true;
                    } else {
                        voice_detected_ = false;
                    }
                    auto led = Board::GetInstance().GetLed();
                    led->OnStateChanged();
                });
            } });

    wake_word_->Initialize(codec);
    wake_word_->OnWakeWordDetected([this](const std::string &wake_word)
                                   { Schedule([this, &wake_word]()
                                              {
                if (!protocol_) {
                    return;
                }

                if (device_state_ == kDeviceStateIdle) {
                    wake_word_->EncodeWakeWordData();

                    if (!protocol_->IsAudioChannelOpened()) {
                        SetDeviceState(kDeviceStateConnecting);
                        if (!protocol_->OpenAudioChannel()) {
                            wake_word_->StartDetection();
                            return;
                        }
                    }

                    ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_USE_AFE_WAKE_WORD
                    AudioStreamPacket packet;
                    // Encode and send the wake word data to the server
                    while (wake_word_->GetWakeWordOpus(packet.payload)) {
                        protocol_->SendAudio(packet);
                    }
                    // Set the chat state to wake word detected
                    protocol_->SendWakeWordDetected(wake_word);
#else
                    // Play the pop up sound to indicate the wake word is detected
                    // And wait 60ms to make sure the queue has been processed by audio task
                    ResetDecoder();
                    PlaySound(Lang::Sounds::P3_POPUP);
                    vTaskDelay(pdMS_TO_TICKS(60));
#endif
                    SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
                } else if (device_state_ == kDeviceStateSpeaking) {
                    AbortSpeaking(kAbortReasonWakeWordDetected);
                } else if (device_state_ == kDeviceStateActivating) {
                    SetDeviceState(kDeviceStateIdle);
                } }); });
    wake_word_->StartDetection();

    // Wait for the new version check to finish
    xEventGroupWaitBits(event_group_, CHECK_NEW_VERSION_DONE_EVENT, pdTRUE, pdFALSE, portMAX_DELAY);
    SetDeviceState(kDeviceStateIdle);

    if (protocol_started)
    {
        std::string message = std::string(Lang::Strings::VERSION) + ota_.GetCurrentVersion();
        display->ShowNotification(message.c_str());
        display->SetChatMessage("system", "");
        // Play the success sound to indicate the device is ready
        ResetDecoder();
        PlaySound(Lang::Sounds::P3_SUCCESS);
    }

    // Print heap stats
    SystemInfo::PrintHeapStats();

    // Enter the main event loop
    MainEventLoop();
}

void Application::OnClockTimer()
{
    clock_ticks_++;

    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar();

    // Print the debug info every 10 seconds
    if (clock_ticks_ % 10 == 0)
    {
        // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
        // SystemInfo::PrintTaskList();
        SystemInfo::PrintHeapStats();

#if 0
            char pcWriteBuffer[1024];
            // 生成任务列表信息到缓冲区
            vTaskList(pcWriteBuffer);
            // 打印任务列表信息
            printf("Task List:\n%s\n", pcWriteBuffer);
#endif

        // If we have synchronized server time, set the status to clock "HH:MM" if the device is idle
        if (ota_.HasServerTime())
        {
            if (device_state_ == kDeviceStateIdle)
            {
                Schedule([this]()
                         {
                        // Set status to clock "HH:MM"
                        time_t now = time(NULL);
                        char time_str[64];
                        strftime(time_str, sizeof(time_str), "%H:%M  ", localtime(&now));
                        Board::GetInstance().GetDisplay()->SetStatus(time_str); });
            }
        }
    }

    // if (clock_ticks_ % 2 == 0)
    // {
    //     Reinitialize();
    // }
}

// Add a async task to MainLoop
void Application::Schedule(std::function<void()> callback)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, SCHEDULE_EVENT);
}

// The Main Event Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::MainEventLoop()
{
    // Raise the priority of the main event loop to avoid being interrupted by background tasks (which has priority 2)
    vTaskPrioritySet(NULL, 3);

    while (true)
    {
        auto bits = xEventGroupWaitBits(event_group_, SCHEDULE_EVENT | SEND_AUDIO_EVENT, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & SEND_AUDIO_EVENT)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            auto packets = std::move(audio_send_queue_);
            lock.unlock();
            for (auto &packet : packets)
            {
                if (!protocol_->SendAudio(packet))
                {
                    break;
                }
            }
        }

        if (bits & SCHEDULE_EVENT)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto &task : tasks)
            {
                task();
            }
        }
    }
}

// The Audio Loop is used to input and output audio data
void Application::AudioLoop()
{
    auto codec = Board::GetInstance().GetAudioCodec();
    while (true)
    {
        OnAudioInput();
        if (codec->output_enabled())
        {
            OnAudioOutput();
        }
#if CONFIG_FREERTOS_HZ == 1000
        vTaskDelay(pdMS_TO_TICKS(10));
#endif
    }
}

void Application::OnAudioOutput()
{
    if (busy_decoding_audio_)
    {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto codec = Board::GetInstance().GetAudioCodec();
    const int max_silence_seconds = 10;

    std::unique_lock<std::mutex> lock(mutex_);
    if (audio_decode_queue_.empty())
    {
        // Disable the output if there is no audio data for a long time
        if (device_state_ == kDeviceStateIdle)
        {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_output_time_).count();
            if (duration > max_silence_seconds)
            {
                codec->EnableOutput(false);
            }
        }
        return;
    }

    auto packet = std::move(audio_decode_queue_.front());
    audio_decode_queue_.pop_front();
    lock.unlock();
    audio_decode_cv_.notify_all();

    int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (free_sram < 10000)
    {
        return;
    }

    // Synchronize the sample rate and frame duration
    SetDecodeSampleRate(packet.sample_rate, packet.frame_duration);

    busy_decoding_audio_ = true;
    background_task_->Schedule([this, codec, packet = std::move(packet)]() mutable
                               {
            busy_decoding_audio_ = false;
            if (aborted_) {
                return;
            }
#ifdef CONFIG_USE_AUDIO_CODEC_DECODE_OPUS
            WriteAudio(packet.payload);
#else
            std::vector<int16_t> pcm;
            if (!opus_decoder_->Decode(std::move(packet.payload), pcm)) {
                return;
            }
            WriteAudio(pcm, opus_decoder_->sample_rate());
#endif
#ifdef CONFIG_USE_SERVER_AEC
            std::lock_guard<std::mutex> lock(timestamp_mutex_);
            timestamp_queue_.push_back(packet.timestamp);
#endif
            last_output_time_ = std::chrono::steady_clock::now(); });
}

void Application::OnAudioInput()
{
    if (wake_word_->IsDetectionRunning())
    {
        std::vector<int16_t> data;
        int samples = wake_word_->GetFeedSize();
        if (samples > 0)
        {
            if (ReadAudio(data, 16000, samples))
            {
                wake_word_->Feed(data);
                return;
            }
        }
    }
    if (audio_processor_->IsRunning())
    {
#ifdef CONFIG_USE_AUDIO_CODEC_ENCODE_OPUS
        int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        if (free_sram < 10000)
        {
            return;
        }
        std::vector<uint8_t> opus;
        if (!ReadAudio(opus, 16000, 30 * 16000 / 1000))
        {
            return;
        }
        AudioStreamPacket packet;
        packet.payload = std::move(opus);
#ifdef CONFIG_USE_SERVER_AEC
        {
            std::lock_guard<std::mutex> lock(timestamp_mutex_);
            if (!timestamp_queue_.empty())
            {
                packet.timestamp = timestamp_queue_.front();
                timestamp_queue_.pop_front();
            }
            else
            {
                packet.timestamp = 0;
            }

            if (timestamp_queue_.size() > 3)
            {                                 // 限制队列长度3
                timestamp_queue_.pop_front(); // 该包发送前先出队保持队列长度
                return;
            }
        }
#endif
        std::lock_guard<std::mutex> lock(mutex_);
        if (audio_send_queue_.size() >= MAX_AUDIO_PACKETS_IN_QUEUE)
        {
            ESP_LOGW(TAG, "Too many audio packets in queue, drop the oldest packet");
            audio_send_queue_.pop_front();
        }
        audio_send_queue_.emplace_back(std::move(packet));
        xEventGroupSetBits(event_group_, SEND_AUDIO_EVENT);
#else
        std::vector<int16_t> data;
        int samples = audio_processor_->GetFeedSize();
        if (samples > 0)
        {
            if (ReadAudio(data, 16000, samples))
            {
                audio_processor_->Feed(data);
                return;
            }
        }
#endif
    }

#if CONFIG_FREERTOS_HZ != 1000
    vTaskDelay(pdMS_TO_TICKS(OPUS_FRAME_DURATION_MS / 2));
#endif
}

bool Application::ReadAudio(std::vector<int16_t> &data, int sample_rate, int samples)
{
    auto codec = Board::GetInstance().GetAudioCodec();
    if (!codec->input_enabled())
    {
        return false;
    }

    if (codec->input_sample_rate() != sample_rate)
    {
        data.resize(samples * codec->input_sample_rate() / sample_rate);
        if (!codec->InputData(data))
        {
            return false;
        }
        if (codec->input_channels() == 2)
        {
            auto mic_channel = std::vector<int16_t>(data.size() / 2);
            auto reference_channel = std::vector<int16_t>(data.size() / 2);
            for (size_t i = 0, j = 0; i < mic_channel.size(); ++i, j += 2)
            {
                mic_channel[i] = data[j];
                reference_channel[i] = data[j + 1];
            }
            auto resampled_mic = std::vector<int16_t>(input_resampler_.GetOutputSamples(mic_channel.size()));
            auto resampled_reference = std::vector<int16_t>(reference_resampler_.GetOutputSamples(reference_channel.size()));
            input_resampler_.Process(mic_channel.data(), mic_channel.size(), resampled_mic.data());
            reference_resampler_.Process(reference_channel.data(), reference_channel.size(), resampled_reference.data());
            data.resize(resampled_mic.size() + resampled_reference.size());
            for (size_t i = 0, j = 0; i < resampled_mic.size(); ++i, j += 2)
            {
                data[j] = resampled_mic[i];
                data[j + 1] = resampled_reference[i];
            }
        }
        else
        {
            auto resampled = std::vector<int16_t>(input_resampler_.GetOutputSamples(data.size()));
            input_resampler_.Process(data.data(), data.size(), resampled.data());
            data = std::move(resampled);
        }
    }
    else
    {
        data.resize(samples);
        if (!codec->InputData(data))
        {
            return false;
        }
    }

    // 音频调试：发送原始音频数据
    if (audio_debugger_)
    {
        audio_debugger_->Feed(data);
    }

    return true;
}

#ifdef CONFIG_USE_AUDIO_CODEC_ENCODE_OPUS
bool Application::ReadAudio(std::vector<uint8_t> &opus, int sample_rate, int samples)
{
    auto codec = Board::GetInstance().GetAudioCodec();
    if (!codec->input_enabled())
    {
        return false;
    }

    opus.resize(samples);
    if (!codec->InputData(opus))
    {
        return false;
    }

    return true;
}
#endif

void Application::WriteAudio(std::vector<int16_t> &data, int sample_rate)
{
    auto codec = Board::GetInstance().GetAudioCodec();
    // Resample if the sample rate is different
    if (sample_rate != codec->output_sample_rate())
    {
        int target_size = output_resampler_.GetOutputSamples(data.size());
        std::vector<int16_t> resampled(target_size);
        output_resampler_.Process(data.data(), data.size(), resampled.data());
        data = std::move(resampled);
    }
    codec->OutputData(data);
}

#ifdef CONFIG_USE_AUDIO_CODEC_DECODE_OPUS
void Application::WriteAudio(std::vector<uint8_t> &opus)
{
    auto codec = Board::GetInstance().GetAudioCodec();
    codec->OutputData(opus);
}
#endif

void Application::AbortSpeaking(AbortReason reason)
{
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    protocol_->SendAbortSpeaking(reason);
}

void Application::SetListeningMode(ListeningMode mode)
{
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

void Application::SetDeviceState(DeviceState state)
{
    if (device_state_ == state)
    {
        return;
    }

    clock_ticks_ = 0;
    auto previous_state = device_state_;
    device_state_ = state;
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);
    // The state is changed, wait for all background tasks to finish
    background_task_->WaitForCompletion();

    auto &board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    switch (state)
    {
    case kDeviceStateUnknown:
    case kDeviceStateIdle:
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        audio_processor_->Stop();
        wake_word_->StartDetection();
        break;
    case kDeviceStateConnecting:
        display->SetStatus(Lang::Strings::CONNECTING);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
        timestamp_queue_.clear();
        break;
    case kDeviceStateListening:
        display->SetStatus(Lang::Strings::LISTENING);
        display->SetEmotion("neutral");
        // Update the IoT states before sending the start listening command
#if CONFIG_IOT_PROTOCOL_XIAOZHI
        UpdateIotStates();
#endif

        // Make sure the audio processor is running
        if (!audio_processor_->IsRunning())
        {
            // Send the start listening command
            protocol_->SendStartListening(listening_mode_);
            if (previous_state == kDeviceStateSpeaking)
            {
                audio_decode_queue_.clear();
                audio_decode_cv_.notify_all();
                // FIXME: Wait for the speaker to empty the buffer
                vTaskDelay(pdMS_TO_TICKS(120));
            }
#ifdef CONFIG_USE_AUDIO_CODEC_ENCODE_OPUS
#else
            opus_encoder_->ResetState();
#endif
            audio_processor_->Start();
            wake_word_->StopDetection();
        }
        break;
    case kDeviceStateSpeaking:
        display->SetStatus(Lang::Strings::SPEAKING);

        if (listening_mode_ != kListeningModeRealtime)
        {
            audio_processor_->Stop();
            // Only AFE wake word can be detected in speaking mode
#if CONFIG_USE_AFE_WAKE_WORD
            wake_word_->StartDetection();
#else
            wake_word_->StopDetection();
#endif
        }
        ResetDecoder();
        break;
    default:
        // Do nothing
        break;
    }
}

void Application::ResetDecoder()
{
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef CONFIG_USE_AUDIO_CODEC_DECODE_OPUS
#else
    opus_decoder_->ResetState();
#endif
    audio_decode_queue_.clear();
    audio_decode_cv_.notify_all();
    last_output_time_ = std::chrono::steady_clock::now();
    auto codec = Board::GetInstance().GetAudioCodec();
    codec->EnableOutput(true);
}

void Application::SetDecodeSampleRate(int sample_rate, int frame_duration)
{
#ifdef CONFIG_USE_AUDIO_CODEC_DECODE_OPUS
    auto codec = Board::GetInstance().GetAudioCodec();
    codec->ConfigDecode(sample_rate, 1, frame_duration);
#else
    if (opus_decoder_->sample_rate() == sample_rate && opus_decoder_->duration_ms() == frame_duration)
    {
        return;
    }

    // 使用reset释放后重新分配可能存在分配失败问题
    // opus_decoder_.reset();
    // opus_decoder_ = std::make_unique<OpusDecoderWrapper>(sample_rate, 1, frame_duration);
    opus_decoder_->Config(sample_rate, 1, frame_duration);

    auto codec = Board::GetInstance().GetAudioCodec();
    if (opus_decoder_->sample_rate() != codec->output_sample_rate())
    {
        ESP_LOGI(TAG, "Resampling audio from %d to %d", opus_decoder_->sample_rate(), codec->output_sample_rate());
        output_resampler_.Configure(opus_decoder_->sample_rate(), codec->output_sample_rate());
    }
#endif
}

void Application::UpdateIotStates()
{
#if CONFIG_IOT_PROTOCOL_XIAOZHI
    auto &thing_manager = iot::ThingManager::GetInstance();
    std::string states;
    if (thing_manager.GetStatesJson(states, true))
    {
        protocol_->SendIotStates(states);
    }
#endif
}

void Application::Reboot()
{
    ESP_LOGI(TAG, "Rebooting...");
    esp_restart();
}

void Application::WakeWordInvoke(const std::string &wake_word)
{
    if (device_state_ == kDeviceStateIdle)
    {
        ToggleChatState();
        Schedule([this, wake_word]()
                 {
                if (protocol_) {
                    protocol_->SendWakeWordDetected(wake_word); 
                } });
    }
    else if (device_state_ == kDeviceStateSpeaking)
    {
        Schedule([this]()
                 { AbortSpeaking(kAbortReasonNone); });
    }
    else if (device_state_ == kDeviceStateListening)
    {
        Schedule([this]()
                 {
                if (protocol_) {
                    protocol_->CloseAudioChannel();
                } });
    }
}

bool Application::CanEnterSleepMode()
{
    if (device_state_ != kDeviceStateIdle)
    {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened())
    {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::SendMcpMessage(const std::string &payload)
{
    Schedule([this, payload]()
             {
            if (protocol_) {
                protocol_->SendMcpMessage(payload);
            } });
}

void Application::SetAecMode(AecMode mode)
{
    aec_mode_ = mode;
    Schedule([this]()
             {
            auto& board = Board::GetInstance();
            auto display = board.GetDisplay();
            switch (aec_mode_) {
            case kAecOff:
                audio_processor_->EnableDeviceAec(false);
                display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
                break;
            case kAecOnServerSide:
                audio_processor_->EnableDeviceAec(false);
                display->ShowNotification(Lang::Strings::RTC_MODE_ON);
                break;
            case kAecOnDeviceSide:
                audio_processor_->EnableDeviceAec(true);
                display->ShowNotification(Lang::Strings::RTC_MODE_ON);
                break;
            }

            // If the AEC mode is changed, close the audio channel
            if (protocol_ && protocol_->IsAudioChannelOpened()) {
                protocol_->CloseAudioChannel();
            } });
}

#if defined(CONFIG_VB6824_OTA_SUPPORT) && CONFIG_VB6824_OTA_SUPPORT == 1
void Application::ReleaseDecoder()
{
    ESP_LOGW(TAG, "Release decoder");
    while (!audio_decode_queue_.empty())
    {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    vTaskDelete(audio_loop_task_handle_);
    audio_loop_task_handle_ = nullptr;
    background_task_->WaitForCompletion();
    background_task_->WaitForCompletion();
    delete background_task_;
    background_task_ = nullptr;
    opus_decoder_.reset();
    ESP_LOGW(TAG, "Decoder released DONE");
}

void Application::ShowOtaInfo(const std::string &code, const std::string &ip)
{
    Schedule([this]()
             {
            if(device_state_ != kDeviceStateActivating && device_state_ != kDeviceStateIdle && protocol_ != nullptr) {
                protocol_->CloseAudioChannel();
            } });
    vTaskDelay(pdMS_TO_TICKS(600));
    if (device_state_ != kDeviceStateIdle)
    {
        ESP_LOGW(TAG, "ShowOtaInfo, device_state_:%s != kDeviceStateIdle", STATE_STRINGS[device_state_]);
        background_task_->Schedule([this, code, ip]()
                                   { this->ShowOtaInfo(code, ip); });
        return;
    }
    if (protocol_ != nullptr)
    {
        Schedule([this]()
                 {
                        protocol_.reset();
                        protocol_ = nullptr; });
        vTaskDelay(pdMS_TO_TICKS(100));
        background_task_->Schedule([this, code, ip]()
                                   { this->ShowOtaInfo(code, ip); });
        return;
    }

    ResetDecoder();
    ESP_LOGW(TAG, "DEV CODE:%s ip:%s", code.c_str(), ip.c_str());
    struct digit_sound
    {
        char digit;
        const std::string_view &sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{digit_sound{'0', Lang::Sounds::P3_0},
                                                           digit_sound{'1', Lang::Sounds::P3_1},
                                                           digit_sound{'2', Lang::Sounds::P3_2},
                                                           digit_sound{'3', Lang::Sounds::P3_3},
                                                           digit_sound{'4', Lang::Sounds::P3_4},
                                                           digit_sound{'5', Lang::Sounds::P3_5},
                                                           digit_sound{'6', Lang::Sounds::P3_6},
                                                           digit_sound{'7', Lang::Sounds::P3_7},
                                                           digit_sound{'8', Lang::Sounds::P3_8},
                                                           digit_sound{'9', Lang::Sounds::P3_9}}};

    Schedule([this, code, ip]()
             {
            auto display = Board::GetInstance().GetDisplay();
            std::string message;
            if (ip.empty()) {
                message = "浏览器访问\nhttp://vbota.esp32.cn/vbota\n设备码:"+code;
            } else {
                message = "浏览器访问\nhttp://vbota.esp32.cn/vbota\n或\nhttp://"+ip+"\n设备码:"+code;
            }
            
            display->SetStatus("升级模式");
            display->SetChatMessage("system", message.c_str());
            PlaySound(Lang::Sounds::P3_START_OTA);
            for (const auto& digit : code) {
                auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
                    [digit](const digit_sound& ds) { return ds.digit == digit; });
                if (it != digit_sounds.end()) {
                    PlaySound(it->sound);
                }
            } });
}
#endif

#if CONFIG_USE_EYE_STYLE_ES8311 || CONFIG_USE_EYE_STYLE_VB6824 // 如果开启魔眼显示
// 将眼睛的位置值从一个范围映射到另一个范围。具体来说，它将眼睛的位置值从逻辑上的位置范围（0 到 1023）映射到屏幕上的实际像素位置范围。
int Application::linear_map(int x, int in_min, int in_max, int out_min, int out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// 生成一个在 [min, max] 范围内的随机整数。它确保生成的随机数是均匀分布的，它主要用来生成随机的虹膜缩放值和眨眼时间间隔，从而模拟眼睛的自然虹膜缩放和眨眼行为
int Application::random_range(int min, int max)
{
    return min + esp_random() % (max - min + 1);
}

// 用于生成 0 到指定最大值之间的随机整数。它主要用来生成随机的时间间隔和位置偏移，以增加眼睛动画的随机性和自然性。
int Application::random_max(int max)
{
    return esp_random() % max;
}

/* 对眼睛进行绘制 */
void Application::drawEye(uint8_t e, uint32_t iScale, uint32_t scleraX, uint32_t scleraY, uint32_t uT, uint32_t lT)
{

    // uint32_t start_time = esp_timer_get_time();
    // ESP_LOGI(TAG, "drawEye start");

    uint8_t screenX;
    uint16_t p;
    uint32_t d;
    int16_t irisX, irisY;

    // 缓存scleraX的初始值
    uint32_t scleraXsave = scleraX;
    // 计算irisY的初始位置
    irisY = scleraY - (SCLERA_HEIGHT - IRIS_HEIGHT) / 2;

    // 定义一次处理的行数

    // 动态申请双缓冲的lineBuf，每个缓冲区为10行的大小
    uint16_t *lineBuf[2];
    lineBuf[0] = (uint16_t *)malloc(LINES_PER_BATCH * SCREEN_WIDTH * sizeof(uint16_t));
    lineBuf[1] = (uint16_t *)malloc(LINES_PER_BATCH * SCREEN_WIDTH * sizeof(uint16_t));
    if (lineBuf[0] == NULL || lineBuf[1] == NULL)
    {
        // 如果内存分配失败，释放已分配的缓冲区并返回
        if (lineBuf[0])
            free(lineBuf[0]);
        if (lineBuf[1])
            free(lineBuf[1]);
        return;
    }

    uint8_t bufIdx = 0;
    // 外循环，遍历屏幕的每一批行
    for (uint16_t screenY = 0; screenY < SCREEN_HEIGHT; screenY += LINES_PER_BATCH)
    {
        // 切换缓冲区
        uint16_t *currentBuf = lineBuf[bufIdx];
        bufIdx ^= 1; // 切换缓冲区索引
        // 计算本次批处理的实际行数（处理到屏幕底部时可能不足10行）
        uint8_t linesToProcess = (SCREEN_HEIGHT - screenY) < LINES_PER_BATCH ? (SCREEN_HEIGHT - screenY) : LINES_PER_BATCH;

        // 内循环，遍历批处理的每一行
        for (uint8_t line = 0; line < linesToProcess; line++, scleraY++, irisY++)
        {
            // 重置scleraX到初始值
            scleraX = scleraXsave;
            irisX = scleraX - (SCLERA_WIDTH - IRIS_WIDTH) / 2;
            // 遍历屏幕的每一列
            for (screenX = 0; screenX < SCREEN_WIDTH; screenX++, scleraX++, irisX++)
            {
                uint32_t screenIdx = (screenY + line) * SCREEN_WIDTH + screenX;
                uint32_t pixelIdx = line * SCREEN_WIDTH + screenX;

                // 判断像素点是否被遮挡
                if ((lower[screenIdx] <= lT) || (upper[screenIdx] <= uT))
                {
                    p = 0; // 被眼睑遮挡
                }
                else if ((irisY < 0) || (irisY >= IRIS_HEIGHT) || (irisX < 0) || (irisX >= IRIS_WIDTH))
                {
                    p = sclera[scleraY * SCLERA_WIDTH + scleraX]; // 在巩膜中
                }
                else
                {
                    p = polar[irisY * IRIS_WIDTH + irisX]; // 极角/距离
                    d = (iScale * (p & 0x7F)) / 240;
                    if (d < IRIS_MAP_HEIGHT)
                    {
                        uint16_t a = (IRIS_MAP_WIDTH * (p >> 7)) / 512;
                        p = iris[d * IRIS_MAP_WIDTH + a];
                    }
                    else
                    {
                        p = sclera[scleraY * SCLERA_WIDTH + scleraX];
                    }
                }
                // 将像素数据写入当前缓冲区
                currentBuf[pixelIdx] = (p >> 8) | (p << 8);
            }
        }
        // 批量绘制当前处理的行
        auto &board = Board::GetInstance();
        auto display = board.GetDisplay();
        display->SetEye(0, screenY, SCREEN_WIDTH, screenY + linesToProcess, currentBuf);
    }

    // 释放动态分配的缓冲区
    free(lineBuf[0]);
    free(lineBuf[1]);
    // uint32_t end_time = esp_timer_get_time();
    // ESP_LOGI(TAG, "drawEye end, time: %lu us", end_time - start_time);
}

/*
    动画函数
    眼球运动：通过随机生成目标位置和运动时间，模拟眼球的自然运动。使用缓动曲线ease实现平滑的运动效果。
    眨眼动画：通过随机触发眨眼事件，模拟眼睛的自然眨眼。使用状态机管理眨眼的开始、持续和结束。
    虹膜缩放：根据眼球位置和眨眼状态，计算虹膜的缩放比例，并通过drawEye函数绘制眼睛。
    */
void Application::frame(uint16_t iScale)
{
    // uint32_t start_time = esp_timer_get_time();
    // ESP_LOGI(TAG, "frame start");
    static uint8_t eyeIndex = 0;       // eye[] array counter //眼睛数组的索引
    int16_t eyeX, eyeY;                // 眼睛的位置
    uint32_t t = esp_timer_get_time(); // Time at start of function
    if (++eyeIndex >= NUM_EYES)        // 如果当前眼睛的索引已经到最后，回到第一个眼睛
    {
        eyeIndex = 0; // Cycle through eyes, 1 per call
    }
    // X/Y movement
    static bool eyeInMotion = false;
    static int16_t eyeOldX = 512, eyeOldY = 512;
    static uint32_t eyeMoveStartTime = 0L;
    static int32_t eyeMoveDuration = 0L;

    int32_t dt = t - eyeMoveStartTime; // uS elapsed since last eye event

    if (eyeInMotion)
    { // Currently moving?
        if (dt >= eyeMoveDuration)
        {                                         // Time up?  Destination reached.
            eyeInMotion = false;                  // Stop moving
            eyeMoveDuration = random_max(100000); // 0-3 sec stop  //眼睛变化的移动时间，降低至一秒
            eyeMoveStartTime = t;                 // Save initial time of stop
            eyeX = eyeOldX = eyeNewX;             // Save position //变化完成，将eyeNewX和eyeNewY赋值
            eyeY = eyeOldY = eyeNewY;             // 这里的eyeX和eyeY存放上一次变化的值
        }
        else
        { // Move time's not yet fully elapsed -- interpolate position
            // 根据移动时间来设置移动的偏移量，ease是缓动曲线数组，用来让移动更加平滑
            int16_t e = ease[255 * dt / eyeMoveDuration] + 1; // Ease curve   e计算出来的是ease数组的索引

            // 得到当前应该移动的偏移量。这样，每次更新时，位置会逐渐逼近目标位置，而缓动因子e控制了移动的速度变化，使得移动更加自然
            eyeX = eyeOldX + (((eyeNewX - eyeOldX) * e) / 256); // Interp X
            eyeY = eyeOldY + (((eyeNewY - eyeOldY) * e) / 256); // and Y
        }
    }
    else // 这里是生成新的移动位置的地方，眼珠停止，计算新的位置
    {    // Eye stopped
        eyeX = eyeOldX;
        eyeY = eyeOldY;

        if (dt > eyeMoveDuration)
        { // Time up?  Begin new move.
            int16_t dx, dy;
            uint32_t d;

            do
            { // Pick new dest in circle    //新的移动位置在0~1023之间
                // eyeNewX = random_max(1024); //需要变化的位置应该是在这里设置,这里注释掉,从外部获取
                // eyeNewY = random_max(1024);
                dx = (eyeNewX * 2) - 1023;
                dy = (eyeNewY * 2) - 1023;
            } while ((d = (dx * dx + dy * dy)) > (1023 * 1023)); // Keep trying

            eyeMoveDuration = random_range(72000, 144000); // ~1/14 - ~1/7 sec //变化的时间随机
            eyeMoveStartTime = t;                          // Save initial time of move
            eyeInMotion = true;                            // Start move on next frame
        }
    }

    // Blinking
    if (is_blink)
    {
        // Similar to the autonomous eye movement above -- blink start times
        // and durations are random_range (within ranges).
        if ((t - timeOfLastBlink) >= timeToNextBlink)
        { // Start new blink?
            timeOfLastBlink = t;
            uint32_t blinkDuration = random_range(36000, 72000); // ~1/28 - ~1/14 sec

            // Set up durations for both eyes (if not already winking)
            for (uint8_t e = 0; e < NUM_EYES; e++)
            {
                if (eye[e].blink.state == NOBLINK)
                {
                    eye[e].blink.state = ENBLINK;
                    eye[e].blink.startTime = t;
                    eye[e].blink.duration = blinkDuration;
                }
            }
            timeToNextBlink = blinkDuration * 3 + random_max(4000000);
        }
    }

    if (eye[eyeIndex].blink.state)
    { // Eye currently blinking?
        // Check if current blink state time has elapsed
        if ((t - eye[eyeIndex].blink.startTime) >= eye[eyeIndex].blink.duration)
        {
            // Yes -- increment blink state, unless...

            if (++eye[eyeIndex].blink.state > DEBLINK)
            {                                        // Deblinking finished?
                eye[eyeIndex].blink.state = NOBLINK; // No longer blinking
            }
            else
            {                                      // Advancing from ENBLINK to DEBLINK mode
                eye[eyeIndex].blink.duration *= 2; // DEBLINK is 1/2 ENBLINK speed
                eye[eyeIndex].blink.startTime = t;
            }
        }
    }

    // 将动作、眨眼和虹膜大小处理成可渲染的值
    //  Process motion, blinking and iris scale into renderable values
    // 运行 `python tablegen.py terminatorEye/sclera.png terminatorEye/iris.png terminatorEye/lid-upper-symmetrical.png terminatorEye/lid-lower-symmetrical.png terminatorEye/lid-upper.png terminatorEye/lid-lower.png` 并将输出重定向到 `terminatorEye.h` 文件。
    //  python tablegen.py terminatorEye/sclera.png terminatorEye/iris.png terminatorEye/lid-upper-symmetrical.png terminatorEye/lid-lower-symmetrical.png terminatorEye/lid-upper.png terminatorEye/lid-lower.png > terminatorEye.h
    //  Scale eye X/Y positions (0-1023) to pixel units used by drawEye()    //像素单位转换
    eyeX = linear_map(eyeX, 0, 1023, 0, SCLERA_WIDTH - DISPLAY_SIZE);
    eyeY = linear_map(eyeY, 0, 1023, 0, SCLERA_HEIGHT - DISPLAY_SIZE);
    // python tablegen.py doeEye/sclera.png doeEye/iris.png doeEye/lid-upper.png doeEye/lid-lower.png 160 > dragonEye.h
    // Horizontal position is offset so that eyes are very slightly crossed
    // to appear fixated (converged) at a conversational distance.  Number
    // here was extracted from my posterior and not mathematically based.
    // I suppose one could get all clever with a range sensor, but for now...
    if (NUM_EYES > 1)
    {
        if (eyeIndex == 1)
            eyeX += 4;
        else
            eyeX -= 4;
    }
    if (eyeX > (SCLERA_WIDTH - DISPLAY_SIZE))
        eyeX = (SCLERA_WIDTH - DISPLAY_SIZE);

    // Eyelids are rendered using a brightness threshold image.  This same
    // map can be used to simplify another problem: making the upper eyelid
    // track the pupil (eyes tend to open only as much as needed -- e.g. look
    // down and the upper eyelid drops).  Just sample a point in the upper
    // lid map slightly above the pupil to determine the rendering threshold.
    static uint8_t uThreshold = 240;
    uint8_t lThreshold = 0, n = 0;
    if (is_track)
    {
        int16_t sampleX = SCLERA_WIDTH / 2 - (eyeX / 2), // Reduce X influence
            sampleY = SCLERA_HEIGHT / 2 - (eyeY + IRIS_HEIGHT / 4);
        // Eyelid is slightly asymmetrical, so two readings are taken, averaged
        if (sampleY < 0)
            n = 0;
        else
            n = upper[sampleY * SCREEN_WIDTH + sampleX] +
                upper[sampleY * SCREEN_WIDTH + (SCREEN_WIDTH - 1 - sampleX)] /
                    2;
        uThreshold = (uThreshold * 3 + n) / 4; // Filter/soften motion
        // Lower eyelid doesn't track the same way, but seems to be pulled upward
        // by tension from the upper lid.
        lThreshold = 254 - uThreshold;
    }
    else
        uThreshold = lThreshold = 0; // No tracking -- eyelids full open unless blink modifies them

    // The upper/lower thresholds are then scaled relative to the current
    // blink position so that b links work together with pupil tracking.
    if (eye[eyeIndex].blink.state)
    { // Eye currently blinking?
        uint32_t s = (t - eye[eyeIndex].blink.startTime);

        if (s >= eye[eyeIndex].blink.duration)
        {
            s = 255; // At or past blink end
        }
        else
        {
            s = 255 * s / eye[eyeIndex].blink.duration; // Mid-blink
        }

        s = (eye[eyeIndex].blink.state == DEBLINK) ? 1 + s : 256 - s;

        n = (uThreshold * s + 254 * (257 - s)) / 256;
        lThreshold = (lThreshold * s + 254 * (257 - s)) / 256;
    }
    else
    {
        n = uThreshold;
    }
    // uint32_t end_time = esp_timer_get_time();
    // ESP_LOGI(TAG, "frame end, time: %lu us", end_time - start_time);

    // Pass all the derived values to the eye-rendering function:
    drawEye(eyeIndex, iScale, eyeX, eyeY, n, lThreshold);
}

// 虹膜缩放动画：通过递归函数split生成虹膜缩放动画，模拟瞳孔对光线的反应。使用时间插值实现平滑的缩放效果。
void Application::split(
    int16_t startValue, // 虹膜缩放的起始值
    int16_t endValue,   // 虹膜缩放的结束值
    uint64_t startTime, // 开始时间（使用`esp_timer_get_time()`获取）
    int32_t duration,   // 动画持续时间（微秒）
    int16_t range)
{ // 允许的缩放值变化范围
    if (range >= 8)
    {               // 限制递归深度
        range /= 2; // 将范围和时间分成两半
        duration /= 2;
        int16_t midValue = (startValue + endValue - range) / 2 + (esp_random() % range);
        uint64_t midTime = startTime + duration;
        split(startValue, midValue, startTime, duration, range); // 第一部分
        split(midValue, endValue, midTime, duration, range);     // 第二部分
    }
    else
    {               // No more subdivisions, do iris motion...
        int32_t dt; // Time since start of motion
        int16_t v;  // Interim value
        // uint32_t start_time = esp_timer_get_time();
        // ESP_LOGI(TAG, "split start");
        while ((dt = (esp_timer_get_time() - startTime)) < duration)
        { // 使用 esp_timer_get_time() 获取当前时间，并计算与 startTime 的时间差 dt。
            // 计算当前值
            v = startValue + (((endValue - startValue) * dt) / duration); // 根据时间差 dt 和总时间 duration，计算当前虹膜的缩放值 v
            if (v < IRIS_MIN)
                v = IRIS_MIN; // Clip just in case    确保 v 不会超出预定义的虹膜大小范围（IRIS_MIN 和 IRIS_MAX）。
            else if (v > IRIS_MAX)
                v = IRIS_MAX;
            frame(v); // Draw frame with interim iris scale value   调用 frame(v) 函数，使用计算出的 v 值绘制当前帧。
            // 分段延时，允许任务切换
        }
        //  uint32_t end_time = esp_timer_get_time();
        // ESP_LOGI(TAG, "split end, time: %lu us", end_time - start_time);
    }
}

void Application::eye_style(uint8_t eye_style)
{
    // is_track = rand() % 2;
    is_track = true;
    switch (eye_style)
    {
    case 1:
        iris = iris_default;
        sclera = sclera_default;
        break;
    case 2:
        iris = iris_style_blood;
        sclera = sclera_style_white;
        break;
    case 3:
        iris = iris_style_cospa1;
        sclera = sclera_style_cute_girl;
        break;
    case 4:
        iris = iris_style_spikes;
        sclera = sclera_style_white;
        break;
    case 5:
        iris = iris_style_ribbon;
        sclera = sclera_style_ocean_girl;
        break;
    case 6:
        iris = iris_style_black_star;
        sclera = sclera_style_zhuozhu;
        break;
    case 7:
        iris = iris_style_straw;
        sclera = sclera_style_lufei;
        break;
    default:
        break;
    }
}

void Application::EyeLoop()
{
    uint8_t e;                        // Eye index, 0 to NUM_EYES-1
    startTime = esp_timer_get_time(); // For frame-rate calculation
    for (e = 0; e < NUM_EYES; e++)
    {
        eye[e].blink.state = NOBLINK;
        // If project involves only ONE eye and NO other SPI devices, its
        // select line can be permanently tied to GND and corresponding pin
        // in config.h set to -1.  Best to use it though.
    }

    while (true)
    {
        ESP_LOGI(TAG, "EYE_Task...");
        newIris = random_range(IRIS_MIN, IRIS_MAX);                                   //
        split(oldIris, newIris, esp_timer_get_time(), 5000000L, IRIS_MAX - IRIS_MIN); //

        vTaskDelay(10 / portTICK_PERIOD_MS); // 确保任务不卡住
    }
}

#endif

void Application::PushMuteAudio(int mute_time)
{
    for (size_t i = 0; i < (mute_time / 60); i++)
    {
        uint8_t opus[] = {0x5b, 0x41, 0x61, 0x01, 0xb8, 0xde, 0x72, 0x36, 0x92, 0x1f, 0x05, 0xfa, 0x47, 0x8d, 0x69, 0x2c, 0x8f, 0x20, 0x0a, 0xfb, 0xfb, 0x2b, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        AudioStreamPacket packet;
        packet.sample_rate = 16000;
        packet.frame_duration = 60;
        packet.payload.resize(sizeof(opus));
        memcpy(packet.payload.data(), (uint8_t *)opus, sizeof(opus));
        std::lock_guard<std::mutex> lock(mutex_);
        audio_decode_queue_.emplace_back(std::move(packet));
    }
}

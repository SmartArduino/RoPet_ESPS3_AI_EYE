#include "wifi_board.h"
#include "audio_codecs/vb6824_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "iot/thing_manager.h"
#include "led/single_led.h"
#include "power_save_timer.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>
#include <esp_lcd_gc9a01.h>
#include "mcp_server.h"
#if CONFIG_USE_EYE_STYLE_ES8311
#include "touch_button.h"
#endif
#include "assets/lang_config.h"
#include <ssid_manager.h>

#define TAG "CompactWifiBoardLCD"

#if CONFIG_LCD_GC9A01_240X240
LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);
#elif CONFIG_LCD_GC9A01_160X160
LV_FONT_DECLARE(font_puhui_14_1);
LV_FONT_DECLARE(font_awesome_14_1);
#endif

class CompactWifiBoardLCD : public WifiBoard
{
private:
    esp_lcd_panel_io_handle_t lcd_io = NULL;
    esp_lcd_panel_handle_t lcd_panel = NULL;
    Button boot_button_;
    LcdDisplay *display_;
    VbAduioCodec audio_codec;
    PowerSaveTimer *power_save_timer_;
    int64_t boot_time_us_; // 开机时间（微秒），用于按钮保护期
    bool first_idle_auto_wake_done_ = false; // 标记是否已经完成首次自动唤醒

    void InitializePowerSaveTimer()
    {
        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]()
                                            {
                                                ESP_LOGI(TAG, "Enabling sleep mode");
                                                auto display = GetDisplay();
                                                display->SetChatMessage("system", "");
                                                display->SetEmotion("sleepy");
#if CONFIG_LCD_GC9A01_160X160
                                                GetBacklight()->RestoreBrightness();
#endif
                                                // gpio_set_level(SLEEP_GOIO, 0);
                                            });
        power_save_timer_->OnExitSleepMode([this]()
                                           {
                                               auto display = GetDisplay();
                                               display->SetChatMessage("system", "");
                                               display->SetEmotion("neutral");
#if CONFIG_LCD_GC9A01_160X160
                                               GetBacklight()->RestoreBrightness();
#endif
                                               // gpio_set_level(SLEEP_GOIO, 1);
                                           });
        power_save_timer_->OnShutdownRequest([this]()
                                             {
                                                 // pmic_->PowerOff();
                                                 // gpio_set_level(SLEEP_GOIO, 0);
                                                 //  ESP_LOGI(TAG,"Not used for a long time. Shut down. Press and hold to turn on!");
                                                 //  gpio_set_level(SLEEP_GOIO, 0);
                                             });
        power_save_timer_->SetEnabled(true);
    }

    // 首次进入idle状态时自动唤醒智能体
    void CheckFirstIdleAutoWake() {
        if (!first_idle_auto_wake_done_) {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateIdle) {
                first_idle_auto_wake_done_ = true;
                ESP_LOGI(TAG, "首次进入idle状态，自动唤醒智能体");
                
                // 延迟2秒后自动唤醒，确保系统完全稳定
                app.Schedule([this]() {
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    Application::GetInstance().WakeWordInvoke("你好小智");
                });
            }
        }
    }

    // 初始化按钮
    void InitializeButtons()
    {
        // 当boot_button_被点击时，执行以下操作
        boot_button_.OnClick([this]()
                             {
            // 检查是否在开机保护期内（5秒）
            int64_t current_time = esp_timer_get_time();
            if (current_time - boot_time_us_ < 5000000) { // 5秒 = 5,000,000微秒
                ESP_LOGI(TAG, "按钮保护期内，忽略点击事件");
                return;
            }
            
            // 获取应用程序实例
            auto& app = Application::GetInstance();
            // 如果设备状态为kDeviceStateStarting且WifiStation未连接，则重置Wifi配置
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            // 切换聊天状态
            app.ToggleChatState(); });

        boot_button_.OnPressRepeat([this](uint16_t count)
                                   {
            if(count >= 3){
                ESP_LOGI(TAG, "重新配网");
                ResetWifiConfiguration();
            } });
#if (defined(CONFIG_VB6824_OTA_SUPPORT) && CONFIG_VB6824_OTA_SUPPORT == 1)
        boot_button_.OnDoubleClick([this]()
                                   {
            // OTA功能也有类似的时间保护（20秒内有效）
            if (esp_timer_get_time() > 20 * 1000 * 1000) {
                ESP_LOGI(TAG, "超过20秒，不进入OTA模式 %ld", (uint32_t)esp_timer_get_time());
                return;
            }
            audio_codec.OtaStart(0); });
#endif
        // boot_button_.OnDoubleClick([this]() {
        //     auto& app = Application::GetInstance();
        //     app.eye_style_num = (app.eye_style_num+1) % 8;
        //     app.eye_style(app.eye_style_num);
        // });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot()
    {
        auto &thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Screen"));
        // thing_manager.AddThing(iot::CreateThing("Lamp"));
    }

    // GC9A01-SPI2初始化-用于显示小智
    void InitializeSpiEye1()
    {
        const spi_bus_config_t buscfg = {
            .mosi_io_num = GC9A01_SPI1_LCD_GPIO_MOSI,
            .miso_io_num = GPIO_NUM_NC,
            .sclk_io_num = GC9A01_SPI1_LCD_GPIO_SCLK,
            .quadwp_io_num = GPIO_NUM_NC,
            .quadhd_io_num = GPIO_NUM_NC,
            .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t), // 增大传输大小,
        };
        ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI1_NUM, &buscfg, SPI_DMA_CH_AUTO));
    }

    // GC9A01-SPI2初始化-用于显示魔眼
    void InitializeGc9a01DisplayEye1()
    {
        ESP_LOGI(TAG, "Init GC9A01 display1");

        ESP_LOGI(TAG, "Install panel IO1");
        ESP_LOGD(TAG, "Install panel IO1");
        const esp_lcd_panel_io_spi_config_t io_config = {
            .cs_gpio_num = GC9A01_SPI1_LCD_GPIO_CS,
            .dc_gpio_num = GC9A01_SPI1_LCD_GPIO_DC,
            .spi_mode = 0,
            .pclk_hz = GC9A01_LCD_PIXEL_CLK_HZ,
            .trans_queue_depth = 10,
            .lcd_cmd_bits = GC9A01_LCD_CMD_BITS,
            .lcd_param_bits = GC9A01_LCD_PARAM_BITS,

        };
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)DISPLAY_SPI1_NUM, &io_config, &lcd_io);

        ESP_LOGD(TAG, "Install LCD1 driver");
        esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = GC9A01_SPI1_LCD_GPIO_RST,
            .color_space = GC9A01_LCD_COLOR_SPACE,
            .bits_per_pixel = GC9A01_LCD_BITS_PER_PIXEL,

        };
        panel_config.rgb_endian = DISPLAY_RGB_ORDER;
        esp_lcd_new_panel_gc9a01(lcd_io, &panel_config, &lcd_panel);

        esp_lcd_panel_reset(lcd_panel);
        esp_lcd_panel_init(lcd_panel);
        esp_lcd_panel_invert_color(lcd_panel, DISPLAY_COLOR_INVERT);
        esp_lcd_panel_disp_on_off(lcd_panel, true);
        display_ = new SpiLcdDisplay(lcd_io, lcd_panel,
                                     DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                     {
#if CONFIG_LCD_GC9A01_240X240
                                         .text_font = &font_puhui_20_4,
                                         .icon_font = &font_awesome_20_4,
#elif CONFIG_LCD_GC9A01_160X160
                                         .text_font = &font_puhui_14_1,
                                         .icon_font = &font_awesome_14_1,
#endif
                                         .emoji_font = font_emoji_64_init(),
                                     });
    }

    virtual void StartNetwork() override
    {

        // User can press BOOT button while starting to enter WiFi configuration mode
        if (wifi_config_mode_)
        {
            EnterWifiConfigMode();
            return;
        }

        // If no WiFi SSID is configured, enter WiFi configuration mode
        auto &ssid_manager = SsidManager::GetInstance();
        auto ssid_list = ssid_manager.GetSsidList();
        if (ssid_list.empty())
        {
            wifi_config_mode_ = true;
            EnterWifiConfigMode();
            return;
        }

        auto &wifi_station = WifiStation::GetInstance();
        wifi_station.OnScanBegin([this]()
                                 {
        auto display = Board::GetInstance().GetDisplay();
        display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000); });
        wifi_station.OnConnect([this](const std::string &ssid)
                               {
        auto display = Board::GetInstance().GetDisplay();
        std::string notification = Lang::Strings::CONNECT_TO;
        notification += ssid;
        notification += "...";
        display->ShowNotification(notification.c_str(), 30000); });
        wifi_station.OnConnected([this](const std::string &ssid)
                                 {
                                     auto display = Board::GetInstance().GetDisplay();
                                     std::string notification = Lang::Strings::CONNECTED_TO;
                                     notification += ssid;
                                     display->ShowNotification(notification.c_str(), 30000); });
        wifi_station.Start();

        // Try to connect to WiFi, if failed, launch the WiFi configuration AP
        if (!wifi_station.WaitForConnected(60 * 1000))
        {
            wifi_station.Stop();
            wifi_config_mode_ = true;
            EnterWifiConfigMode();
            return;
        }
    }

public:
    CompactWifiBoardLCD() : boot_button_(BOOT_BUTTON_GPIO), audio_codec(CODEC_RX_GPIO, CODEC_TX_GPIO)
    {
        // 记录开机时间
        boot_time_us_ = esp_timer_get_time();
        ESP_LOGI(TAG, "开机时间记录: %lld微秒", boot_time_us_);

// 如果定义了CONFIG_LCD_GC9A01_160X160，则配置GPIO引脚
#if CONFIG_LCD_GC9A01_160X160
        gpio_config_t bk_gpio_config = {
            .pin_bit_mask = 1ULL << GC9A01_SPI1_LCD_GPIO_BL,
            .mode = GPIO_MODE_OUTPUT,
        };
        // 配置GPIO引脚
        ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
        // 设置GPIO引脚电平为高
        ESP_ERROR_CHECK(gpio_set_level(GC9A01_SPI1_LCD_GPIO_BL, 1));
#endif

        // 初始化SPI眼
        InitializeSpiEye1();
        // 初始化GC9A01显示屏
        InitializeGc9a01DisplayEye1();
        // 初始化按钮
        InitializeButtons();
        // 初始化物联网
        InitializeIot();

        // 设置SLEEP_GOIO引脚为上拉输入模式
        gpio_set_pull_mode(SLEEP_GOIO, GPIO_PULLUP_ONLY);
        // 设置SLEEP_GOIO引脚为输出模式
        gpio_set_direction(SLEEP_GOIO, GPIO_MODE_OUTPUT);
        // 设置SLEEP_GOIO引脚电平为高
        gpio_set_level(SLEEP_GOIO, 1);

        // 初始化省电定时器
        InitializePowerSaveTimer();

        // 设置音频编解码器唤醒回调函数
        audio_codec.OnWakeUp([this](const std::string &command)
                             {
            // 如果唤醒词为vb6824_get_wakeup_word()，则唤醒设备
            if (command == std::string(vb6824_get_wakeup_word())){
                if(Application::GetInstance().GetDeviceState() != kDeviceStateListening){
                    Application::GetInstance().WakeWordInvoke("你好小智");
                }
            // 如果唤醒词为"开始配网"，则重置WiFi配置
            }else if (command == "开始配网"){
                ResetWifiConfiguration();
            } });
        audio_codec.SetOutputVolume(100);
        
        // 启动定时检查任务，监控设备状态变化以实现首次自动唤醒
        xTaskCreate([](void* param) {
            auto* self = static_cast<CompactWifiBoardLCD*>(param);
            while (true) {
                self->CheckFirstIdleAutoWake();
                vTaskDelay(pdMS_TO_TICKS(1000)); // 每秒检查一次
                
                // 如果已经完成首次自动唤醒，退出任务
                if (self->first_idle_auto_wake_done_) {
                    break;
                }
            }
            vTaskDelete(NULL);
        }, "auto_wake_monitor", 2048, this, 1, NULL);
    }

    virtual Led *GetLed() override
    {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec *GetAudioCodec() override
    {
        return &audio_codec;
    }

    virtual Display *GetDisplay() override
    {
        return display_;
    }

#if CONFIG_LCD_GC9A01_160X160
    // 获取背光对象
    virtual Backlight *GetBacklight() override
    {
        if (GC9A01_SPI1_LCD_GPIO_BL != GPIO_NUM_NC)
        {
            static PwmBacklight backlight(GC9A01_SPI1_LCD_GPIO_BL, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }
#endif
};

    DECLARE_BOARD(CompactWifiBoardLCD);
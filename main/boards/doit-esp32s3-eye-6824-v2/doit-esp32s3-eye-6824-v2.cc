
#include "wifi_board.h"
#include "audio_codecs/vb6824_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"
// #include "power_save_timer.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>

#include "mcp_server.h"
#include "assets/lang_config.h"
#include "anim_eye_display.h"
#include "mcp_server.h"
#include "lcd_cmd.h"

#include "doit_ble.h"
#include "doit_file.h"


#define TAG "CompactWifiBoardLCD"

#if CONFIG_LCD_ST77916_360X360 || CONFIG_LCD_ST7796_240X240 || CONFIG_LCD_GC9A01_240X240
LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);
#elif CONFIG_LCD_GC9A01_160X160
LV_FONT_DECLARE(font_puhui_14_1);
LV_FONT_DECLARE(font_awesome_14_1);
#endif

class CompactWifiBoardLCD : public WifiBoard
{
private:
    // i2c_master_bus_handle_t sc7a20h_bus_handle = NULL;
    // i2c_master_dev_handle_t sc7a20h_dev_handle = NULL;
    esp_lcd_panel_io_handle_t panel_io = NULL;
    esp_lcd_panel_handle_t panel = NULL;
    Button boot_button_;
    LcdDisplay *display_;
    VbAduioCodec audio_codec;
    // PowerSaveTimer *power_save_timer_;

    //     void InitializePowerSaveTimer()
    //     {
    //         power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
    //         power_save_timer_->OnEnterSleepMode([this]()
    //                                             {
    //                                                 ESP_LOGI(TAG, "Enabling sleep mode");
    //                                                 auto display = GetDisplay();
    //                                                 display->SetChatMessage("system", "");
    //                                                 display->SetEmotion("sleepy");
    // #if CONFIG_LCD_GC9A01_160X160
    //                                                 GetBacklight()->RestoreBrightness();
    // #endif
    //                                                 // gpio_set_level(SLEEP_GOIO, 0);
    //                                             });
    //         power_save_timer_->OnExitSleepMode([this]()
    //                                            {
    //                                                auto display = GetDisplay();
    //                                                display->SetChatMessage("system", "");
    //                                                display->SetEmotion("neutral");
    // #if CONFIG_LCD_GC9A01_160X160
    //                                                GetBacklight()->RestoreBrightness();
    // #endif
    //                                                // gpio_set_level(SLEEP_GOIO, 1);
    //                                            });
    //         power_save_timer_->OnShutdownRequest([this]()
    //                                              {
    //                                                  // pmic_->PowerOff();
    //                                                  // gpio_set_level(SLEEP_GOIO, 0);
    //                                                  //  ESP_LOGI(TAG,"Not used for a long time. Shut down. Press and hold to turn on!");
    //                                                  //  gpio_set_level(SLEEP_GOIO, 0);
    //                                              });
    //         power_save_timer_->SetEnabled(true);
    //     }

    // 初始化按钮
    void InitializeButtons()
    {
        // 当boot_button_被点击时，执行以下操作
        boot_button_.OnClick([this]()
                             {
                                 ESP_LOGI(TAG, "Boot button clicked");
                                 if(min_program_in_ota_mode()){
                                    ESP_LOGI(TAG, "In PSD Ota Mode, restart system");
                                    esp_restart();
                                 }
                                 // 获取应用程序实例
                                 auto &app = Application::GetInstance();
                                 app.ToggleChatState(); });

#if (defined(CONFIG_VB6824_OTA_SUPPORT) && CONFIG_VB6824_OTA_SUPPORT == 1)
        boot_button_.OnDoubleClick([this]()
                                   {

    // if (esp_timer_get_time() > 20 * 1000 * 1000){
                ESP_LOGI(TAG, "Long press, do not enter OTA mode %ld", (uint32_t)esp_timer_get_time());
#if CONFIG_USE_PSD_MULTIPLE
        
            doit_file_psd_multi_process(true);
#endif
                return;
//         }else{
// audio_codec.OtaStart(0);
//         } 
    });
#endif

        boot_button_.OnPressRepeaDone([this](uint16_t count)
                                      {
#if CONFIG_SUPPORT_MINI_PROGRAMS_REPLACE_PSD
                                          if (count >= 5)
                                          {
                                             auto &app = Application::GetInstance();
                                            if (esp_timer_get_time() > 20 * 1000 * 1000 && app.GetDeviceState() == kDeviceStateIdle && WifiStation::GetInstance().IsConnected() && !min_program_in_ota_mode()){
                ESP_LOGI(TAG, "five press repeadone, do not enter PSD mode %ld", (uint32_t)esp_timer_get_time());
                return;
        }
                                                     ESP_LOGI(TAG, "素材替换模式");
    
                                                Application::GetInstance().Schedule([]{
Application::GetInstance().ResetDecoder();
                                                  Application::GetInstance().PlaySound(Lang::Sounds::P3_SUCAI);
                                                                                 vTaskDelay(2000);                min_program_ble_start();
                                                });
                                             
                                              
                                          } else
#endif
                                         if (count >= 3)
                                          {
                                              ESP_LOGI(TAG, "重新配网");
                                              ResetWifiConfiguration();
                                          } });
    }

    void InitializeSpi()
    {
        ESP_LOGI(TAG, "Initialize QSPI bus");
#if CONFIG_LCD_ST77916_360X360
        const spi_bus_config_t bus_config = {
            .data0_io_num = QSPI_PIN_NUM_LCD_DATA0,
            .data1_io_num = QSPI_PIN_NUM_LCD_DATA1,
            .sclk_io_num = QSPI_PIN_NUM_LCD_PCLK,
            .data2_io_num = QSPI_PIN_NUM_LCD_DATA2,
            .data3_io_num = QSPI_PIN_NUM_LCD_DATA3,
            .max_transfer_sz = DISPLAY_WIDTH * 80 * sizeof(uint16_t),
        };
        ESP_ERROR_CHECK(
            spi_bus_initialize(QSPI_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));
#elif CONFIG_LCD_ST7796_240X240 || CONFIG_LCD_GC9A01_240X240 || CONFIG_LCD_GC9A01_160X160
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = SPI_LCD_GPIO_MOSI;
        buscfg.miso_io_num = SPI_LCD_GPIO_MISO;
        buscfg.sclk_io_num = SPI_LCD_GPIO_SCLK;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));
#endif
    }

    void InitializeDisplay()
    {
#if CONFIG_LCD_ST77916_360X360
        const esp_lcd_panel_io_spi_config_t io_config = ST77916_PANEL_IO_QSPI_CONFIG(QSPI_PIN_NUM_LCD_CS, NULL, NULL);
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
            (esp_lcd_spi_bus_handle_t)QSPI_LCD_HOST, &io_config, &panel_io));

        st77916_vendor_config_t vendor_config = {
            .flags =
                {
                    .use_qspi_interface = 1,
                },
        };
        vendor_config.init_cmds = vendor_specific_init_new;
        vendor_config.init_cmds_size =
            sizeof(vendor_specific_init_new) / sizeof(st77916_lcd_init_cmd_t);

        ESP_LOGD(TAG, "Install LCD driver");
        const esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = QSPI_PIN_NUM_LCD_RST, // Shared with Touch reset
            .rgb_ele_order = DISPLAY_RGB_ORDER,
            .bits_per_pixel = 16,
            .vendor_config = &vendor_config,
        };
        esp_lcd_new_panel_st77916(panel_io, &panel_config, &panel);

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_mirror(panel, false, false);
        esp_lcd_panel_disp_on_off(panel, true);
        esp_lcd_panel_init(panel);
#if  CONFIG_SUPPORT_MINI_PROGRAMS_REPLACE_PSD
        display_ = new AnimEyeDisplay(panel_io, panel,
                                      DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X,
                                      DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                      {
                                          .text_font = &font_puhui_20_4,
                                          .icon_font = &font_awesome_20_4,
                                          .emoji_font = font_emoji_64_init(),
                                      });
#else
        display_ = new SpiLcdDisplay(panel_io, panel,
                                     DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X,
                                     DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                     {
                                         .text_font = &ont_puhui_20_4,
                                         .icon_font = &font_awesome_20_4,
                                         .emoji_font = font_emoji_64_init(),
                                     });
#endif
        gpio_config_t bk_gpio_config = {
            .pin_bit_mask = 1ULL << QSPI_PIN_NUM_LCD_BL,
            .mode = GPIO_MODE_OUTPUT,
        };
        // 配置GPIO引脚
        ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
        // 设置GPIO引脚电平为高
        ESP_ERROR_CHECK(gpio_set_level(QSPI_PIN_NUM_LCD_BL, QSPI_DISPLAY_BACKLIGHT_OUTPUT_INVERT));
#elif CONFIG_LCD_ST7796_240X240 || CONFIG_LCD_GC9A01_240X240 || CONFIG_LCD_GC9A01_160X160
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = SPI_LCD_GPIO_CS;
        io_config.dc_gpio_num = SPI_LCD_GPIO_DC;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI_LCD_HOST, &io_config, &panel_io));

        ESP_LOGD(TAG, "Install LCD driver");

#if CONFIG_LCD_ST7796_240X240
        st7796_vendor_config_t st7796_vendor_config = {
            .init_cmds = vendor_specific_init_new,
            .init_cmds_size = sizeof(vendor_specific_init_new) / sizeof(st7796_lcd_init_cmd_t),
        };
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = SPI_LCD_GPIO_RST;
        panel_config.color_space = ESP_LCD_COLOR_SPACE_RGB;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        panel_config.vendor_config = &st7796_vendor_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(panel_io, &panel_config, &panel));
#elif CONFIG_LCD_GC9A01_240X240
        esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = SPI_LCD_GPIO_RST,
            .color_space = ESP_LCD_COLOR_SPACE_RGB,
            .bits_per_pixel = 16};
        panel_config.rgb_endian = DISPLAY_RGB_ORDER;
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
#elif CONFIG_LCD_GC9A01_160X160
        esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = SPI_LCD_GPIO_RST,
            .color_space = ESP_LCD_COLOR_SPACE_RGB,
            .bits_per_pixel = 16};
        panel_config.rgb_endian = DISPLAY_RGB_ORDER;
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = vendor_specific_init_new,
            .init_cmds_size = sizeof(vendor_specific_init_new) / sizeof(gc9a01_lcd_init_cmd_t),
        };
        panel_config.vendor_config = &gc9107_vendor_config;
        esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel);
#endif
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, DISPLAY_COLOR_INVERT));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

#if CONFIG_SUPPORT_MINI_PROGRAMS_REPLACE_PSD
        display_ = new AnimEyeDisplay(panel_io, panel,
                                      DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X,
                                      DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                      {
#if CONFIG_LCD_GC9A01_240X240 || CONFIG_LCD_ST7796_240X240
                                          .text_font = &font_puhui_20_4,
                                          .icon_font = &font_awesome_20_4,
#elif CONFIG_LCD_GC9A01_160X160
                                          .text_font = &font_puhui_14_1,
                                          .icon_font = &font_awesome_14_1,
#endif
                                          .emoji_font = font_emoji_64_init()});
#else
        display_ = new SpiLcdDisplay(panel_io, panel,
                                     DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X,
                                     DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                     {
#if CONFIG_LCD_GC9A01_240X240 || CONFIG_LCD_ST7796_240X240
                                         .text_font = &font_puhui_20_4,
                                         .icon_font = &font_awesome_20_4,
#elif CONFIG_LCD_GC9A01_160X160
                                         .text_font = &font_puhui_14_1,
                                         .icon_font = &font_awesome_14_1,
#endif
                                         .emoji_font = font_emoji_64_init()});

#endif
        gpio_config_t config;
        config.pin_bit_mask = BIT64(SPI_LCD_BL);
        config.mode = GPIO_MODE_OUTPUT;
        config.pull_up_en = GPIO_PULLUP_DISABLE;
        config.pull_down_en = GPIO_PULLDOWN_ENABLE;
        config.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&config);
        gpio_set_level(SPI_LCD_BL, SPI_DISPLAY_BACKLIGHT_OUTPUT_INVERT);
#endif
#if CONFIG_LCD_GC9A01_160X160 || CONFIG_LCD_ST7796_240X240 || CONFIG_LCD_ST77916_360X360
        GetBacklight()->SetBrightness(100);
#endif
    }

    void InitializeTools()
    {
        auto &mcp_server = McpServer::GetInstance();
    }


public:
    CompactWifiBoardLCD() : boot_button_(BOOT_BUTTON_GPIO), audio_codec(CODEC_RX_GPIO, CODEC_TX_GPIO)
    {
        // 设置SLEEP_GOIO引脚为上拉输入模式
        gpio_set_pull_mode(SLEEP_GOIO, GPIO_PULLUP_ONLY);
        // 设置SLEEP_GOIO引脚为输出模式
        gpio_set_direction(SLEEP_GOIO, GPIO_MODE_OUTPUT);
        // 设置SLEEP_GOIO引脚电平为高
        gpio_set_level(SLEEP_GOIO, 1);
        // 如果定义了CONFIG_LCD_GC9A01_160X160，则配置GPIO引脚
        // 初始化SPI
        InitializeSpi();
        InitializeDisplay();
        // 初始化按钮
        InitializeButtons();

        // InitializePowerSaveTimer();

        // 设置音频编解码器唤醒回调函数
        audio_codec.OnWakeUp([this](const std::string &command)
                             {
            // 如果唤醒词为vb6824_get_wakeup_word()，则唤醒设备
            if (command == std::string(vb6824_get_wakeup_word())){
                if(Application::GetInstance().GetDeviceState() != kDeviceStateListening && !min_program_in_ota_mode()){
                    ESP_LOGI(TAG, "Wake word detected: %s", command.c_str());
                    ESP_LOGI(TAG,"min_program_in_ota_mode=%d",min_program_in_ota_mode());
                    Application::GetInstance().WakeWordInvoke("你好小智");
                }
            // 如果唤醒词为"开始配网"，则重置WiFi配置
            }else if (command == "开始配网"){
                ResetWifiConfiguration();
            } });
        audio_codec.SetOutputVolume(100);

        // InitializeSC7A20H();

        // motor_init(TEMP_EN);
        // touch_button_init();
    }

    virtual Led *GetLed() override
    {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    /**
     * 获取音频编解码器的虚函数实现
     * 这是一个重写（override）的虚函数，用于返回当前对象的音频编解码器实例
     * @return 返回一个指向AudioCodec类型对象的指针，具体为audio_codec成员变量
     */
    virtual AudioCodec *GetAudioCodec() override
    {
        return &audio_codec; // 返回audio_codec成员变量的地址
    }

    virtual Display *GetDisplay() override
    {
        return display_;
    }

#if CONFIG_LCD_ST77916_360X360
    // 获取背光对象
    virtual Backlight *GetBacklight() override
    {
        if (QSPI_PIN_NUM_LCD_BL != GPIO_NUM_NC)
        {
            static PwmBacklight backlight(QSPI_PIN_NUM_LCD_BL, QSPI_DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }
#elif CONFIG_LCD_ST7796_240X240 || CONFIG_LCD_GC9A01_160X160
    // 获取背光对象
    virtual Backlight *GetBacklight() override
    {
        if (SPI_LCD_BL != GPIO_NUM_NC)
        {
            static PwmBacklight backlight(SPI_LCD_BL, SPI_DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }
#endif
};

DECLARE_BOARD(CompactWifiBoardLCD);

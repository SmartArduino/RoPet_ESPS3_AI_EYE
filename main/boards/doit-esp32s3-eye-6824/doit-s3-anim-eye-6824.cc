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
#include "assets/lang_config.h"
#include <ssid_manager.h>
#include "mcp_server.h"

#include "doit_ble.h"
#include "doit_file.h"
#include "anim_eye_display.h"


#define TAG "CompactWifiBoardLCD"

#if CONFIG_LCD_GC9A01_240X240
LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);
#elif CONFIG_LCD_GC9A01_160X160
LV_FONT_DECLARE(font_puhui_14_1);
LV_FONT_DECLARE(font_awesome_14_1);
#endif

static const gc9a01_lcd_init_cmd_t vendor_specific_init_new[] = {
    // === 你原代码里的三次“重置” ===
    {0xA0, NULL, 0, 50},
    {0xA0, NULL, 0, 50},
    {0xA0, NULL, 0, 120},

    // === 解锁/页切换等 ===
    {0xFE, NULL, 0, 0},
    {0xEF, NULL, 0, 0},

    // 0x80~0x8E 全部写 0xFF
    {0x80, (uint8_t[]){0xFF}, 1, 0},
    {0x81, (uint8_t[]){0xFF}, 1, 0},
    {0x82, (uint8_t[]){0xFF}, 1, 0},
    {0x83, (uint8_t[]){0xFF}, 1, 0},
    {0x84, (uint8_t[]){0xFF}, 1, 0},
    {0x85, (uint8_t[]){0xFF}, 1, 0},
    {0x86, (uint8_t[]){0xFF}, 1, 0},
    {0x87, (uint8_t[]){0xFF}, 1, 0},
    {0x88, (uint8_t[]){0xFF}, 1, 0},
    {0x89, (uint8_t[]){0xFF}, 1, 0},
    {0x8A, (uint8_t[]){0xFF}, 1, 0},
    {0x8B, (uint8_t[]){0xFF}, 1, 0},
    {0x8C, (uint8_t[]){0xFF}, 1, 0},
    {0x8D, (uint8_t[]){0xFF}, 1, 0},
    {0x8E, (uint8_t[]){0xFF}, 1, 0},

    // 像素格式 16bit
    {0x3A, (uint8_t[]){0x05}, 1, 0},
    // ?
    {0xEC, (uint8_t[]){0x01}, 1, 0},

    // 你原来的多字节配置
    {0x74, (uint8_t[]){0x02, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00}, 7, 0},
    {0x98, (uint8_t[]){0x3E}, 1, 0},
    {0x99, (uint8_t[]){0x3E}, 1, 0},
    {0xB5, (uint8_t[]){0x0D, 0x0D}, 2, 0},

    {0x60, (uint8_t[]){0x38, 0x0F, 0x79, 0x67}, 4, 0},
    {0x61, (uint8_t[]){0x38, 0x11, 0x79, 0x67}, 4, 0},
    {0x64, (uint8_t[]){0x38, 0x17, 0x71, 0x5F, 0x79, 0x67}, 6, 0},
    {0x65, (uint8_t[]){0x38, 0x13, 0x71, 0x5B, 0x79, 0x67}, 6, 0},

    {0x6A, (uint8_t[]){0x00, 0x00}, 2, 0},
    {0x6C, (uint8_t[]){0x22, 0x02, 0x22, 0x02, 0x22, 0x22, 0x50}, 7, 0},

    // 0x6E 共 32 字节
    {0x6E, (uint8_t[]){0x03, 0x03, 0x01, 0x01, 0x00, 0x00, 0x0f, 0x0f, 0x0d, 0x0d, 0x0b, 0x0b, 0x09, 0x09, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x0a, 0x0c, 0x0c, 0x0e, 0x0e, 0x10, 0x10, 0x00, 0x00, 0x02, 0x02, 0x04, 0x04}, 32, 0},

    {0xBF, (uint8_t[]){0x01}, 1, 0},
    {0xF9, (uint8_t[]){0x40}, 1, 0},
    {0x9B, (uint8_t[]){0x3b, 0x33, 0x7f, 0x00}, 4, 0},
    {0x7E, (uint8_t[]){0x30}, 1, 0},

    {0x70, (uint8_t[]){0x0d, 0x02, 0x08, 0x0d, 0x02, 0x08}, 6, 0},
    {0x71, (uint8_t[]){0x0d, 0x02, 0x08}, 3, 0},

    {0x91, (uint8_t[]){0x0E, 0x09}, 2, 0},
    {0xC3, (uint8_t[]){0x18}, 1, 0},
    {0xC4, (uint8_t[]){0x18}, 1, 0},
    {0xC9, (uint8_t[]){0x3c}, 1, 0},

    {0xF0, (uint8_t[]){0x13, 0x15, 0x04, 0x05, 0x01, 0x38}, 6, 0},
    {0xF2, (uint8_t[]){0x13, 0x15, 0x04, 0x05, 0x01, 0x34}, 6, 0},
    {0xF1, (uint8_t[]){0x4b, 0xb8, 0x7b, 0x34, 0x35, 0xef}, 6, 0},
    {0xF3, (uint8_t[]){0x47, 0xb4, 0x72, 0x34, 0x35, 0xda}, 6, 0},

    // MADCTL（方向/镜像）——你原代码给 0x00，如有需要可改
    {0x36, (uint8_t[]){0x00}, 1, 0},

    // 退出休眠 / 开显示 / 内存写
    {0x11, NULL, 0, 200},
    {0x29, NULL, 0, 0},
    {0x2C, NULL, 0, 0},
};

class CompactWifiBoardLCD : public WifiBoard
{
private:
    esp_lcd_panel_io_handle_t lcd_io = NULL;
    esp_lcd_panel_handle_t lcd_panel = NULL;
    Button boot_button_;
    LcdDisplay *display_;
    VbAduioCodec audio_codec;
    PowerSaveTimer *power_save_timer_;

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

        ESP_LOGI(TAG, "Install GC9A01 panel driver");

        esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = GC9A01_SPI1_LCD_GPIO_RST,
            .color_space = GC9A01_LCD_COLOR_SPACE,
            .bits_per_pixel = GC9A01_LCD_BITS_PER_PIXEL,

        };
        panel_config.rgb_endian = DISPLAY_RGB_ORDER;

#if CONFIG_LCD_GC9A01_160X160
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = vendor_specific_init_new,
            .init_cmds_size = sizeof(vendor_specific_init_new) / sizeof(gc9a01_lcd_init_cmd_t),
        };
        panel_config.vendor_config = &gc9107_vendor_config;
#endif
        esp_lcd_new_panel_gc9a01(lcd_io, &panel_config, &lcd_panel);

        esp_lcd_panel_reset(lcd_panel);
        esp_lcd_panel_init(lcd_panel);
        esp_lcd_panel_invert_color(lcd_panel, DISPLAY_COLOR_INVERT);
        esp_lcd_panel_disp_on_off(lcd_panel, true);
#if CONFIG_SUPPORT_MINI_PROGRAMS_REPLACE_PSD
        display_ = new AnimEyeDisplay(lcd_io, lcd_panel,
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
#else
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
#endif
    }


public:
    CompactWifiBoardLCD() : boot_button_(BOOT_BUTTON_GPIO), audio_codec(CODEC_RX_GPIO, CODEC_TX_GPIO)
    {

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

        // 设置SLEEP_GOIO引脚为上拉输入模式
        gpio_set_pull_mode(SLEEP_GOIO, GPIO_PULLUP_ONLY);
        // 设置SLEEP_GOIO引脚为输出模式
        gpio_set_direction(SLEEP_GOIO, GPIO_MODE_OUTPUT);
        // 设置SLEEP_GOIO引脚电平为高
        gpio_set_level(SLEEP_GOIO, 1);

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

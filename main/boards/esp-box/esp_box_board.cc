#include "wifi_board.h"
#include "audio_codecs/box_audio_codec.h"
#include "display/oled_display.h"
// #include "display/lcd_display.h"
// #include "esp_lcd_ili9341.h"
#include "font_awesome_symbols.h"
#include "application.h"
#include "button.h"
#include "led/single_led.h"
#include "config.h"
#include "assets/lang_config.h"
#include "iot/thing_manager.h"
#include "axp2101.h"
#include "power_manager.h"
#include "power_save_timer.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <wifi_station.h>

#define TAG "EspBoxBoard"

// LV_FONT_DECLARE(font_puhui_20_4);
// LV_FONT_DECLARE(font_awesome_20_4);

LV_FONT_DECLARE(font_puhui_14_1);
LV_FONT_DECLARE(font_awesome_14_1);

// Init ili9341 by custom cmd
// static const ili9341_lcd_init_cmd_t vendor_specific_init[] = {
//     {0xC8, (uint8_t []){0xFF, 0x93, 0x42}, 3, 0},
//     {0xC0, (uint8_t []){0x0E, 0x0E}, 2, 0},
//     {0xC5, (uint8_t []){0xD0}, 1, 0},
//     {0xC1, (uint8_t []){0x02}, 1, 0},
//     {0xB4, (uint8_t []){0x02}, 1, 0},
//     {0xE0, (uint8_t []){0x00, 0x03, 0x08, 0x06, 0x13, 0x09, 0x39, 0x39, 0x48, 0x02, 0x0a, 0x08, 0x17, 0x17, 0x0F}, 15, 0},
//     {0xE1, (uint8_t []){0x00, 0x28, 0x29, 0x01, 0x0d, 0x03, 0x3f, 0x33, 0x52, 0x04, 0x0f, 0x0e, 0x37, 0x38, 0x0F}, 15, 0},

//     {0xB1, (uint8_t []){00, 0x1B}, 2, 0},
//     {0x36, (uint8_t []){0x08}, 1, 0},
//     {0x3A, (uint8_t []){0x55}, 1, 0},
//     {0xB7, (uint8_t []){0x06}, 1, 0},

//     {0x11, (uint8_t []){0}, 0x80, 0},
//     {0x29, (uint8_t []){0}, 0x80, 0},

//     {0, (uint8_t []){0}, 0xff, 0},
// };

class Pca9557 : public I2cDevice {
    public:
        Pca9557(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
            WriteReg(0x01, 0x03);
            WriteReg(0x03, 0xf8);
        }
    
        void SetOutputState(uint8_t bit, uint8_t level) {
            uint8_t data = ReadReg(0x01);
            data = (data & ~(1 << bit)) | (level << bit);
            WriteReg(0x01, data);
        }
    };
    
class Pmic : public Axp2101 {
    public:
        Pmic(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : Axp2101(i2c_bus, addr) {
            // ** EFUSE defaults **
            WriteReg(0x22, 0b110); // PWRON > OFFLEVEL as POWEROFF Source enable
            WriteReg(0x27, 0x10);  // hold 4s to power off
        
            WriteReg(0x93, 0x1C); // 配置 aldo2 输出为 3.3V
        
            uint8_t value = ReadReg(0x90); // XPOWERS_AXP2101_LDO_ONOFF_CTRL0
            value = value | 0x02; // set bit 1 (ALDO2)
            WriteReg(0x90, value);  // and power channels now enabled
        
            WriteReg(0x64, 0x03); // CV charger voltage setting to 4.2V
            
            WriteReg(0x61, 0x05); // set Main battery precharge current to 125mA
            WriteReg(0x62, 0x0A); // set Main battery charger current to 400mA ( 0x08-200mA, 0x09-300mA, 0x0A-400mA )
            WriteReg(0x63, 0x15); // set Main battery term charge current to 125mA
        
            WriteReg(0x14, 0x00); // set minimum system voltage to 4.1V (default 4.7V), for poor USB cables
            WriteReg(0x15, 0x00); // set input voltage limit to 3.88v, for poor USB cables
            WriteReg(0x16, 0x05); // set input current limit to 2000mA
        
            WriteReg(0x24, 0x01); // set Vsys for PWROFF threshold to 3.2V (default - 2.6V and kill battery)
            WriteReg(0x50, 0x14); // set TS pin to EXTERNAL input (not temperature)
        }
    };

class EspBox3Board : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_bus_handle_t display_i2c_bus_;
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    // LcdDisplay* display_;
    Display* display_ = nullptr;
    Pmic* pmic_ = nullptr;
    PowerManager *power_manager_;
    PowerSaveTimer* power_save_timer_;

    void InitializePowerManager() {
        power_manager_ = new PowerManager(GPIO_NUM_38);
        power_manager_->OnChargingStatusChanged([this](bool is_charging) {  //设置充电状态变化回调
            if (is_charging) {
                power_save_timer_->SetEnabled(false);
            } else {
                power_save_timer_->SetEnabled(true);
            } 
        });
    }
    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(240, 60, -1);    //创建电源管理器对象
        power_save_timer_->OnEnterSleepMode([this]() {
            ESP_LOGI(TAG, "Enabling sleep mode");
            display_->SetChatMessage("system", "");
            display_->SetEmotion("sleepy");
            GetBacklight()->SetBrightness(1);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            display_->SetChatMessage("system", "");
            display_->SetEmotion("neutral");
            GetBacklight()->RestoreBrightness();
        });
         
        power_save_timer_->SetEnabled(true);
    }


    // void InitializeI2c() {
    //     // Initialize I2C peripheral
    //     i2c_master_bus_config_t i2c_bus_cfg = {
    //         .i2c_port = (i2c_port_t)1,
    //         .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
    //         .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
    //         .clk_source = I2C_CLK_SRC_DEFAULT,
    //         .glitch_ignore_cnt = 7,
    //         .intr_priority = 0,
    //         .trans_queue_depth = 0,
    //         .flags = {
    //             .enable_internal_pullup = 1,
    //         },
    //     };
    //     ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    // }

    // void InitializeSpi() {
    //     spi_bus_config_t buscfg = {};
    //     buscfg.mosi_io_num = GPIO_NUM_6;
    //     buscfg.miso_io_num = GPIO_NUM_NC;
    //     buscfg.sclk_io_num = GPIO_NUM_7;
    //     buscfg.quadwp_io_num = GPIO_NUM_NC;
    //     buscfg.quadhd_io_num = GPIO_NUM_NC;
    //     buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
    //     ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    // }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            Application::GetInstance().ToggleChatState();
        });
        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    // void InitializeIli9341Display() {
    //     esp_lcd_panel_io_handle_t panel_io = nullptr;
    //     esp_lcd_panel_handle_t panel = nullptr;

    //     // 液晶屏控制IO初始化
    //     ESP_LOGD(TAG, "Install panel IO");
    //     esp_lcd_panel_io_spi_config_t io_config = {};
    //     io_config.cs_gpio_num = GPIO_NUM_5;
    //     io_config.dc_gpio_num = GPIO_NUM_4;
    //     io_config.spi_mode = 0;
    //     io_config.pclk_hz = 40 * 1000 * 1000;
    //     io_config.trans_queue_depth = 10;
    //     io_config.lcd_cmd_bits = 8;
    //     io_config.lcd_param_bits = 8;
    //     ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

    //     // 初始化液晶屏驱动芯片
    //     ESP_LOGD(TAG, "Install LCD driver");
    //     const ili9341_vendor_config_t vendor_config = {
    //         .init_cmds = &vendor_specific_init[0],
    //         .init_cmds_size = sizeof(vendor_specific_init) / sizeof(ili9341_lcd_init_cmd_t),
    //     };

    //     esp_lcd_panel_dev_config_t panel_config = {};
    //     panel_config.reset_gpio_num = GPIO_NUM_48;
    //     panel_config.flags.reset_active_high = 0,
    //     panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    //     panel_config.bits_per_pixel = 16;
    //     panel_config.vendor_config = (void *)&vendor_config;
    //     ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
        
    //     esp_lcd_panel_reset(panel);
    //     esp_lcd_panel_init(panel);
    //     esp_lcd_panel_invert_color(panel, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
    //     esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
    //     esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    //     esp_lcd_panel_disp_on_off(panel, true);
    //     display_ = new SpiLcdDisplay(panel_io, panel,
    //                                 DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
    //                                 {
    //                                     .text_font = &font_puhui_20_4,
    //                                     .icon_font = &font_awesome_20_4,
    //                                     .emoji_font = font_emoji_64_init(),
    //                                 });
    // }


    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display() {
        // SSD1306 config
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 400 * 1000,
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
        ESP_LOGI(TAG, "SSD1306 driver installed");

        // Reset the display
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }

        // Set the display to on
        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y,
            {&font_puhui_14_1, &font_awesome_14_1});
    }

    void InitializeCodecI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));

         // Initialize PCA9557
         ESP_LOGI(TAG, "Init AXP2101");
         pmic_ = new Pmic(i2c_bus_, 0x34);
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Screen"));
        thing_manager.AddThing(iot::CreateThing("Battery"));
    }

public:
    EspBox3Board() : boot_button_(BOOT_BUTTON_GPIO),
    volume_up_button_(VOLUME_UP_BUTTON_GPIO),
    volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) 
    {
        InitializeCodecI2c();
         InitializeDisplayI2c();
         InitializeSsd1306Display();

         InitializePowerSaveTimer();

        InitializeButtons();
        InitializeIot();
        // GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            i2c_bus_, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, 
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_LRCK, 
            AUDIO_I2S_CODEC_DSDIN, 
            AUDIO_I2S_ADC_SDOUT,
            AUDIO_CODEC_PA_PIN, 
            AUDIO_CODEC_ES8311_ADDR, 
            AUDIO_CODEC_ES7210_ADDR, 
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        charging = pmic_->IsCharging();
        discharging = pmic_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }

        level = pmic_->GetBatteryLevel();
        return true;
    }

    // virtual Backlight* GetBacklight() override {
    //     static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
    //     return &backlight;
    // }
};

DECLARE_BOARD(EspBox3Board);

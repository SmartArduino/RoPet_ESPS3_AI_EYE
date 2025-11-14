
#include "wifi_board.h"
#include "audio_codecs/vb6824_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"
#include "power_save_timer.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>

#include "lcd_cmd.h"

#include "m_touch_button.h"
#include "motor.h"

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
                                 // 获取应用程序实例
                                 auto &app = Application::GetInstance();
                                app.ToggleChatState(); });

#if (defined(CONFIG_VB6824_OTA_SUPPORT) && CONFIG_VB6824_OTA_SUPPORT == 1)
        boot_button_.OnDoubleClick([this]()
                                   {
            if (esp_timer_get_time() > 20 * 1000 * 1000)
            {
                ESP_LOGI(TAG, "Long press, do not enter OTA mode %ld", (uint32_t)esp_timer_get_time());
                return;
            }
            audio_codec.OtaStart(0); });
#endif

        boot_button_.OnPressRepeaDone([this](uint16_t count)
                                      {
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

        display_ = new SpiLcdDisplay(panel_io, panel,
                                     DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X,
                                     DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                     {
                                         .text_font = &ont_puhui_20_4,
                                         .icon_font = &font_awesome_20_4,
                                         .emoji_font = font_emoji_64_init(),
                                     });

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
        io_config.pclk_hz = 20 * 1000 * 1000;
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

    //===============================================SC7A20H传感器========================================
    // void I2C_ScanBus()
    // {
    //     ESP_LOGI(TAG, "开始 I2C 扫描（新驱动）...");

    //     /* 2. 逐地址探测 */
    //     int found = 0;
    //     for (uint8_t addr = 0x08; addr <= 0x77; ++addr)
    //     {
    //         if (i2c_master_probe(sc7a20h_bus_handle, addr, 50) == ESP_OK)
    //         {
    //             ESP_LOGI(TAG, "发现设备 at 0x%02X", addr);
    //             ++found;
    //         }
    //     }

    //     ESP_LOGI(TAG, "扫描完成，共发现 %d 个设备", found);
    // }

    // esp_err_t sc7a20h_write_byte(uint8_t reg, uint8_t value)
    // {
    //     uint8_t buf[2] = {reg, value};

    //     /* new driver 一次写命令+数据 */
    //     return i2c_master_transmit(sc7a20h_dev_handle, buf, sizeof(buf), pdMS_TO_TICKS(1000));
    // }

    // esp_err_t sc7a20h_read_bytes(uint8_t reg, uint8_t *data, size_t len)
    // {
    //     /* 第一步：把寄存器地址发出去 */
    //     esp_err_t ret = i2c_master_transmit(sc7a20h_dev_handle, &reg, 1, pdMS_TO_TICKS(1000));
    //     if (ret != ESP_OK)
    //     {
    //         return ret;
    //     }

    //     /* 第二步：重启总线，读回 len 字节 */
    //     return i2c_master_receive(sc7a20h_dev_handle, data, len, pdMS_TO_TICKS(1000));
    // }

    /**
     * @brief 读取加速度数据(12位分辨率)
     * @param x X轴加速度输出(单位: mg, 范围: ±2000mg)
     * @param y Y轴加速度输出(单位: mg, 范围: ±2000mg)
     * @param z Z轴加速度输出(单位: mg, 范围: ±2000mg)
     * @return esp_err_t ESP_OK表示成功
     *
     * 该函数一次性读取OUT_X_L_REG(0x28)到OUT_Z_H_REG(0x2D)共6个寄存器，
     * 将高低字节组合成16位数据后右移4位(12位有效数据)，
     * 根据CTRL_REG4配置的±4g量程，1LSB对应1mg
     */
    // esp_err_t sc7a20h_read_accel(int16_t *x, int16_t *y, int16_t *z)
    // {
    //     if (x == NULL || y == NULL || z == NULL)
    //     {
    //         ESP_LOGE(TAG, "无效的输出指针");
    //         return ESP_ERR_INVALID_ARG;
    //     }

    //     uint8_t data[6] = {0};
    //     esp_err_t err_XL = sc7a20h_read_bytes(OUT_X_L_REG, &data[0], 1);
    //     if (err_XL != ESP_OK)
    //     {
    //         ESP_LOGE(TAG, "读取加速度数据失败: 0x%x", err_XL);
    //         return err_XL;
    //     }
    //     esp_err_t err_XH = sc7a20h_read_bytes(OUT_X_H_REG, &data[1], 1);
    //     if (err_XH != ESP_OK)
    //     {
    //         ESP_LOGE(TAG, "读取加速度数据失败: 0x%x", err_XH);
    //         return err_XH;
    //     }
    //     esp_err_t err_YL = sc7a20h_read_bytes(OUT_Y_L_REG, &data[2], 1);
    //     if (err_YL != ESP_OK)
    //     {
    //         ESP_LOGE(TAG, "读取加速度数据失败: 0x%x", err_YL);
    //         return err_YL;
    //     }
    //     esp_err_t err_YH = sc7a20h_read_bytes(OUT_Y_H_REG, &data[3], 1);
    //     if (err_YH != ESP_OK)
    //     {
    //         ESP_LOGE(TAG, "读取加速度数据失败: 0x%x", err_YH);
    //         return err_YH;
    //     }
    //     esp_err_t err_ZL = sc7a20h_read_bytes(OUT_Z_L_REG, &data[4], 1);
    //     if (err_ZL != ESP_OK)
    //     {
    //         ESP_LOGE(TAG, "读取加速度数据失败: 0x%x", err_ZL);
    //         return err_ZL;
    //     }
    //     esp_err_t err_ZH = sc7a20h_read_bytes(OUT_Z_H_REG, &data[5], 1);
    //     if (err_ZH != ESP_OK)
    //     {
    //         ESP_LOGE(TAG, "读取加速度数据失败: 0x%x", err_ZH);
    //         return err_ZH;
    //     }

    //     // 组合高低字节(16位有符号数)
    //     int16_t raw_x = (int16_t)((data[1] << 8) | data[0]);
    //     int16_t raw_y = (int16_t)((data[3] << 8) | data[2]);
    //     int16_t raw_z = (int16_t)((data[5] << 8) | data[4]);

    //     // ±4g量程时 1LSB = 1mg
    //     *x = raw_x;
    //     *y = raw_y;
    //     *z = raw_z;

    //     // // 转换为g值(1g = 1000mg)
    //     // float g_x = raw_x / 1000.0f;
    //     // float g_y = raw_y / 1000.0f;
    //     // float g_z = raw_z / 1000.0f;

    //     // ESP_LOGI(TAG, "加速度数据: X=%.2fmg(0x%04X) Y=%.2fmg(0x%04X) Z=%.2fmg(0x%04X)",
    //     //          (float)raw_x, (uint16_t)raw_x,
    //     //          (float)raw_y, (uint16_t)raw_y,
    //     //          (float)raw_z, (uint16_t)raw_z);
    //     // ESP_LOGI(TAG, "Accel X:%.2fg Y:%.2fg Z:%.2fg", g_x, g_y, g_z);

    //     return ESP_OK;
    // }

    // void InitializeSC7A20H(void)
    // {
    //     /* 1. 创建 I2C 主机总线 */
    //     i2c_master_bus_config_t i2c_bus_config = {
    //         .i2c_port = SC7A20H_I2C_PORT,
    //         .sda_io_num = SC7A20H_I2C_SDA,
    //         .scl_io_num = SC7A20H_I2C_SCL,
    //         .clk_source = I2C_CLK_SRC_DEFAULT,
    //         .glitch_ignore_cnt = 7,
    //         .flags = {
    //             .enable_internal_pullup = 1,
    //         },
    //     };

    //     ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &sc7a20h_bus_handle));
    //     I2C_ScanBus();
    //     /* 2. 初始化传感器 */

    //     i2c_device_config_t sc7a20h_cfg = {
    //         .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    //         .device_address = SC7A20H_I2C_ADDR,
    //         .scl_speed_hz = 400 * 1000,
    //     };
    //     ESP_ERROR_CHECK(i2c_master_bus_add_device(sc7a20h_bus_handle, &sc7a20h_cfg, &sc7a20h_dev_handle));

    //     uint8_t who_am_i;
    //     esp_err_t err = sc7a20h_read_bytes(WHO_AM_I_REG, &who_am_i, 1);
    //     if (err != ESP_OK)
    //     {
    //         ESP_LOGE(TAG, "读取WHO_AM_I寄存器失败: 0x%x", err);
    //         return;
    //     }

    //     if (who_am_i != 0x11)
    //     {
    //         ESP_LOGE(TAG, "无效的WHO_AM_I值: 0x%02X (期望值: 0x11)", who_am_i);
    //         return;
    //     }

    //     // 配置加速度计: 100Hz输出数据率, ±4g量程
    //     sc7a20h_write_byte(CTRL_REG1, 0x57); // CTRL_REG1
    //     if (err != ESP_OK)
    //     {
    //         ESP_LOGE(TAG, "写入CTRL_REG1失败: 0x%x", err);
    //         return;
    //     }
    //     sc7a20h_write_byte(CTRL_REG4, 0x01); // CTRL_REG4
    //     if (err != ESP_OK)
    //     {
    //         ESP_LOGE(TAG, "写入CTRL_REG4失败: 0x%x", err);
    //     }

    //     int16_t x, y, z;
    //     if (sc7a20h_read_accel(&x, &y, &z) == ESP_OK)
    //     {
    //         ESP_LOGI(TAG, "Accel X:%.2fg Y:%.2fg Z:%.2fg",
    //                  x * 0.004f, y * 0.004f, z * 0.004f);
    //     }

    // }
    //==========================================================================================

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

        // motor_init(TEMP_EN);
        // touch_button_init();

        // // InitializePowerSaveTimer();

        // 设置音频编解码器唤醒回调函数
        audio_codec.OnWakeUp([this](const std::string &command)
                             {
            // 如果唤醒词为vb6824_get_wakeup_word()，则唤醒设备
            if (command == std::string(vb6824_get_wakeup_word())){
                if(Application::GetInstance().GetDeviceState() != kDeviceStateListening){
                    ESP_LOGI(TAG, "Wake word detected: %s", command.c_str());
                    Application::GetInstance().WakeWordInvoke("你好小智");
                }
            // 如果唤醒词为"开始配网"，则重置WiFi配置
            }else if (command == "开始配网"){
                ResetWifiConfiguration();
            } });
        audio_codec.SetOutputVolume(100);

        // InitializeSC7A20H();
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

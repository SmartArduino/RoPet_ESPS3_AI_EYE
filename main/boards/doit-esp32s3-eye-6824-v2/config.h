
#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_


/*=====================SC7A20H三轴加速度=====================*/
#define SC7A20H_I2C_SDA GPIO_NUM_1
#define SC7A20H_I2C_SCL GPIO_NUM_2
#define SC7A20H_I2C_PORT I2C_NUM_1
#define SC7A20H_I2C_ADDR 0x19 // 根据 SDO 电平选择 0x30 或 0x32,SDO 接高电平（或悬空）：0x19（7位地址）→ 写：0x32，读：0x33;SDO 接低电平：0x18 → 写：0x30，读：0x31

// 寄存器地址
#define WHO_AM_I_REG 0x0F
#define CTRL_REG1 0x20
#define CTRL_REG4 0x23
#define OUT_X_L_REG 0x28
#define OUT_X_H_REG 0x29
#define OUT_Y_L_REG 0x2A
#define OUT_Y_H_REG 0x2B
#define OUT_Z_L_REG 0x2C
#define OUT_Z_H_REG 0x2D
/*=============================================================*/

#define TEMP_EN GPIO_NUM_39 // 舵机

#if CONFIG_LCD_ST77916_360X360
/*================================QSPI========================*/
// 分辨率
#define DISPLAY_WIDTH 360
#define DISPLAY_HEIGHT 360

#define QSPI_LCD_HOST SPI2_HOST
#define QSPI_PIN_NUM_LCD_PCLK GPIO_NUM_15
#define QSPI_PIN_NUM_LCD_CS GPIO_NUM_12
#define QSPI_PIN_NUM_LCD_DATA0 GPIO_NUM_14
#define QSPI_PIN_NUM_LCD_DATA1 GPIO_NUM_10
#define QSPI_PIN_NUM_LCD_DATA2 GPIO_NUM_9
#define QSPI_PIN_NUM_LCD_DATA3 GPIO_NUM_8
#define QSPI_PIN_NUM_LCD_RST GPIO_NUM_13
#define QSPI_PIN_NUM_LCD_BL GPIO_NUM_11
#define QSPI_PIN_NUM_LCD_TE GPIO_NUM_NC
#define QSPI_DISPLAY_BACKLIGHT_OUTPUT_INVERT false

// 镜像和轴交换：通常不需要镜像或轴交换，除非你的硬件设计需要特定的显示方向。
#define DISPLAY_MIRROR_X false

#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false

// 偏移量：通常不需要偏移，除非你的显示屏有特定的显示区域限制。
#define DISPLAY_OFFSET_X 0
#define DISPLAY_OFFSET_Y 0

#define DISPLAY_RGB_ORDER LCD_RGB_ELEMENT_ORDER_RGB
/*=============================================================*/
#elif CONFIG_LCD_ST7796_240X240 || CONFIG_LCD_GC9A01_240X240 || CONFIG_LCD_GC9A01_160X160

#if CONFIG_LCD_ST7796_240X240 || CONFIG_LCD_GC9A01_240X240
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 240
#elif CONFIG_LCD_GC9A01_160X160
#define DISPLAY_WIDTH 160
#define DISPLAY_HEIGHT 160
#endif

#define SPI_LCD_HOST SPI2_HOST
#define SPI_LCD_GPIO_SCLK (GPIO_NUM_15)
#define SPI_LCD_GPIO_MOSI (GPIO_NUM_14)
#define SPI_LCD_GPIO_RST (GPIO_NUM_13)
#define SPI_LCD_GPIO_DC (GPIO_NUM_12)
#define SPI_LCD_GPIO_CS (GPIO_NUM_NC)
#define SPI_LCD_GPIO_MISO (GPIO_NUM_NC)
#define SPI_LCD_BL GPIO_NUM_11

#define SPI_DISPLAY_BACKLIGHT_OUTPUT_INVERT false

// 镜像和轴交换：通常不需要镜像或轴交换，除非你的硬件设计需要特定的显示方向。
#if CONFIG_LCD_GC9A01_240X240
#define DISPLAY_MIRROR_X true
#elif CONFIG_LCD_GC9A01_160X160 || CONFIG_LCD_ST7796_240X240
#define DISPLAY_MIRROR_X false
#endif

#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false

// 偏移量：通常不需要偏移，除非你的显示屏有特定的显示区域限制。
#define DISPLAY_OFFSET_X 0
#define DISPLAY_OFFSET_Y 0

#if CONFIG_LCD_GC9A01_240X240
#define DISPLAY_RGB_ORDER LCD_RGB_ELEMENT_ORDER_BGR
#else
#define DISPLAY_RGB_ORDER LCD_RGB_ELEMENT_ORDER_RGB
#endif

#if CONFIG_LCD_ST7796_240X240 || CONFIG_LCD_GC9A01_240X240
#define DISPLAY_COLOR_INVERT true
#elif CONFIG_LCD_GC9A01_160X160
#define DISPLAY_COLOR_INVERT false
#endif

#endif

#define CODEC_TX_GPIO GPIO_NUM_47
#define CODEC_RX_GPIO GPIO_NUM_48
#define BUILTIN_LED_GPIO GPIO_NUM_41
#define BOOT_BUTTON_GPIO GPIO_NUM_0
#define SLEEP_GOIO GPIO_NUM_21

#endif // _BOARD_CONFIG_H_

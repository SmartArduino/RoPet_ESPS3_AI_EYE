#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
 #include "esp_lcd_panel_ops.h"

/*==========小智+魔眼============ */
/* LCD size */
#define GC9A01_LCD_H_RES   (240)
#define GC9A01_LCD_V_RES   (240)
/* LCD settings */
#define GC9A01_LCD_SPI1_NUM         (SPI3_HOST)
#define GC9A01_LCD_SPI2_NUM         (SPI2_HOST)
#define GC9A01_LCD_PIXEL_CLK_HZ    (20 * 1000 * 1000)
#define GC9A01_LCD_CMD_BITS        (8)
#define GC9A01_LCD_PARAM_BITS      (8)
#define GC9A01_LCD_COLOR_SPACE     (ESP_LCD_COLOR_SPACE_RGB)
#define GC9A01_LCD_BITS_PER_PIXEL  (16)
#define GC9A01_LCD_DRAW_BUFF_DOUBLE (1)
#define GC9A01_LCD_DRAW_BUFF_HEIGHT (240)
#define GC9A01_LCD_BL_ON_LEVEL     (1)
/* LCD-SPI2 pins */
#define GC9A01_SPI2_LCD_GPIO_SCLK       (GPIO_NUM_38)
#define GC9A01_SPI2_LCD_GPIO_MOSI       (GPIO_NUM_39)
#define GC9A01_SPI2_LCD_GPIO_RST        (GPIO_NUM_40)
#define GC9A01_SPI2_LCD_GPIO_DC         (GPIO_NUM_0)
#define GC9A01_SPI2_LCD_GPIO_CS         (GPIO_NUM_NC)

#define GC9A01_SPI2_LCD_GPIO_BL         (GPIO_NUM_NC)
#define GC9A01_SPI2_LCD_GPIO_MISO       (GPIO_NUM_NC)

/* LCD-SPI1 pins */
#define GC9A01_SPI1_LCD_GPIO_SCLK       (GPIO_NUM_42)
#define GC9A01_SPI1_LCD_GPIO_MOSI       (GPIO_NUM_2)
#define GC9A01_SPI1_LCD_GPIO_RST        (GPIO_NUM_1)
#define GC9A01_SPI1_LCD_GPIO_DC         (GPIO_NUM_41)
#define GC9A01_SPI1_LCD_GPIO_CS         (GPIO_NUM_NC)

#define GC9A01_SPI1_LCD_GPIO_BL         (GPIO_NUM_NC)
#define GC9A01_SPI1_LCD_GPIO_MISO       (GPIO_NUM_NC)

#if !defined(IRIS_MAX)
#define MACRO
#define IRIS_MAX 280
#endif // MACRO
#if !defined(IRIS_MIN)
#define MACRO
#define IRIS_MIN 180
#endif // MACRO

#define  LINES_PER_BATCH 10 //缓冲区的行数为10行

#define NOBLINK 0     // Not currently engaged in a blink
#define ENBLINK 1     // Eyelid is currently closing
#define DEBLINK 2     // Eyelid is currently opening
#define BUFFER_SIZE 1024 // 64 to 512 seems optimum = 30 fps for default eye

#define NUM_EYES (1)    //定义眼睛数量
#define DISPLAY_SIZE 240    //显示尺寸

extern bool is_blink;   //眨眼
extern bool is_track;   //跟踪
extern int16_t eyeNewX, eyeNewY;    // 新的眼睛位置数据


extern esp_lcd_panel_io_handle_t lcd_io_eye;
extern esp_lcd_panel_handle_t lcd_panel_eye;
extern esp_lcd_panel_io_handle_t lcd_io_eye2;
extern esp_lcd_panel_handle_t lcd_panel_eye2;

extern const uint16_t *sclera;
extern const uint16_t *iris;

extern TaskHandle_t task_update_eye_handler;   //魔眼更新任务的句柄

extern SemaphoreHandle_t lcd_mutex ; // 全局互斥锁
//函数声明
void split(
    int16_t  startValue, // Iris scale value (IRIS_MIN to IRIS_MAX) at start
    int16_t  endValue,   // Iris scale value at end
    uint64_t startTime,  // Use esp_timer_get_time() for timing
    int32_t  duration,   // Start-to-end time, in microseconds
    int16_t  range);
void frame(uint16_t iScale);
void drawEye(uint8_t e, uint32_t iScale, uint32_t scleraX, uint32_t scleraY, uint32_t uT, uint32_t lT) ;
esp_err_t esp_lcd_safe_draw_bitmap(esp_lcd_panel_handle_t panel,    
    int x_start,
    int y_start,
    int x_end,
    int y_end,
    const void *color_data);
int map1(int x, int in_min, int in_max, int out_min, int out_max);
//生成一个在 [min, max] 范围内的随机整数。它确保生成的随机数是均匀分布的，并且包含边界值 min 和 max。
int my_random(int min, int max);
int my_random1(int max);


 void task_eye_update(void *pvParameters);
void task_eye_blink(void *pvParameters);
 void eye_update();
 void eye_blink();




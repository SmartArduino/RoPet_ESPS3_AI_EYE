/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

 #include "esp_err.h"
 #include "esp_log.h"
 #include "esp_check.h"
 #include "driver/i2c.h"
 #include "driver/gpio.h"
 #include "driver/spi_master.h"
 #include "esp_lcd_panel_io.h"
 #include "esp_lcd_panel_vendor.h"
#include <esp_log.h>
#include "esp_task_wdt.h"
 #include "esp_lvgl_port.h"
 #include "esp_lcd_gc9a01.h"
 #include "eyes_data.h"
 #include "esp_timer.h"
 #include "esp_random.h"

 #include "eye_display.h"

#include <stdbool.h>

static const char *TAG = "eye_display";
//两个面板眼睛的句柄
esp_lcd_panel_io_handle_t lcd_io_eye = NULL;
esp_lcd_panel_handle_t lcd_panel_eye = NULL;
esp_lcd_panel_io_handle_t lcd_io_eye2 = NULL;
esp_lcd_panel_handle_t lcd_panel_eye2 = NULL;
 

bool is_blink = true;   //眨眼
bool is_track = true;   //跟踪
int16_t eyeNewX = 512, eyeNewY = 512;    // 新的眼睛位置数据

SemaphoreHandle_t lcd_mutex = NULL; // 全局互斥锁

TaskHandle_t eye_update_handler = NULL;

// #define IRIS_MIN      140 // Clip lower analogRead() range from IRIS_PIN
// #define IRIS_MAX      260 // Clip upper "


// const uint16_t *sclera = sclera_default;
// const uint8_t *upper = upper_default;
// const uint8_t *lower = lower_default;
// const uint16_t *polar = polar_default;
// const uint16_t *iris = iris_default;
const uint16_t *sclera = sclera_my;
const uint8_t *upper = upper_my;
const uint8_t *lower = lower_my;
const uint16_t *polar = polar_my;
const uint16_t *iris = iris_my;


uint16_t SCLERA_WIDTH  = 375;   //巩膜的宽度（眼白）
uint16_t SCLERA_HEIGHT= 375;    //巩膜的高度（眼白）
uint16_t SCREEN_WIDTH  =240;    //屏幕宽度
uint16_t SCREEN_HEIGHT =240;    //屏幕高度
uint16_t IRIS_WIDTH  =150;      //虹膜的宽度
uint16_t IRIS_HEIGHT= 150;      //虹膜的高度
uint16_t IRIS_MAP_HEIGHT = 64;  // 虹膜映射的高度
uint16_t IRIS_MAP_WIDTH = 256; // 虹膜映射的宽度




//跟动画有关
const uint8_t ease[] = { // Ease in/out curve for eye movements 3*t^2-2*t^3
    0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  2,  2,  2,  3,   // T
    3,  3,  4,  4,  4,  5,  5,  6,  6,  7,  7,  8,  9,  9, 10, 10,   // h
   11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 22, 23,   // x
   24, 25, 26, 27, 27, 28, 29, 30, 31, 33, 34, 35, 36, 37, 38, 39,   // 2
   40, 41, 42, 44, 45, 46, 47, 48, 50, 51, 52, 53, 54, 56, 57, 58,   // A
   60, 61, 62, 63, 65, 66, 67, 69, 70, 72, 73, 74, 76, 77, 78, 80,   // l
   81, 83, 84, 85, 87, 88, 90, 91, 93, 94, 96, 97, 98,100,101,103,   // e
  104,106,107,109,110,112,113,115,116,118,119,121,122,124,125,127,   // c
  128,130,131,133,134,136,137,139,140,142,143,145,146,148,149,151,   // J
  152,154,155,157,158,159,161,162,164,165,167,168,170,171,172,174,   // a
  175,177,178,179,181,182,183,185,186,188,189,190,192,193,194,195,   // c
  197,198,199,201,202,203,204,205,207,208,209,210,211,213,214,215,   // o
  216,217,218,219,220,221,222,224,225,226,227,228,228,229,230,231,   // b
  232,233,234,235,236,237,237,238,239,240,240,241,242,243,243,244,   // s
  245,245,246,246,247,248,248,249,249,250,250,251,251,251,252,252,   // o
  252,253,253,253,254,254,254,254,254,255,255,255,255,255,255,255 }; // n

  uint32_t timeOfLastBlink = 0L, timeToNextBlink = 0L;    //记录上一次眨眼事件的开始时间（以微秒为单位）。  记录下一次眨眼事件的时间间隔（以微秒为单位）。
  uint16_t oldIris = (IRIS_MIN + IRIS_MAX) / 2, newIris;


//眨眼状态
typedef struct {
    uint8_t  state;     // NOBLINK/ENBLINK/DEBLINK
    int32_t  duration;  // Duration of blink state (micros)
    uint32_t startTime; // Time (micros) of last state change
  } eyeBlink;
  
  
int map1(int x, int in_min, int in_max, int out_min, int out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

//生成一个在 [min, max] 范围内的随机整数。它确保生成的随机数是均匀分布的，并且包含边界值 min 和 max。
int my_random(int min, int max) {
    return min + esp_random() % (max - min + 1);
}

int my_random1(int max) {
    return esp_random() % max;
}
uint32_t startTime;  // For FPS indicator

//存放所有眼睛的数组
struct {
  eyeBlink    blink;   // Current blink state
} eye[1];


TaskHandle_t task_update_eye_handler = NULL;   //魔眼更新任务的句柄


// 封装的安全绘图函数
/*
    参数：lcd的句柄、
    x_start、y_start：位图的起始坐标。
    x_end、y_end：位图的结束坐标。
    color_data：指向位图颜色数据的指针。
*/
esp_err_t esp_lcd_safe_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void *color_data) {
    if (xSemaphoreTake(lcd_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE("LCD", "Failed to acquire LCD mutex");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_lcd_panel_draw_bitmap(lcd_panel_eye, x_start, y_start, x_end, y_end, color_data);
    esp_err_t ret2 = esp_lcd_panel_draw_bitmap(lcd_panel_eye2, x_start, y_start, x_end, y_end, color_data);

    xSemaphoreGive(lcd_mutex);

    return (ret == ESP_OK ) ? ESP_OK : ESP_FAIL;
}


/* 对眼睛进行绘制 */
void drawEye(uint8_t e, uint32_t iScale, uint32_t scleraX, uint32_t scleraY, uint32_t uT, uint32_t lT) {

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
    uint16_t* lineBuf[2];
    lineBuf[0] = (uint16_t*)malloc(LINES_PER_BATCH * SCREEN_WIDTH * sizeof(uint16_t));
    lineBuf[1] = (uint16_t*)malloc(LINES_PER_BATCH * SCREEN_WIDTH * sizeof(uint16_t));
    if (lineBuf[0] == NULL || lineBuf[1] == NULL) {
        // 如果内存分配失败，释放已分配的缓冲区并返回
        if (lineBuf[0]) free(lineBuf[0]);
        if (lineBuf[1]) free(lineBuf[1]);
        return;
    }

    uint8_t bufIdx = 0;
    // 外循环，遍历屏幕的每一批行
    for (uint16_t screenY = 0; screenY < SCREEN_HEIGHT; screenY += LINES_PER_BATCH) {
        // 切换缓冲区
        uint16_t* currentBuf = lineBuf[bufIdx];
        bufIdx ^= 1;  // 切换缓冲区索引
        // 计算本次批处理的实际行数（处理到屏幕底部时可能不足10行）
        uint8_t linesToProcess = (SCREEN_HEIGHT - screenY) < LINES_PER_BATCH ? (SCREEN_HEIGHT - screenY) : LINES_PER_BATCH;

        // 内循环，遍历批处理的每一行
        for (uint8_t line = 0; line < linesToProcess; line++, scleraY++, irisY++) {
            // 重置scleraX到初始值
            scleraX = scleraXsave;
            irisX = scleraX - (SCLERA_WIDTH - IRIS_WIDTH) / 2;
            // 遍历屏幕的每一列
            for (screenX = 0; screenX < SCREEN_WIDTH; screenX++, scleraX++, irisX++) {
                uint32_t screenIdx = (screenY + line) * SCREEN_WIDTH + screenX;
                uint32_t pixelIdx = line * SCREEN_WIDTH + screenX;

                // 判断像素点是否被遮挡
                if ((lower[screenIdx] <= lT) || (upper[screenIdx] <= uT)) {
                    p = 0;  // 被眼睑遮挡
                } else if ((irisY < 0) || (irisY >= IRIS_HEIGHT) || (irisX < 0) || (irisX >= IRIS_WIDTH)) {
                    p = sclera[scleraY * SCLERA_WIDTH + scleraX];  // 在巩膜中
                } else {
                    p = polar[irisY * IRIS_WIDTH + irisX];         // 极角/距离
                    d = (iScale * (p & 0x7F)) / 240;
                    if (d < IRIS_MAP_HEIGHT) {
                        uint16_t a = (IRIS_MAP_WIDTH * (p >> 7)) / 512;
                        p = iris[d * IRIS_MAP_WIDTH + a];
                    } else {
                        p = sclera[scleraY * SCLERA_WIDTH + scleraX];
                    }
                }
                // 将像素数据写入当前缓冲区
                currentBuf[pixelIdx] = (p >> 8) | (p << 8);
            }
        }
        // 批量绘制当前处理的行
        esp_lcd_safe_draw_bitmap(0, screenY, SCREEN_WIDTH, screenY + linesToProcess, currentBuf);
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
void frame(uint16_t iScale)
{
    // uint32_t start_time = esp_timer_get_time();
    // ESP_LOGI(TAG, "frame start");
    static uint8_t eyeIndex = 0; // eye[] array counter //眼睛数组的索引
    int16_t eyeX, eyeY; //眼睛的位置
    uint32_t t = esp_timer_get_time(); // Time at start of function
    if (++eyeIndex >= NUM_EYES) //如果当前眼睛的索引已经到最后，回到第一个眼睛
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
        {                                      // Time up?  Destination reached.
            eyeInMotion = false;               // Stop moving
            eyeMoveDuration = my_random1(100000); // 0-3 sec stop  //眼睛变化的移动时间，降低至一秒
            eyeMoveStartTime = t;              // Save initial time of stop
            eyeX = eyeOldX = eyeNewX;          // Save position //变化完成，将eyeNewX和eyeNewY赋值
            eyeY = eyeOldY = eyeNewY;          //这里的eyeX和eyeY存放上一次变化的值
        }
        else
        {                                                       // Move time's not yet fully elapsed -- interpolate position
            //根据移动时间来设置移动的偏移量，ease是缓动曲线数组，用来让移动更加平滑
            int16_t e = ease[255 * dt / eyeMoveDuration] + 1;   // Ease curve   e计算出来的是ease数组的索引

            //得到当前应该移动的偏移量。这样，每次更新时，位置会逐渐逼近目标位置，而缓动因子e控制了移动的速度变化，使得移动更加自然
            eyeX = eyeOldX + (((eyeNewX - eyeOldX) * e) / 256); // Interp X 
            eyeY = eyeOldY + (((eyeNewY - eyeOldY) * e) / 256); // and Y    
        }
    }
    else    //这里是生成新的移动位置的地方，眼珠停止，计算新的位置
    { // Eye stopped    
        eyeX = eyeOldX;
        eyeY = eyeOldY;

        if (dt > eyeMoveDuration)
        { // Time up?  Begin new move.
            int16_t dx, dy;
            uint32_t d;

            do
            { // Pick new dest in circle    //新的移动位置在0~1023之间
                // eyeNewX = my_random1(1024); //需要变化的位置应该是在这里设置,这里注释掉,从外部获取
                // eyeNewY = my_random1(1024);
                dx = (eyeNewX * 2) - 1023;
                dy = (eyeNewY * 2) - 1023;
            } while ((d = (dx * dx + dy * dy)) > (1023 * 1023)); // Keep trying

            eyeMoveDuration = my_random(72000, 144000);             // ~1/14 - ~1/7 sec //变化的时间随机
            eyeMoveStartTime = t;                                // Save initial time of move
            eyeInMotion = true;                                  // Start move on next frame
        }
    }

    // Blinking
if(is_blink){
    // Similar to the autonomous eye movement above -- blink start times
    // and durations are my_random (within ranges).
    if ((t - timeOfLastBlink) >= timeToNextBlink)
    { // Start new blink?
        timeOfLastBlink = t;
        uint32_t blinkDuration = my_random(36000, 72000); // ~1/28 - ~1/14 sec

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
        timeToNextBlink = blinkDuration * 3 + my_random1(4000000);
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
   
    //将动作、眨眼和虹膜大小处理成可渲染的值
    // Process motion, blinking and iris scale into renderable values   
    //运行 `python tablegen.py terminatorEye/sclera.png terminatorEye/iris.png terminatorEye/lid-upper-symmetrical.png terminatorEye/lid-lower-symmetrical.png terminatorEye/lid-upper.png terminatorEye/lid-lower.png` 并将输出重定向到 `terminatorEye.h` 文件。
    // python tablegen.py terminatorEye/sclera.png terminatorEye/iris.png terminatorEye/lid-upper-symmetrical.png terminatorEye/lid-lower-symmetrical.png terminatorEye/lid-upper.png terminatorEye/lid-lower.png > terminatorEye.h
    // Scale eye X/Y positions (0-1023) to pixel units used by drawEye()    //像素单位转换
    eyeX = map1(eyeX, 0, 1023, 0, SCLERA_WIDTH  - DISPLAY_SIZE);
    eyeY = map1(eyeY, 0, 1023, 0, SCLERA_HEIGHT - DISPLAY_SIZE);
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
if(is_track){
    int16_t sampleX = SCLERA_WIDTH / 2 - (eyeX / 2), // Reduce X influence
        sampleY = SCLERA_HEIGHT / 2 - (eyeY + IRIS_HEIGHT / 4);
    // Eyelid is slightly asymmetrical, so two readings are taken, averaged
    if (sampleY < 0)
        n = 0;
    else
         n = upper[sampleY * SCREEN_WIDTH + sampleX] +
             upper[ sampleY * SCREEN_WIDTH + (SCREEN_WIDTH - 1 - sampleX)] /
            2;
    uThreshold = (uThreshold * 3 + n) / 4; // Filter/soften motion
    // Lower eyelid doesn't track the same way, but seems to be pulled upward
    // by tension from the upper lid.
    lThreshold = 254 - uThreshold;
}
else uThreshold = lThreshold = 0;  // No tracking -- eyelids full open unless blink modifies them

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

// 动画状态结构体
typedef struct {
    int16_t startValue;  // 起始值
    int16_t endValue;    // 结束值
    uint64_t startTime;  // 开始时间
    int32_t duration;    // 动画持续时间（微秒）
    int16_t currentRange; // 当前允许的缩放值变化范围
    bool isActive;       // 动画是否激活
} SplitState;

// 全局动画状态
static SplitState split_state = {
    .startValue = IRIS_MIN,
    .endValue = IRIS_MAX,
    .currentRange = IRIS_MAX - IRIS_MIN,
    .isActive = false
};

// // 更新动画状态
// void update_split_state() {
//     if (!split_state.isActive) {
//         // 如果动画未激活，初始化新动画
//         split_state.startValue = oldIris;
//         split_state.endValue = my_random(IRIS_MIN, IRIS_MAX);
//         split_state.startTime = esp_timer_get_time();
//         split_state.duration = 10000000L; // 10秒
//         split_state.currentRange = IRIS_MAX - IRIS_MIN;
//         split_state.isActive = true;
//     } else {
//         // 如果动画激活，计算当前值并更新
//         uint64_t currentTime = esp_timer_get_time();
//         int32_t elapsed = currentTime - split_state.startTime;  //当前动画的时间

//         if (elapsed >= split_state.duration) {
//             // 动画完成，重置状态
//             split_state.isActive = false;
//             oldIris = split_state.endValue;
//         } else {
//             // 计算当前值
//             int16_t v = split_state.startValue + ((split_state.endValue - split_state.startValue) * elapsed) / split_state.duration;
//             if (v < IRIS_MIN) v = IRIS_MIN;
//             else if (v > IRIS_MAX) v = IRIS_MAX;

//             // 使用缓动曲线平滑动画
//             int32_t dt = elapsed;
//             int16_t e = ease[255 * dt / split_state.duration] + 1;
//             v = split_state.startValue + (((split_state.endValue - split_state.startValue) * e) / 256);

//             // 绘制当前帧
//             frame(v);
//         }
//     }
// }

static int i =0;
//虹膜缩放动画：通过递归函数split生成虹膜缩放动画，模拟瞳孔对光线的反应。使用时间插值实现平滑的缩放效果。
void split(
    int16_t  startValue, // 虹膜缩放的起始值
    int16_t  endValue,   // 虹膜缩放的结束值
    uint64_t startTime,  // 开始时间（使用`esp_timer_get_time()`获取）
    int32_t  duration,   // 动画持续时间（微秒）
    int16_t  range
) {    // 允许的缩放值变化范围
    if (range >= 8) { // 限制递归深度
        range    /= 2; // 将范围和时间分成两半
        duration /= 2;
        int16_t midValue = (startValue + endValue - range) / 2 + (esp_random() % range);
        uint64_t midTime = startTime + duration;
        split(startValue, midValue, startTime, duration, range); // 第一部分
        split(midValue, endValue, midTime, duration, range);     // 第二部分
    } else { // No more subdivisions, do iris motion...
        int32_t dt;     // Time since start of motion
        int16_t v;      // Interim value
        // uint32_t start_time = esp_timer_get_time();
        // ESP_LOGI(TAG, "split start");
        while ((dt = (esp_timer_get_time() - startTime)) < duration) {  //使用 esp_timer_get_time() 获取当前时间，并计算与 startTime 的时间差 dt。
            // 计算当前值
            v = startValue + (((endValue - startValue) * dt) / duration);   //根据时间差 dt 和总时间 duration，计算当前虹膜的缩放值 v
            if (v < IRIS_MIN) v = IRIS_MIN; // Clip just in case    确保 v 不会超出预定义的虹膜大小范围（IRIS_MIN 和 IRIS_MAX）。
            else if (v > IRIS_MAX) v = IRIS_MAX;
            frame(v); // Draw frame with interim iris scale value   调用 frame(v) 函数，使用计算出的 v 值绘制当前帧。
            // 分段延时，允许任务切换
           
        }
        //  uint32_t end_time = esp_timer_get_time();
        // ESP_LOGI(TAG, "split end, time: %lu us", end_time - start_time);
    }
}

//设置眼球位置
void task_eye_update(void *pvParameters) {
    uint8_t e; // Eye index, 0 to NUM_EYES-1
    startTime = esp_timer_get_time(); // For frame-rate calculation
    for(e=0; e<NUM_EYES; e++) {
        eye[e].blink.state = NOBLINK;
    // If project involves only ONE eye and NO other SPI devices, its
    // select line can be permanently tied to GND and corresponding pin
    // in config.h set to -1.  Best to use it though.
    }

    while(1){
        ESP_LOGI(TAG,"EYE_Task...");
        newIris = my_random(IRIS_MIN, IRIS_MAX);    //
        split(oldIris, newIris, esp_timer_get_time(), 5000000L, IRIS_MAX - IRIS_MIN);  //
        //  // 更新动画状态
        //  update_split_state();

         vTaskDelay(10 / portTICK_PERIOD_MS); // 确保任务不卡住
        // vTaskDelay(1);
    }
}

 
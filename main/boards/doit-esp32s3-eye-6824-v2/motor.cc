/*  舵机驱动极简改动版，基于官方 ledc_fade_example  */
#include "motor.h"
#include "esp_log.h"

/* ---------- 用户可调 ---------- */
#define SERVO_GPIO 39 // 任意 GPIO，这里沿用原例 GPIO4
#define SERVO_LEDC_SPEED LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_TIMER LEDC_TIMER_1
#define SERVO_LEDC_CHANNEL LEDC_CHANNEL_2
#define SERVO_FREQ 50                // 50 Hz → 20 ms 周期
#define SERVO_RESO LEDC_TIMER_14_BIT // 14 位分辨率
/* 占空比数值计算：
   20 ms 周期，14 位分辨率 → 2^14 = 16384 档
   0.5 ms → 0.5/20*16384 ≈ 410
   2.5 ms → 2.5/20*16384 ≈ 2048
*/
#define SERVO_MIN_DUTY 410
#define SERVO_MAX_DUTY 2048
/* ------------------------------ */

static void pwm_init(gpio_num_t gpio)
{
    /* 1. 定时器：50 Hz，14 位 */
    ledc_timer_config_t timer = {
        .speed_mode = SERVO_LEDC_SPEED,
        .duty_resolution = SERVO_RESO,
        .timer_num = SERVO_LEDC_TIMER,
        .freq_hz = SERVO_FREQ,
        .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .gpio_num = SERVO_GPIO,
        .speed_mode = SERVO_LEDC_SPEED,
        .channel = SERVO_LEDC_CHANNEL,
        .timer_sel = SERVO_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags = {
            .output_invert = 0}};
    ledc_channel_config(&ch);
}

/* 把角度 0~180 映射到占空比 */
uint32_t angle_to_duty(int angle)
{
    if (angle < 0)
        angle = 0;
    if (angle > 180)
        angle = 180;
    return SERVO_MIN_DUTY +
           (SERVO_MAX_DUTY - SERVO_MIN_DUTY) * angle / 180;
}

void motor_set_duty(uint32_t duty)
{
    ledc_set_duty(SERVO_LEDC_SPEED, SERVO_LEDC_CHANNEL, duty);
    ledc_update_duty(SERVO_LEDC_SPEED, SERVO_LEDC_CHANNEL);
    ESP_LOGI("TEST", "set duty %lu\n", duty);
}

void motor_init(gpio_num_t gpio)
{
    pwm_init(gpio);

    /* 简单演示：0° → 90° → 180° → 0° 循环 */
    // int angle[4] = {0, 90, 180, 0};
    // int idx = 0;

    // while (true)
    // {
    //     uint32_t duty = angle_to_duty(angle[idx]);
    //     ledc_set_duty(SERVO_LEDC_SPEED, SERVO_LEDC_CHANNEL, duty);
    //     ledc_update_duty(SERVO_LEDC_SPEED, SERVO_LEDC_CHANNEL);

    //     printf("set angle %d\n", angle[idx]);
    //     idx = (idx + 1) % 4;
    //     vTaskDelay(pdMS_TO_TICKS(1000));
    // }
}
#ifndef MOTOR_H
#define MOTOR_H
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"

void motor_init(gpio_num_t gpio);
uint32_t angle_to_duty(int angle);
void motor_set_duty(uint32_t duty);
#endif
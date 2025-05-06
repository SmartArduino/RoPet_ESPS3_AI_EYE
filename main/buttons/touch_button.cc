/*
 * @Description: 
 * @Author: 爱酱丶
 * @Date: 2025-04-07 17:59:03
 * @LastEditTime: 2025-04-10 17:03:07
 * @LastEditors: 爱酱丶
 */
#include "touch_button.h"
#include <esp_log.h>
#include "eye_display.h"
#include "eyes_data.h"


static const char *TAG = "Touch Button";
#define TOUCH_BUTTON_NUM     4 //触摸按钮的数量

#define TP4_NUM_4 TOUCH_PAD_NUM4
#define TP3_NUM_5 TOUCH_PAD_NUM5
#define TP2_NUM_6 TOUCH_PAD_NUM6
#define TP1_NUM_7 TOUCH_PAD_NUM7

static touch_button_handle_t button_handle[TOUCH_BUTTON_NUM];

/* Touch buttons channel array */
static const touch_pad_t channel_array[TOUCH_BUTTON_NUM] = {    //每个触摸按钮对应的硬件触摸通道。
    TP4_NUM_4,  // GPIO4
    TP3_NUM_5,  // GPIO5
    TP2_NUM_6,  // GPIO6
    TP1_NUM_7,  // GPIO7
};

/* Touch buttons channel sensitivity array */   //触摸通道灵敏度数组channel_sens_array，用于设置每个触摸通道的灵敏度。
static const float channel_sens_array[TOUCH_BUTTON_NUM] = {
    0.1F,
    0.1F,
    0.1F,
    0.1F,
};

//回调函数的方式注册按键事件
TouchButton::TouchButton() {
     /* Initialize Touch Element library */
     touch_elem_global_config_t global_config = TOUCH_ELEM_GLOBAL_DEFAULT_CONFIG();
     ESP_ERROR_CHECK(touch_element_install(&global_config));
     ESP_LOGI(TAG, "Touch element library installed");
     touch_button_global_config_t button_global_config = TOUCH_BUTTON_GLOBAL_DEFAULT_CONFIG();
     ESP_ERROR_CHECK(touch_button_install(&button_global_config));
     ESP_LOGI(TAG, "Touch button installed");
     for (int i = 0; i < TOUCH_BUTTON_NUM; i++) {
         touch_button_config_t button_config = {
             .channel_num = channel_array[i],
             .channel_sens = channel_sens_array[i]
         };
         /* Create Touch buttons */
         ESP_ERROR_CHECK(touch_button_create(&button_config, &button_handle[i]));
         /* Subscribe touch button events (On Press, On Release, On LongPress) */
         ESP_ERROR_CHECK(touch_button_subscribe_event(button_handle[i],
                                                      TOUCH_ELEM_EVENT_ON_PRESS | TOUCH_ELEM_EVENT_ON_RELEASE | TOUCH_ELEM_EVENT_ON_LONGPRESS,
                                                      (void *)channel_array[i]));
                                                      /* Set EVENT as the dispatch method */
        /* Set EVENT as the dispatch method */                               
        ESP_ERROR_CHECK(touch_button_set_dispatch_method(button_handle[i], TOUCH_ELEM_DISP_CALLBACK));
        /* Register a handler function to handle event messages */
        ESP_ERROR_CHECK(touch_button_set_callback(button_handle[i], handleButtonEvent));

         /* Set LongPress event trigger threshold time */
         ESP_ERROR_CHECK(touch_button_set_longpress(button_handle[i], 2000));
    }
    ESP_LOGI(TAG, "Touch buttons created");

    touch_element_start();
    ESP_LOGI(TAG, "Touch element library start");

}
/*
    按钮
*/
void TouchButton::handleButtonEvent(touch_button_handle_t out_handle, touch_button_message_t *out_message, void *arg) {
    (void) out_handle; //Unused
    switch ((int)arg) {
        case TP4_NUM_4:
        if (out_message->event == TOUCH_BUTTON_EVT_ON_PRESS) {  
            //处理按钮按下的逻辑
            eyeNewX = my_random1(1024);  eyeNewY =my_random1(1024);
            is_track = false;
            ESP_LOGI(TAG, "eyeX=[%d],eyeNewY=[%d]", eyeNewX,eyeNewY);
            ESP_LOGI(TAG, "Button[%d] Press", (int)arg);
        } else if (out_message->event == TOUCH_BUTTON_EVT_ON_RELEASE) { 
            
             //处理按钮松开的逻辑
            ESP_LOGI(TAG, "Button[%d] Release", (int)arg);
        } else if (out_message->event == TOUCH_BUTTON_EVT_ON_LONGPRESS) {   
            //处理按钮长按的逻辑
            ESP_LOGI(TAG, "Button[%d] LongPress", (int)arg);
        }
            break;
        case TP3_NUM_5:
        if (out_message->event == TOUCH_BUTTON_EVT_ON_PRESS) {  
            //处理按钮按下的逻辑
            eyeNewX = my_random1(1024);  eyeNewY =my_random1(1024);
            is_track = true;
            ESP_LOGI(TAG, "eyeX=[%d],eyeNewY=[%d]", eyeNewX,eyeNewY);
            ESP_LOGI(TAG, "Button[%d] Release", (int)arg);
        } else if (out_message->event == TOUCH_BUTTON_EVT_ON_RELEASE) { 
             //处理按钮松开的逻辑
            
            ESP_LOGI(TAG, "Button[%d] Release", (int)arg);
        } else if (out_message->event == TOUCH_BUTTON_EVT_ON_LONGPRESS) {   
            //处理按钮长按的逻辑
            ESP_LOGI(TAG, "Button[%d] LongPress", (int)arg);
        }
            break;
        case TP2_NUM_6:
        if (out_message->event == TOUCH_BUTTON_EVT_ON_PRESS) {  
            //处理按钮按下的逻辑
           
            ESP_LOGI(TAG, "Button[%d] Press", (int)arg);
        } else if (out_message->event == TOUCH_BUTTON_EVT_ON_RELEASE) { 
             //处理按钮松开的逻辑
             iris = iris_my;
            ESP_LOGI(TAG, "Button[%d] Release", (int)arg);
        } else if (out_message->event == TOUCH_BUTTON_EVT_ON_LONGPRESS) {   
            //处理按钮长按的逻辑
            ESP_LOGI(TAG, "Button[%d] LongPress", (int)arg);
        }
            break;
        case TP1_NUM_7:
        if (out_message->event == TOUCH_BUTTON_EVT_ON_PRESS) {  
            //处理按钮按下的逻辑
            iris = iris_cum2;
            ESP_LOGI(TAG, "Button[%d] Press", (int)arg);
        } else if (out_message->event == TOUCH_BUTTON_EVT_ON_RELEASE) { 
             //处理按钮松开的逻辑
             iris = iris_default;
            ESP_LOGI(TAG, "Button[%d] Release", (int)arg);
        } else if (out_message->event == TOUCH_BUTTON_EVT_ON_LONGPRESS) {   
            //处理按钮长按的逻辑
            ESP_LOGI(TAG, "Button[%d] LongPress", (int)arg);
        }
            break;
        default:
            printf("错误按键\n");
    }
    
}

// void Button::OnButton(touch_button_handle_t out_handle, touch_button_message_t *out_message, void *arg)
// {
//     (void) out_handle; //Unused
//     if (out_message->event == TOUCH_BUTTON_EVT_ON_PRESS) {
//         ESP_LOGI(TAG, "Button[%d] Press", (int)arg);
//     } else if (out_message->event == TOUCH_BUTTON_EVT_ON_RELEASE) {
//         ESP_LOGI(TAG, "Button[%d] Release", (int)arg);
//     } else if (out_message->event == TOUCH_BUTTON_EVT_ON_LONGPRESS) {
//         ESP_LOGI(TAG, "Button[%d] LongPress", (int)arg);
//     }
// }


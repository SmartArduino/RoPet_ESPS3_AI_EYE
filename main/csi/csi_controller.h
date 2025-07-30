
/*
    CSI控制器
*/

#include <string>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <esp_timer.h>

#include "application.h"
#include "assets/lang_config.h"
#include "board.h"
#include "mcp_server.h"
#include "doit_csi.h"

#define TAG "CSIController"

static EventGroupHandle_t csi_event_group_; // CSI事件组

class CSIController
{
private:
    enum CSI_State
    {
        STATE_IDLE,        // 空闲状态
        STATE_PRE_CALIB,   // 预校准状态
        STATE_CALIBRATING, // 校准状态
        STATE_CSI_RUNNING  // CSI运行状态
    };

    static inline CSI_State csi_state_ = STATE_IDLE; // 初始化状态机为空闲状态
    static const int STATE_MACHINE_DONE_BIT = 0x01;

    static void PreCountDown()
    {
        SetIsPreCalibRunning(true);
        ESP_LOGI(TAG, "开始倒计时");
        std::string code = "987654321";

        struct digit_sound
        {
            char digit;
            const std::string_view &sound;
        };
        static const std::array<digit_sound, 10> digit_sounds{{digit_sound{'0', Lang::Sounds::P3_0},
                                                               digit_sound{'1', Lang::Sounds::P3_1},
                                                               digit_sound{'2', Lang::Sounds::P3_2},
                                                               digit_sound{'3', Lang::Sounds::P3_3},
                                                               digit_sound{'4', Lang::Sounds::P3_4},
                                                               digit_sound{'5', Lang::Sounds::P3_5},
                                                               digit_sound{'6', Lang::Sounds::P3_6},
                                                               digit_sound{'7', Lang::Sounds::P3_7},
                                                               digit_sound{'8', Lang::Sounds::P3_8},
                                                               digit_sound{'9', Lang::Sounds::P3_9}}};

        for (const auto &digit : code)
        {
            auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
                                   [digit](const digit_sound &ds)
                                   { return ds.digit == digit; });
            if (it != digit_sounds.end())
            {
                auto &app = Application::GetInstance();
                app.PlaySound(it->sound);
            }
        }
    }

    static void CalibCountDown()
    {
        SetIsStartCalibRunning(true);
        start_csi_calib(); // 开始校准
        ESP_LOGI(TAG, "开始校准倒计时");
        std::string code = "987654321";

        struct digit_sound
        {
            char digit;
            const std::string_view &sound;
        };
        static const std::array<digit_sound, 10> digit_sounds{{digit_sound{'0', Lang::Sounds::P3_0},
                                                               digit_sound{'1', Lang::Sounds::P3_1},
                                                               digit_sound{'2', Lang::Sounds::P3_2},
                                                               digit_sound{'3', Lang::Sounds::P3_3},
                                                               digit_sound{'4', Lang::Sounds::P3_4},
                                                               digit_sound{'5', Lang::Sounds::P3_5},
                                                               digit_sound{'6', Lang::Sounds::P3_6},
                                                               digit_sound{'7', Lang::Sounds::P3_7},
                                                               digit_sound{'8', Lang::Sounds::P3_8},
                                                               digit_sound{'9', Lang::Sounds::P3_9}}};

        for (const auto &digit : code)
        {
            auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
                                   [digit](const digit_sound &ds)
                                   { return ds.digit == digit; });
            if (it != digit_sounds.end())
            {
                auto &app = Application::GetInstance();
                app.PlaySound(it->sound);
            }
        }
        stop_csi_calib(); // 停止校准
    }
    /*
        CSI状态
    */
    static void CSI_Task(void *arg)
    {
        while (true)
        {
            // CSI状态机
            switch (csi_state_)
            {
            case STATE_PRE_CALIB:
                if (!GetIsPreCalibRunning())
                {

                    PreCountDown();
                    csi_state_ = STATE_CALIBRATING; // 切换为开始校准
                }
                break;
            case STATE_CALIBRATING:
                if (!GetIsStartCalibRunning())
                {
                    CalibCountDown();
                    csi_state_ = STATE_CSI_RUNNING; // 切换为运行状态
                }
                break;
            case STATE_CSI_RUNNING:
                if (!GetIsCsiRadarRunning())
                {
                    ESP_LOGI(TAG, "CSI雷达开始运行");
                    start_csi_radar();
                    xEventGroupSetBits(csi_event_group_, STATE_MACHINE_DONE_BIT); // 设置事件位
                }
                break;
            default: // 默认空闲
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(g_send_data_interval));
        }
    }

    CSIController();
    ~CSIController();
    void RegisterMcpTools();

public:
    // 获取CSIController实例的静态方法
    static CSIController &GetInstance()
    {
        // 定义一个静态的CSIController实例
        static CSIController instance;
        // 返回该实例
        return instance;
    }

    void Init();
};

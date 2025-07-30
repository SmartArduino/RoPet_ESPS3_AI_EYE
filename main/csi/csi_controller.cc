
/*
    CSI控制器
*/

#include <esp_log.h>

#include "csi_controller.h"
#include "board.h"
#include "mcp_server.h"
#include "doit_csi.h"

#define TAG "CSIController"

CSIController::CSIController()
{
    RegisterMcpTools();
    ESP_LOGI(TAG, "CSI控制器已初始化并注册MCP工具");

    // xTaskCreate(CSI_Task, "CSI_Task", 4096, this, 5, NULL); // csi任务

    // 初始化事件组
    csi_event_group_ = xEventGroupCreate();
}

CSIController::~CSIController()
{
    ESP_LOGI(TAG, "CSI控制器已初始化并注册MCP工具");
}

void CSIController::Init()
{
    doit_csi_init(); // 初始化CSI
}

void CSIController::RegisterMcpTools()
{
    auto &mcp_server = McpServer::GetInstance();

    ESP_LOGI(TAG, "开始注册Eye MCP工具...");
    mcp_server.AddTool(
        // "self.radar.calibration",
        // "雷达校准，当收到用户说开始校准时，大模型大模型开始10秒倒计时，大模型回复内容：‘准备开始校准，请在10秒内离开房间’，其他的什么都不要回复！，只需要回复单引号的内容即可",
        "self.radar.calibration",
        "雷达校准，当收到用户说开始校准时，调用该函数",
        PropertyList(),
        [this](const PropertyList &properties) -> ReturnValue
        {
            // auto &app = Application::GetInstance();
            // app.CountDown();
            csi_state_ = STATE_PRE_CALIB;
            xEventGroupWaitBits(csi_event_group_, STATE_MACHINE_DONE_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
            return true;
        });

    ESP_LOGI(TAG, "Eye MCP工具注册完成");
}

#pragma once

#include "display/lcd_display.h"
/**
 * @brief Otto机器人GIF表情显示类
 * 继承LcdDisplay，添加GIF表情支持
 */
class AnimEyeDisplay : public SpiLcdDisplay
{
public:
    /**
     * @brief 构造函数，参数与SpiLcdDisplay相同
     */
    AnimEyeDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                   int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                   bool swap_xy, DisplayFonts fonts);

    virtual ~AnimEyeDisplay() = default;

    // 重写表情设置方法
    virtual void SetEmotion(const char *emotion) override;

    // 重写聊天消息设置方法
    virtual void SetChatMessage(const char *role, const char *content) override;


private:
    void SetupAnimContainer();
};
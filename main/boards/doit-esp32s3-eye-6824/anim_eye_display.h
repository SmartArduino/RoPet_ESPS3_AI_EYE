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

#if CONFIG_USE_AVI_ANIM_EYE
    virtual void SetEyeTheme(const std::string &eye_theme_name) override;
#endif
#if CONFIG_SUPPORT_MINI_PROGRAMS_REPLACE_PSD
    void UpdateAnimContainer();
    void SetPSD(uint8_t psd_order);
#endif

    lv_obj_t *psd_container_ = nullptr;
    lv_obj_t *psd_obj_ = nullptr;

private:
    void SetupAnimContainer();

};
#include "anim_eye_display.h"

#include <vector>
#include <algorithm>
#include <font_awesome_symbols.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_lvgl_port.h>
#include <esp_heap_caps.h>
#include "assets/lang_config.h"
#include <cstring>
#include "settings.h"

#include "board.h"


#define TAG "AnimEyeDisplay"


AnimEyeDisplay::AnimEyeDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                               int width, int height, int offset_x, int offset_y, bool mirror_x,
                               bool mirror_y, bool swap_xy, DisplayFonts fonts)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy,
                    fonts)
{
    SetupAnimContainer();
};

void AnimEyeDisplay::SetupAnimContainer()
{
}

void AnimEyeDisplay::SetEmotion(const char *emotion)
{
}

void AnimEyeDisplay::SetChatMessage(const char *role, const char *content)
{
}



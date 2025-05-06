#ifndef TouchButton_H
#define TouchButton_H

#include <string>
#include <functional>
#include "touch_element/touch_button.h"
class TouchButton {
public:
    TouchButton();
    static void handleButtonEvent(touch_button_handle_t out_handle, touch_button_message_t *out_message, void *arg);
// protected:
    //std::function<void(const std::string& message)> on_button_event;
};

#endif
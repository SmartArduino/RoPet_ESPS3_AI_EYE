#include "doit_nvs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <esp_log.h>

static nvs_handle_t min_program_handle;

void doit_nvs_set(const char *type)
{
    nvs_open("min_program", NVS_READWRITE, &min_program_handle);
    if (strcmp(type, "jpg") == 0)
    {
        nvs_set_i32(min_program_handle, "min_program_img", 1);
        nvs_set_i32(min_program_handle, "min_program_vpg", 0);
    }
    else if (strcmp(type, "vpg") == 0)
    {
        nvs_set_i32(min_program_handle, "min_program_img", 0);
        nvs_set_i32(min_program_handle, "min_program_vpg", 1);
    }
}

char *doit_nvs_get(void)
{
    nvs_open("min_program", NVS_READWRITE, &min_program_handle);

    int32_t value = 0;
    nvs_get_i32(min_program_handle, "min_program_img", &value);
    if (value)
        return "jpg";

    nvs_get_i32(min_program_handle, "min_program_vpg", &value);
    if (value)
        return "vpg";

    return "vpg";
}

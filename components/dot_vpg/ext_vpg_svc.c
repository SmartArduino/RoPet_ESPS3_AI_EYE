#include "ext_vpg_svc.h"
#include "inc_decode.h"
#include "inc_vpg_decode.h"
#include "inc_img_decode.h"
#include "dv_common.h"

void dot_vpg_decode_init(void) {
    inc_decode_init();
}

void dot_vpg_decode_deinit() {
    inc_decode_deinit();
}

void dot_img_show(const char *dir_name) {
    inc_img_decode(dir_name);
}

void dot_vpg_start(const char *dir_name) {
    inc_vpg_player_start(dir_name);
}

void dot_vpg_stop() {
    inc_vpg_player_stop();
}

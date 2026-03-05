#ifndef __EXT_NIMBLE_SVC_H__
#define __EXT_NIMBLE_SVC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "inc_custom_svc.h"

/* ext func */
void dot_nimble_init(cust_nimble_get_info_cb cb);
void dot_nimble_deinit(void);
bool dot_send_str_to_phone(const char *str);

#ifdef __cplusplus
}
#endif

#endif // __EXT_NIMBLE_SVC_H__
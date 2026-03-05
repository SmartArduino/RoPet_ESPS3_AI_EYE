#ifndef __EXT_FS_H__
#define __EXT_FS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "dff_common.h"

void dot_fat_init(void);
void dot_fat_deinit(void);
void dot_fat_org_filder(void);
bool dot_fat_is_file_size_overflow(uint32_t file_size);

const char *dot_fs_get_file_name_by_index(uint8_t index);
const char *dot_fat_get_cur_file_type(void);
const char *dot_fs_next_file(void);
uint32_t dot_fs_get_file_count(void);
const char *dot_fs_get_current_file_name(void);
bool dot_fs_delete_current_node_and_file(void);

// wl_handle_t *get_wl_handle(void);
uint32_t dir_size_bytes(const char *path);

#ifdef __cplusplus
}
#endif

#endif // __FS__
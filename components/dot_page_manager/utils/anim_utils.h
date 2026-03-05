#ifndef __ANIM_UTILS_H__
#define __ANIM_UTILS_H__

#include "page_manager.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 执行页面切换动画 */
void anim_utils_page_switch(page_t *old_page, page_t *new_page, 
                           anim_type_t anim, uint32_t time);


#ifdef __cplusplus
}
#endif
#endif /* ANIM_UTILS_H */
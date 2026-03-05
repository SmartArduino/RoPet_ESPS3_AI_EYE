#include "page_manager.h"
#include "anim_utils.h"
#include "page_cfg.h"
#include "log_conf.h"
#include "esp_lvgl_port.h"
#include <esp_heap_caps.h>

static page_manager_t s_pm = {0}; // 页面管理器唯一实例

/* 栈工具 */
static bool st_push(page_t *page) {
    if (s_pm.stack_top >= PAGE_HISTORY_DEPTH) {
        PM_LOGW("页面历史记录栈已满，无法入栈");
        return false;
    }

    s_pm.page_stack[s_pm.stack_top++] = page;
    PM_LOGW("push to stack:%s (top=%d)\n", page->name, s_pm.stack_top);
    return true;
}

static page_t *st_pop(void) {
    return s_pm.stack_top ? s_pm.page_stack[--s_pm.stack_top] : NULL;
}

static void st_clear(void) {
    s_pm.stack_top = 0;
}

static page_t *find_page(const char *name) {
    for (page_t *p = s_pm.pages; p; p = p->next)
        if (strcmp(p->name, name) == 0) return p;
    return NULL;
}

/**
 * @brief 释放page_stash结构体占用的内存资源
 *
 * @param stash 指向要释放的page_stash结构体的指针
 *
 * 该函数会释放stash结构体中ptr指向的内存空间，
 * 并将ptr设置为NULL，size设置为0。
 * 如果stash为NULL，函数直接返回。
 */
static void page_stash_free(page_stash_t *stash) {
    if (!stash) return;
    if (stash->ptr) {
        free(stash->ptr);
        stash->ptr = NULL;
        stash->size = 0;
    }
}

/**
 * @brief 设置页面的stash结构体
 *
 * @param page 指向要设置stash的page结构体的指针
 * @param stash 指向page_stash结构体的指针
 *
 * 该函数会释放stash结构体中ptr指向的内存空间，
 * 并将ptr设置为NULL，size设置为0。
 * 如果stash为NULL，函数直接返回。
 */
static void page_stash_set(page_t *p, const void *buf, uint32_t len) {
    if (!p) {
        PM_LOGE("page is NULL,skip page_stash_set");
        return;
    }

    /* 1.先释放旧数据 */
    if (p->stash && p->stash->ptr) {
        free(p->stash->ptr);
        p->stash->ptr = NULL;
        p->stash->size = 0;
    }

    /* 2. 拷贝新数据 */
    if (!buf || len == 0) {
        PM_LOGW("stash->ptr is NULL, can't set stash");
        return;
    }

    void *ptr = heap_caps_malloc(len, MALLOC_CAP_8BIT);
    memcpy(ptr, buf, len);

    if (!p->stash) {
        p->stash = (page_stash_t *)heap_caps_malloc(sizeof(page_stash_t), MALLOC_CAP_8BIT);
        p->stash->ptr = heap_caps_malloc(len, MALLOC_CAP_8BIT);
    }
    memcpy(p->stash->ptr, buf, len);
    p->stash->size = len;
}

/* ---------- 执行切换 ---------- */
static void perform_switch(page_t *old, page_t *new_page, anim_type_t anim) {
    if (anim == ANIM_TYPE_NONE) {
        if (old) {
            lvgl_port_lock(-1);
            lv_obj_add_flag(old->obj, LV_OBJ_FLAG_HIDDEN);
            lvgl_port_unlock();
            if (old->onHide) old->onHide(old);
        }
        lvgl_port_lock(-1);
        lv_obj_clear_flag(new_page->obj, LV_OBJ_FLAG_HIDDEN);
        lvgl_port_unlock();
        if (new_page->onShow) new_page->onShow(new_page);
    } else {
        anim_utils_page_switch(old, new_page, anim, s_pm.anim_time);
    }
}

static page_t *page_manager_create_page(const char *name) {
    page_t *page = (page_t *)malloc(sizeof(page_t));
    if (!page) {
        PM_LOGE("页面创建失败，内存不足");
        return NULL;
    }
    memset(page, 0, sizeof(page_t));
    page->name = name;
    page->state = PAGE_STATE_IDLE;
    page->stash = NULL;
    return page;
}

static void page_manager_register_page(page_t *page) {
    page->next = s_pm.pages;
    s_pm.pages = page;
    PM_LOGI(">>>Page %s registered\n", page->name);
}

/* 页面管理器初始化 */
void page_manager_init(lv_obj_t *screen, uint16_t width, uint16_t height) {
    PM_LOGI("页面管理器初始化");
    memset(&s_pm, 0, sizeof(page_manager_t));
    s_pm.anim_type = ANIM_TYPE_NONE;
    s_pm.anim_time = 300; // 默认动画时间300ms
    st_clear();

    s_pm.screen_param = (screen_param_t *)malloc(sizeof(screen_param_t));
    s_pm.screen_param->width_ = width;
    s_pm.screen_param->height_ = height;
    s_pm.screen_param->screen = screen ? screen : lv_obj_create(NULL); // 如果有传
    lvgl_port_lock(-1);
    lv_screen_load(s_pm.screen_param->screen);
    lvgl_port_unlock();

    /* 注册页面 */
    PM_LOGI("页面管理器初始化-注册页面");
    page_install();

    page_manager_switch_to(page_info_get(PAGE_ID_HOME)->name, ANIM_TYPE_NONE); // 开启切换到默认页面
}

/* 获取页面管理器实例 */
page_manager_t *page_manager_get(void) {
    return &s_pm;
}

void page_manager_register_impl(const char *name, const page_impl_t *impl) {
    page_t *p = page_manager_create_page(name);
    if (!p) return;

    // 把生命周期函数“装配”进去
    p->onCreate = impl->onCreate;
    p->onCreated = impl->onCreated;
    p->onShow = impl->onShow;
    p->onHide = impl->onHide;
    p->onDestroy = impl->onDestroy;

    page_manager_register_page(p);
}

/* ---------- 主切换函数 ---------- */
bool page_manager_switch_to(const char *name, anim_type_t anim) {
    return page_manager_switch_to_ex(name, anim, NULL, 0);
}

/**
 * 不入栈的切换
 */
bool page_manager_switch_to_nohist(const char *name, anim_type_t anim) {
    PM_LOGI("Switch to page: %s\n", name);
    page_t *tgt = find_page(name);
    if (!tgt) {
        PM_LOGW("Page %s not found", name);
        return false;
    }
    if (tgt == s_pm.current_page) {
        /* 同页重复进入：也允许更新参数，然后重走 onShow */
        PM_LOGI("Already in page %s\n", name);
        return true;
    }

    /* 切换时还未创建页面 */
    if (!tgt->obj) {
        if (tgt->onCreate) tgt->onCreate(tgt);
    }

    /* 真正切换 */
    perform_switch(s_pm.current_page, tgt, anim);

    s_pm.current_page = tgt;
    tgt->state = PAGE_STATE_ACTIVE;
    return true;
}

bool page_manager_switch_to_ex(const char *name, anim_type_t anim, const void *param, size_t param_size) {
    PM_LOGI("Switch to page: %s\n", name);
    page_t *tgt = find_page(name);
    if (!tgt) {
        PM_LOGW("Page %s not found", name);
        return false;
    }
    if (tgt == s_pm.current_page) {
        /* 同页重复进入：也允许更新参数，然后重走 onShow */
        PM_LOGI("Already in page %s\n", name);
        page_stash_set(tgt, param, param_size);
        perform_switch(s_pm.current_page, tgt, anim);
        return true;
    }

    /* 先把参数拷贝进目标页（旧参数会释放） */
    page_stash_set(tgt, param, param_size);

    /* 切换时还未创建页面 */
    if (!tgt->obj) {
        // lvgl_port_lock(-1);
        // tgt->state = PAGE_STATE_LOADING;
        // tgt->obj = lv_obj_create(lv_screen_active());
        // lv_obj_set_size(tgt->obj, LV_HOR_RES, LV_VER_RES);
        // lv_obj_clear_flag(tgt->obj, LV_OBJ_FLAG_SCROLLABLE);
        // lv_obj_add_flag(tgt->obj, LV_OBJ_FLAG_HIDDEN);
        // lvgl_port_unlock();
        if (tgt->onCreate) tgt->onCreate(tgt);
    }

    /* 真正切换 */
    perform_switch(s_pm.current_page, tgt, anim);

    /* 把历史页面压入栈 */
    if (s_pm.current_page && s_pm.current_page != tgt)
        st_push(s_pm.current_page);

    s_pm.current_page = tgt;
    tgt->state = PAGE_STATE_ACTIVE;
    return true;
}

/* ---------- 返回 ---------- */
bool page_manager_go_back(anim_type_t anim) {
    page_t *back = st_pop();
    PM_LOGI("go back to %s\n", back ? back->name : "NULL");
    return back ? page_manager_switch_to(back->name, anim) : false;
}

/* ---------- 设置默认动画 ---------- */
void page_manager_set_default_anim(anim_type_t anim, uint32_t time) {
    s_pm.anim_type = anim;
    s_pm.anim_time = time;
}

/* ---------- 把指定页面从栈清除 ---------- */
void page_manager_remove_from_stack(page_t *page) {
    uint8_t wp = 0;
    for (uint8_t rp = 0; rp < s_pm.stack_top; ++rp)
        if (s_pm.page_stack[rp] != page)
            s_pm.page_stack[wp++] = s_pm.page_stack[rp];
    s_pm.stack_top = wp;
}

/* 暂时写法 */

void print_page_info(page_t *page) {
    if (!page) {
        PM_LOGI("页面链表为空\n");
        return;
    }

    PM_LOGI("页面信息：\n");
    while (page) {
        PM_LOGI("页面名称：%s\n", page->name);
        PM_LOGI("页面对象：%p\n", page->obj); // 打印指针地址
        PM_LOGI("页面状态：");

        // 根据页面状态打印对应的描述
        switch (page->state) {
        case PAGE_STATE_IDLE:
            PM_LOGI("空闲\n");
            break;
        case PAGE_STATE_LOADING:
            PM_LOGI("加载中\n");
            break;
        case PAGE_STATE_ACTIVE:
            PM_LOGI("活跃\n");
            break;
        default:
            PM_LOGI("未知状态\n");
            break;
        }

        // 如果有生命周期函数，打印函数指针地址（可选）
        if (page->onCreate) {
            PM_LOGI("onCreate 函数：%p\n", page->onCreate);
        }
        if (page->onCreated) {
            PM_LOGI("onCreated 函数：%p\n", page->onCreated);
        }
        if (page->onShow) {
            PM_LOGI("onShow 函数：%p\n", page->onShow);
        }
        if (page->onHide) {
            PM_LOGI("onHide 函数：%p\n", page->onHide);
        }
        if (page->onDestroy) {
            PM_LOGI("onDestroy 函数：%p\n", page->onDestroy);
        }

        // 打印分隔符
        PM_LOGI("------------------------\n");

        // 移动到下一个页面
        page = page->next;
    }
}

/**
 * 给外部获取显示动画的对象
 */
lv_obj_t *page_manager_get_home_img_obj(void) {
    page_t *p = find_page(page_info_get(PAGE_ID_HOME)->name);
    // print_page_info(p); // 打印页面信息
    if (p->second_obj) {
        PM_LOGE("get home obj =%p", p->second_obj);
        return p->second_obj;
    }
    PM_LOGW("home obj is null");
    return NULL;
}
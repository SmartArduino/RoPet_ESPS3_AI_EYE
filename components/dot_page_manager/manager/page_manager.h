#ifndef __PAGE_MANAGER__
#define __PAGE_MANAGER__

#ifdef __cplusplus
extern "C" {
#endif

/* include */
#include "lvgl.h"

#define PAGE_HISTORY_DEPTH 8 // 页面历史记录深度

/**
 * @brief 页面状态枚举
 *
 */
typedef enum {
    PAGE_STATE_IDLE,     // 空闲
    PAGE_STATE_LOADING,  // 加载中
    PAGE_STATE_ACTIVE,   // 活动
    PAGE_STATE_UNLOADING // 卸载中
} page_state_t;

/**
 * @brief 页面切换动画类型
 *
 */
typedef enum {
    ANIM_TYPE_NONE,
    ANIM_TYPE_SLIDE_LEFT,
    ANIM_TYPE_SLIDE_RIGHT,
    ANIM_TYPE_FADE
} anim_type_t;

// 页面数据块
typedef struct
{
    void *ptr;
    uint32_t size;
} page_stash_t;

/**
 * @brief 页面结构体
 *
 */
typedef struct page {
    const char *name;     // 页面名称
    lv_obj_t *obj;        // 页面对象
    lv_obj_t *second_obj; // 需要向外传递的控件
    page_state_t state;   // 页面状态
    page_stash_t *stash;  // 随着页面切换可能会带的用户数据

    /* 页面的生命周期函数 */
    void (*onCreate)(struct page *self);  // 创建
    void (*onCreated)(struct page *self); // 创建完成
    void (*onShow)(struct page *self);    // 显示
    void (*onHide)(struct page *self);    // 隐藏
    void (*onDestroy)(struct page *self); // 销毁
    struct page *next;                    // 链表指针
} page_t;

typedef struct screen_param {
    lv_obj_t *screen;
    uint16_t width_;
    uint16_t height_;
} screen_param_t;

/* 生命周期钩子，必须实现 */
typedef struct {
    void (*onCreate)(page_t *page);  /* 可选，可置 NULL */
    void (*onCreated)(page_t *page); /* 可选，可置 NULL */
    void (*onShow)(page_t *page);    /* 可选，可置 NULL */
    void (*onHide)(page_t *page);    /* 可选，可置 NULL */
    void (*onDestroy)(page_t *page); /* 可选，可置 NULL */
} page_impl_t;

typedef struct {
    page_t *pages;        // 页面链表头
    page_t *current_page; // 当前页面

    page_t *page_stack[PAGE_HISTORY_DEPTH]; // 页面历史记录栈
    uint8_t stack_top;                      // 栈顶索引

    anim_type_t anim_type;        // 默认动画类型
    uint32_t anim_time;           // 默认动画时间
    screen_param_t *screen_param; // 外部传入屏幕参数

} page_manager_t;

/* 函数声明 */

/**
 * @brief 页面栈初始化
 *
 */
void page_manager_init(lv_obj_t *screen, uint16_t width, uint16_t height);

/**
 * @brief 获取页面管理器实例
 *
 * @return page_manager_t*
 */
page_manager_t *page_manager_get(void);

void page_manager_register_impl(const char *name, const page_impl_t *impl);

// /**
//  * @brief 页面管理器-创建页面
//  *
//  * @param name
//  * @return page_t*
//  */
// page_t *page_manager_create_page(const char *name);

// /**
//  * @brief 页面管理器-注册页面-尾插法,依次注册 A → B → C :pages ──> C ──> B ──> A ──> NULL
//  *
//  * @param page
//  */
// void page_manager_register_page(page_t *page);

/**
 * @brief 页面管理器-切换页面
 *
 * @param name
 * @param anim
 * @return true
 * @return false
 */
bool page_manager_switch_to(const char *name, anim_type_t anim);

bool page_manager_switch_to_nohist(const char *name, anim_type_t anim);

/**
 * @brief 页面管理器-切换页面
 *
 * @param name
 * @param anim
 * @param user_data
 * @return true
 * @return false
 */
bool page_manager_switch_to_ex(const char *name, anim_type_t anim, const void *param, size_t param_size);

/**
 * @brief 页面管理器-返回上一页
 *
 * @param anim
 * @return true
 * @return false
 */
bool page_manager_go_back(anim_type_t anim);

/**
 * @brief 页面管理器-设置默认动画
 *
 * @param anim
 * @param time
 */
void page_manager_set_default_anim(anim_type_t anim, uint32_t time);

lv_obj_t *page_manager_get_home_img_obj(void);

/* 内部维护：页面销毁时把自己从栈里摘掉 */
void page_manager_remove_from_stack(page_t *page);

#ifdef __cplusplus
}
#endif

#endif // __PAGE_MANAGER__
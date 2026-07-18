#ifndef __MJC_DEFINE_H
#define __MJC_DEFINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mjc_item_t mjc_item_t;
typedef struct mjc_page_t mjc_page_t;

typedef enum {
    MJC_EVENT_EXIT,       // 退出该项
    MJC_EVENT_CHANGE,     // 值改变（number/checkbox）UI 交互时触发
    MJC_EVENT_TRIGGER,     // 触发事件（如按钮按下）
    MJC_EVENT_CUSTOM,      // 自定义事件
} mjc_event_type_t;

typedef void (*mjc_action_cb_t)(mjc_item_t* item, mjc_event_type_t event, void* user_data);

typedef enum {
    MJC_ITEM_TYPE_LABEL,
    MJC_ITEM_TYPE_NUMBER,
    MJC_ITEM_TYPE_CHECKBOX,
    MJC_ITEM_TYPE_SUBMENU,
    MJC_ITEM_TYPE_ACTION,
    MJC_ITEM_TYPE_MAX
} mjc_item_type_t;

typedef enum {
    MJC_NUM_INT8,
    MJC_NUM_UINT8,
    MJC_NUM_INT16,
    MJC_NUM_UINT16,
    MJC_NUM_INT32,
    MJC_NUM_UINT32,
    MJC_NUM_FLOAT
} mjc_num_type_t;

typedef struct {
    void* value_ptr;
    mjc_num_type_t num_type;
    float step;
    float min;
    float max;
} mjc_number_config_t;

struct mjc_page_t {
    mjc_item_t* items;       // 当前页面的条目数组
    uint8_t count;           // 条目数量
    uint8_t selected_index; // 当前选中的条目索引
    mjc_page_t* parent_page; // 指向父页面，根页面置为 NULL
};

// 类型定义
struct mjc_item_t {
    const char* name;
    mjc_item_type_t type;
    union {
        uint8_t* checkbox;
        mjc_page_t* submenu; // 子菜单页面
        mjc_number_config_t number;
    } data;

    mjc_action_cb_t action;
    void* user_data;

};

#ifdef __cplusplus
}
#endif

#endif // !__MJC_DEFINE_H

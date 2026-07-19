/**
 * @file gui_app.c
 * @brief MujicaUI 启动菜单：底盘快捷操作 + 按参数表动态生成设置页
 *
 * Params 菜单：
 *   - 启动时遍历 param 注册表，按 name 首段前缀（`motor_` / `chassis_` …）分组
 *   - 每组一个子页，组内每项为 NUMBER，直接绑 param.data
 *   - 编辑 commit 后对 motor_* 调用 motor_apply_param()；chassis 在 update 热刷
 *   - 「Save Params」写 LFS /param.txt
 */
#include "gui_app.h"

#include <string.h>

#include "service/gui/MujicaUI_Lite/mjc_core.h"
#include "service/gui/MujicaUI_Lite/mjc_input_button.h"
#include "common/event/event.h"
#include "common/event/soft_timer.h"
#include "service/sys/sys_log.h"
#include "service/com/param.h"
#include "service/motion/chassis.h"
#include "service/motion/motor.h"

#define GUI_BTN_TICK_MS     (5U)    /* multi_button TICKS_INTERVAL */
#define GUI_UI_TICK_MS      (100U)  /* 菜单刷新 */

/* 动态参数页静态池（无堆；上限对齐 param 注册表） */
#define GUI_PARAM_GROUP_MAX     (12U)
#define GUI_PARAM_ITEM_MAX      (PARAM_MAX_COUNT)
#define GUI_GROUP_NAME_LEN      (16U)
#define GUI_ITEM_NAME_LEN       (24U)
/* Params 顶层：分组 submenu + Save + 提示 label */
#define GUI_PARAMS_MENU_EXTRA   (2U)
#define GUI_PARAMS_MENU_MAX     (GUI_PARAM_GROUP_MAX + GUI_PARAMS_MENU_EXTRA)

/* -------------------------------------------------------------------------- */
/* 菜单 action                                                                */
/* -------------------------------------------------------------------------- */
static void gui_action_save_param(mjc_item_t *item, mjc_event_type_t event, void *user_data)
{
    (void)item;
    (void)user_data;
    if (event != MJC_EVENT_TRIGGER) {
        return;
    }
    if (param_save() == 0) {
        sys_log_text(info, "GUI: param_save ok");
    } else {
        sys_log_text(error, "GUI: param_save failed");
    }
}

static void gui_action_chassis_stop(mjc_item_t *item, mjc_event_type_t event, void *user_data)
{
    (void)item;
    (void)user_data;
    if (event != MJC_EVENT_TRIGGER) {
        return;
    }
    chassis_stop();
    sys_log_text(info, "GUI: chassis_stop");
}

static void gui_action_chassis_idle(mjc_item_t *item, mjc_event_type_t event, void *user_data)
{
    (void)item;
    (void)user_data;
    if (event != MJC_EVENT_TRIGGER) {
        return;
    }
    chassis_set_mode(CHASSIS_MODE_IDLE);
    chassis_stop();
    sys_log_text(info, "GUI: chassis IDLE");
}

/**
 * NUMBER commit 后：motor PID 需显式 apply；chassis 在 chassis_update 内热刷。
 * user_data = 完整参数名（s_param_full_names 内）。
 */
static void gui_param_number_action(mjc_item_t *item, mjc_event_type_t event, void *user_data)
{
    const char *name = (const char *)user_data;

    (void)item;
    if (event != MJC_EVENT_CHANGE || name == NULL) {
        return;
    }

    if (strncmp(name, "motor_", 6) == 0) {
        motor_apply_param();
        sys_log_text(info, "GUI: motor_apply_param after %s", name);
    }
}

/* -------------------------------------------------------------------------- */
/* 静态页面：Chassis / Root                                                    */
/* -------------------------------------------------------------------------- */
static mjc_item_t s_chassis_items[] = {
    { .name = "Stop",  .type = MJC_ITEM_TYPE_ACTION, .action = gui_action_chassis_stop },
    { .name = "IDLE",  .type = MJC_ITEM_TYPE_ACTION, .action = gui_action_chassis_idle },
    { .name = "MAIN=OK AUX=Back", .type = MJC_ITEM_TYPE_LABEL },
};
static mjc_page_t s_chassis_page = {
    .items = s_chassis_items,
    .count = (uint8_t)(sizeof(s_chassis_items) / sizeof(s_chassis_items[0])),
    .selected_index = 0,
    .parent_page = NULL,
};

static mjc_item_t s_root_items[] = {
    { .name = "BaseProject 3519", .type = MJC_ITEM_TYPE_LABEL },
    { .name = "Chassis",          .type = MJC_ITEM_TYPE_SUBMENU },
    { .name = "Params",           .type = MJC_ITEM_TYPE_SUBMENU },
    { .name = "Save Params",      .type = MJC_ITEM_TYPE_ACTION, .action = gui_action_save_param },
    { .name = "Keys A30/A31/B0/B1", .type = MJC_ITEM_TYPE_LABEL },
};
static mjc_page_t s_root_page = {
    .items = s_root_items,
    .count = (uint8_t)(sizeof(s_root_items) / sizeof(s_root_items[0])),
    .selected_index = 0,
    .parent_page = NULL,
};

/* -------------------------------------------------------------------------- */
/* 动态参数页池                                                                */
/* -------------------------------------------------------------------------- */
typedef struct {
    char name[GUI_GROUP_NAME_LEN];
    uint16_t count;      /* pass1 计数 / pass3 写指针 / 最终条目数 */
    uint16_t item_base;  /* 在 s_param_items 中的起始下标 */
} gui_param_group_t;

static gui_param_group_t s_groups[GUI_PARAM_GROUP_MAX];
static uint8_t s_group_count;

static mjc_item_t s_param_items[GUI_PARAM_ITEM_MAX];
static char s_param_item_names[GUI_PARAM_ITEM_MAX][GUI_ITEM_NAME_LEN];
static char s_param_full_names[GUI_PARAM_ITEM_MAX][PARAM_NAME_MAX_LEN];
static uint16_t s_param_item_total;

static mjc_page_t s_group_pages[GUI_PARAM_GROUP_MAX];

static mjc_item_t s_params_menu_items[GUI_PARAMS_MENU_MAX];
static mjc_page_t s_params_page;

/* 无参数时的占位页 */
static mjc_item_t s_params_empty_items[] = {
    { .name = "(no params)", .type = MJC_ITEM_TYPE_LABEL },
    { .name = "AUX=Back",    .type = MJC_ITEM_TYPE_LABEL },
};

static void gui_extract_group(const char *name, char *out, size_t out_len)
{
    size_t i = 0;

    if (out == NULL || out_len == 0) {
        return;
    }
    if (name == NULL || name[0] == '\0') {
        strncpy(out, "misc", out_len - 1U);
        out[out_len - 1U] = '\0';
        return;
    }

    while (name[i] != '\0' && name[i] != '_' && (i + 1U) < out_len) {
        out[i] = name[i];
        i++;
    }
    if (i == 0U) {
        strncpy(out, "misc", out_len - 1U);
        out[out_len - 1U] = '\0';
        return;
    }
    out[i] = '\0';
}

static int gui_find_group(const char *group_name)
{
    uint8_t g;

    for (g = 0; g < s_group_count; g++) {
        if (strncmp(s_groups[g].name, group_name, GUI_GROUP_NAME_LEN) == 0) {
            return (int)g;
        }
    }
    return -1;
}

static mjc_num_type_t gui_map_param_type(param_type_t type)
{
    switch (type) {
    case PARAM_TYPE_UINT8:  return MJC_NUM_UINT8;
    case PARAM_TYPE_UINT16: return MJC_NUM_UINT16;
    case PARAM_TYPE_UINT32: return MJC_NUM_UINT32;
    case PARAM_TYPE_INT8:   return MJC_NUM_INT8;
    case PARAM_TYPE_INT16:  return MJC_NUM_INT16;
    case PARAM_TYPE_INT32:  return MJC_NUM_INT32;
    case PARAM_TYPE_FLOAT:  return MJC_NUM_FLOAT;
    default:                return MJC_NUM_FLOAT;
    }
}

static float gui_default_step(param_type_t type)
{
    return (type == PARAM_TYPE_FLOAT) ? 0.01f : 1.0f;
}

/** 屏上显示名：去掉首段前缀 `xxx_`，过长则截断 */
static void gui_fill_display_name(const char *full_name, char *out, size_t out_len)
{
    const char *src;
    size_t n;

    if (out == NULL || out_len == 0U) {
        return;
    }
    if (full_name == NULL) {
        out[0] = '\0';
        return;
    }

    src = strchr(full_name, '_');
    if (src != NULL && src[1] != '\0') {
        src = src + 1;
    } else {
        src = full_name;
    }

    n = strnlen(src, out_len - 1U);
    memcpy(out, src, n);
    out[n] = '\0';
}

/**
 * @brief 根据当前 param 注册表构建 Params 分组菜单
 * @return 1 成功（含空表占位）；0 池耗尽等硬失败
 * @note  须在全部 param_add 之后、mjc_init 之前调用
 */
static uint8_t gui_build_param_pages(void)
{
    uint16_t n = param_get_count();
    uint16_t i;
    uint8_t g;
    uint16_t base;
    char group_name[GUI_GROUP_NAME_LEN];

    s_group_count = 0;
    s_param_item_total = 0;
    memset(s_groups, 0, sizeof(s_groups));
    memset(s_param_items, 0, sizeof(s_param_items));
    memset(s_group_pages, 0, sizeof(s_group_pages));
    memset(s_params_menu_items, 0, sizeof(s_params_menu_items));

    if (n == 0U) {
        s_params_page.items = s_params_empty_items;
        s_params_page.count = (uint8_t)(sizeof(s_params_empty_items) / sizeof(s_params_empty_items[0]));
        s_params_page.selected_index = 0;
        s_params_page.parent_page = NULL;
        return 1;
    }

    if (n > GUI_PARAM_ITEM_MAX) {
        n = GUI_PARAM_ITEM_MAX;
    }

    /* Pass 1: 发现分组并计数 */
    for (i = 0; i < n; i++) {
        param_t p;
        int gi;

        if (param_get_by_index(i, &p) != 0 || p.data == NULL || p.type == PARAM_TYPE_NONE) {
            continue;
        }

        gui_extract_group(p.name, group_name, sizeof(group_name));
        gi = gui_find_group(group_name);
        if (gi < 0) {
            if (s_group_count >= GUI_PARAM_GROUP_MAX) {
                sys_log_text(warning, "GUI: param group full, skip %s", p.name);
                continue;
            }
            gi = (int)s_group_count;
            strncpy(s_groups[gi].name, group_name, GUI_GROUP_NAME_LEN - 1U);
            s_groups[gi].name[GUI_GROUP_NAME_LEN - 1U] = '\0';
            s_groups[gi].count = 0;
            s_groups[gi].item_base = 0;
            s_group_count++;
        }
        s_groups[gi].count++;
    }

    if (s_group_count == 0U) {
        s_params_page.items = s_params_empty_items;
        s_params_page.count = (uint8_t)(sizeof(s_params_empty_items) / sizeof(s_params_empty_items[0]));
        s_params_page.selected_index = 0;
        s_params_page.parent_page = NULL;
        return 1;
    }

    /* Pass 2: 分配连续切片 */
    base = 0;
    for (g = 0; g < s_group_count; g++) {
        s_groups[g].item_base = base;
        base = (uint16_t)(base + s_groups[g].count);
        s_groups[g].count = 0; /* 复用为写指针 */
    }
    s_param_item_total = base;

    /* Pass 3: 填充 NUMBER 条目 */
    for (i = 0; i < n; i++) {
        param_t p;
        int gi;
        uint16_t idx;
        mjc_item_t *it;

        if (param_get_by_index(i, &p) != 0 || p.data == NULL || p.type == PARAM_TYPE_NONE) {
            continue;
        }

        gui_extract_group(p.name, group_name, sizeof(group_name));
        gi = gui_find_group(group_name);
        if (gi < 0) {
            continue;
        }

        idx = (uint16_t)(s_groups[gi].item_base + s_groups[gi].count);
        if (idx >= GUI_PARAM_ITEM_MAX) {
            sys_log_text(error, "GUI: param item pool overflow");
            return 0;
        }

        gui_fill_display_name(p.name, s_param_item_names[idx], GUI_ITEM_NAME_LEN);
        strncpy(s_param_full_names[idx], p.name, PARAM_NAME_MAX_LEN - 1U);
        s_param_full_names[idx][PARAM_NAME_MAX_LEN - 1U] = '\0';

        it = &s_param_items[idx];
        it->name = s_param_item_names[idx];
        it->type = MJC_ITEM_TYPE_NUMBER;
        it->data.number.value_ptr = p.data;
        it->data.number.num_type = gui_map_param_type(p.type);
        it->data.number.step = gui_default_step(p.type);
        it->data.number.min = 0.0f; /* min==max → 不限幅（见 mjc_number_clamp） */
        it->data.number.max = 0.0f;
        it->action = gui_param_number_action;
        it->user_data = s_param_full_names[idx];

        s_groups[gi].count++;
    }

    /* 组装各组子页 + Params 顶层菜单 */
    for (g = 0; g < s_group_count; g++) {
        s_group_pages[g].items = &s_param_items[s_groups[g].item_base];
        s_group_pages[g].count = (uint8_t)s_groups[g].count;
        s_group_pages[g].selected_index = 0;
        s_group_pages[g].parent_page = NULL;

        s_params_menu_items[g].name = s_groups[g].name;
        s_params_menu_items[g].type = MJC_ITEM_TYPE_SUBMENU;
        s_params_menu_items[g].data.submenu = &s_group_pages[g];
        s_params_menu_items[g].action = NULL;
        s_params_menu_items[g].user_data = NULL;
    }

    s_params_menu_items[s_group_count].name = "Save Params";
    s_params_menu_items[s_group_count].type = MJC_ITEM_TYPE_ACTION;
    s_params_menu_items[s_group_count].action = gui_action_save_param;
    s_params_menu_items[s_group_count].user_data = NULL;

    s_params_menu_items[s_group_count + 1U].name = "MAIN edit AUX back";
    s_params_menu_items[s_group_count + 1U].type = MJC_ITEM_TYPE_LABEL;
    s_params_menu_items[s_group_count + 1U].action = NULL;
    s_params_menu_items[s_group_count + 1U].user_data = NULL;

    s_params_page.items = s_params_menu_items;
    s_params_page.count = (uint8_t)(s_group_count + GUI_PARAMS_MENU_EXTRA);
    s_params_page.selected_index = 0;
    s_params_page.parent_page = NULL;

    sys_log_text(info, "GUI: params menu groups=%u items=%u",
                 (unsigned)s_group_count, (unsigned)s_param_item_total);
    return 1;
}

/* -------------------------------------------------------------------------- */
/* 周期任务                                                                    */
/* -------------------------------------------------------------------------- */
static void gui_btn_task(const event_t *event, void *user_data)
{
    (void)event;
    (void)user_data;
    mjc_buttons_tick_5ms();
}

static void gui_ui_task(const event_t *event, void *user_data)
{
    (void)event;
    (void)user_data;
    (void)mjc_update();
}

static soft_timer_id_t gui_start_timer(uint32_t period_ms,
                                       void (*cb)(const event_t *, void *),
                                       const char *name)
{
    soft_timer_id_t id;

    id = soft_timer_create_simple(period_ms, TIMER_MODE_PERIODIC, cb,
                                  EVENT_DISPATCH_SYNC);
    if (id == 0) {
        sys_log_text(error, "GUI: %s timer create failed", name);
        return 0;
    }
    soft_timer_start(id);
    soft_timer_set_name(id, name);
    return id;
}

/* -------------------------------------------------------------------------- */
/* 对外接口                                                                    */
/* -------------------------------------------------------------------------- */
exit_code_t gui_app_init(void)
{
    /* 1) 按当前注册表动态生成 Params 页（须在全部 param_add 之后） */
    if (!gui_build_param_pages()) {
        sys_log_text(error, "GUI: build param pages failed");
        return EXIT_FAIL;
    }

    /* 2) 挂子菜单指针，mjc_init 会 walk parent 与默认 submenu action */
    s_root_items[1].data.submenu = &s_chassis_page;
    s_root_items[2].data.submenu = &s_params_page;

    if (!mjc_init(&s_root_page)) {
        sys_log_text(error, "GUI: mjc_init failed (IPS200?)");
        return EXIT_FAIL;
    }

    if (!mjc_add_submenu(&s_root_page, &s_root_items[1], &s_chassis_page)) {
        sys_log_text(error, "GUI: mjc_add_submenu Chassis failed");
        return EXIT_FAIL;
    }
    if (!mjc_add_submenu(&s_root_page, &s_root_items[2], &s_params_page)) {
        sys_log_text(error, "GUI: mjc_add_submenu Params failed");
        return EXIT_FAIL;
    }

    /* 各组子页 parent 已由 mjc_set_page_parent 递归设置；再显式绑一次更稳 */
    {
        uint8_t g;
        for (g = 0; g < s_group_count; g++) {
            if (!mjc_add_submenu(&s_params_page, &s_params_menu_items[g], &s_group_pages[g])) {
                sys_log_text(error, "GUI: mjc_add_submenu group %s failed", s_groups[g].name);
                return EXIT_FAIL;
            }
        }
    }

    if (!mjc_buttons_init()) {
        sys_log_text(error, "GUI: mjc_buttons_init failed");
        return EXIT_FAIL;
    }

    if (gui_start_timer(GUI_BTN_TICK_MS, gui_btn_task, "mjc_btn") == 0) {
        return EXIT_FAIL;
    }
    if (gui_start_timer(GUI_UI_TICK_MS, gui_ui_task, "mjc_ui") == 0) {
        return EXIT_FAIL;
    }

    /* 首帧立即绘制 */
    (void)mjc_update();

    sys_log_text(info, "GUI: IPS200 + keys A30/A31/B0/B1 ready");
    return EXIT_OK;
}

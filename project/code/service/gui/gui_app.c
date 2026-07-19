/**
 * @file gui_app.c
 * @brief MujicaUI 启动菜单：底盘快捷操作 + 参数保存
 */
#include "gui_app.h"

#include "service/gui/MujicaUI_Lite/mjc_core.h"
#include "service/gui/MujicaUI_Lite/mjc_input_button.h"
#include "common/event/event.h"
#include "common/event/soft_timer.h"
#include "service/sys/sys_log.h"
#include "service/com/param.h"
#include "service/motion/chassis.h"

#define GUI_BTN_TICK_MS     (5U)    /* multi_button TICKS_INTERVAL */
#define GUI_UI_TICK_MS      (100U)  /* 菜单刷新 */

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

/* -------------------------------------------------------------------------- */
/* 页面树（静态）                                                              */
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
    /* 先挂子菜单指针，mjc_init 会 walk parent 与默认 submenu action */
    s_root_items[1].data.submenu = &s_chassis_page;

    if (!mjc_init(&s_root_page)) {
        sys_log_text(error, "GUI: mjc_init failed (IPS200?)");
        return EXIT_FAIL;
    }

    if (!mjc_add_submenu(&s_root_page, &s_root_items[1], &s_chassis_page)) {
        sys_log_text(error, "GUI: mjc_add_submenu failed");
        return EXIT_FAIL;
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

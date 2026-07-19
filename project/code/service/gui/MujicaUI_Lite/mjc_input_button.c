#include "mjc_input_button.h"

#include "service/gui/multi_button.h"
#include "mjc_core.h"
#include "mjc_define.h"
#include "zf_driver_gpio.h"
#include "common/event/event.h"
#include "service/sys/sys_log.h"

#define MJC_BUTTON_ACTIVE_LEVEL 0
#define MJC_BUTTON_COUNT 4

enum {
    MJC_BTN_UP = 0,
    MJC_BTN_DOWN,
    MJC_BTN_MAIN,
    MJC_BTN_AUX
};

/*
 * TODO: remap keys before enabling with motors.
 * B12/B13 conflict with right motor PWM/DIR (see docs/hardware.md).
 */
static const gpio_pin_enum s_button_pins[MJC_BUTTON_COUNT] = { B13, B12, B14, B15 };
static Button s_buttons[MJC_BUTTON_COUNT];
static event_listener_id_t s_evt_press = EVENT_LISTENER_ID_INVALID;
static event_listener_id_t s_evt_click = EVENT_LISTENER_ID_INVALID;
static event_listener_id_t s_evt_hold = EVENT_LISTENER_ID_INVALID;
static event_listener_id_t s_evt_double = EVENT_LISTENER_ID_INVALID;
static uint8_t s_input_enabled = 1;

static void mjc_buttons_gpio_init(void)
{
    for (uint8_t i = 0; i < MJC_BUTTON_COUNT; i++) {
        gpio_init(s_button_pins[i], GPI, GPIO_HIGH, GPI_PULL_UP);
    }
}

static uint8_t read_button_level(uint8_t id)
{
    if (id >= MJC_BUTTON_COUNT) {
        return (uint8_t)!MJC_BUTTON_ACTIVE_LEVEL;
    }
    return gpio_get_level(s_button_pins[id]);
}

static void mjc_buttons_nav_enter(void)
{
    mjc_item_t* item = mjc_get_selected_item();
    if (item == NULL) {
        return;
    }

    if (item->type == MJC_ITEM_TYPE_NUMBER) {
        (void)mjc_edit_begin();
        return;
    }

    (void)mjc_item_execute(item, MJC_EVENT_TRIGGER);
}

static void mjc_buttons_handle(uint8_t id, ButtonEvent ev)
{
    if (mjc_is_edit_mode()) {
        switch (id) {
        case MJC_BTN_UP:
            if (ev == BTN_PRESS_DOWN || ev == BTN_LONG_PRESS_HOLD) {
                (void)mjc_edit_adjust(-1);
            } else if (ev == BTN_DOUBLE_CLICK) {
                (void)mjc_edit_scale_step(-1); /* finer step */
            }
            break;
        case MJC_BTN_DOWN:
            if (ev == BTN_PRESS_DOWN || ev == BTN_LONG_PRESS_HOLD) {
                (void)mjc_edit_adjust(+1);
            } else if (ev == BTN_DOUBLE_CLICK) {
                (void)mjc_edit_scale_step(+1); /* coarser step */
            }
            break;
        case MJC_BTN_MAIN:
            if (ev == BTN_SINGLE_CLICK) {
                (void)mjc_edit_commit();
            }
            break;
        case MJC_BTN_AUX:
            if (ev == BTN_SINGLE_CLICK) {
                (void)mjc_edit_cancel();
            }
            break;
        default:
            break;
        }
        return;
    }

    /* Browse mode */
    switch (id) {
    case MJC_BTN_UP:
        if (ev == BTN_PRESS_DOWN || ev == BTN_LONG_PRESS_HOLD) {
            (void)mjc_page_up();
        }
        break;
    case MJC_BTN_DOWN:
        if (ev == BTN_PRESS_DOWN || ev == BTN_LONG_PRESS_HOLD) {
            (void)mjc_page_down();
        }
        break;
    case MJC_BTN_MAIN:
        if (ev == BTN_SINGLE_CLICK) {
            mjc_buttons_nav_enter();
        }
        break;
    case MJC_BTN_AUX:
        if (ev == BTN_SINGLE_CLICK) {
            (void)mjc_page_back();
        }
        break;
    default:
        sys_log_text(warning, "btn unknown id=%u ev=%u", (unsigned)id, (unsigned)ev);
        break;
    }
}

static void mjc_on_button_event(const event_t* ev, void* user_data)
{
    (void)user_data;
    if (!s_input_enabled) {
        return;
    }
    if (ev == NULL || ev->data == NULL) {
        return;
    }

    if (ev->type < BUTTON_EVENT_BASE) {
        return;
    }

    uint16_t offset = (uint16_t)(ev->type - BUTTON_EVENT_BASE);
    if (offset >= (uint16_t)BTN_EVENT_COUNT) {
        return;
    }

    ButtonEvent bev = (ButtonEvent)offset;
    Button* btn = (Button*)ev->data;

    if (btn == NULL) {
        sys_log_text(error, "btn evt null data type=%u", (unsigned)ev->type);
        return;
    }

    mjc_buttons_handle(btn->button_id, bev);
}

void mjc_input_set_enabled(uint8_t enabled)
{
    s_input_enabled = enabled ? 1U : 0U;
}

uint8_t mjc_input_is_enabled(void)
{
    return s_input_enabled;
}

uint8_t mjc_buttons_init(void)
{
    mjc_buttons_gpio_init();

    for (uint8_t i = 0; i < MJC_BUTTON_COUNT; i++) {
        button_init(&s_buttons[i], read_button_level, MJC_BUTTON_ACTIVE_LEVEL, i);
        button_start(&s_buttons[i]);
    }

    s_evt_press = event_subscribe((event_type_t)(BUTTON_EVENT_BASE + BTN_PRESS_DOWN),
                                  mjc_on_button_event, NULL);
    s_evt_click = event_subscribe((event_type_t)(BUTTON_EVENT_BASE + BTN_SINGLE_CLICK),
                                  mjc_on_button_event, NULL);
    s_evt_hold = event_subscribe((event_type_t)(BUTTON_EVENT_BASE + BTN_LONG_PRESS_HOLD),
                                 mjc_on_button_event, NULL);
    s_evt_double = event_subscribe((event_type_t)(BUTTON_EVENT_BASE + BTN_DOUBLE_CLICK),
                                   mjc_on_button_event, NULL);

    if (s_evt_press == EVENT_LISTENER_ID_INVALID ||
        s_evt_click == EVENT_LISTENER_ID_INVALID ||
        s_evt_hold == EVENT_LISTENER_ID_INVALID ||
        s_evt_double == EVENT_LISTENER_ID_INVALID) {
        return 0;
    }

    s_input_enabled = 1;
    return 1;
}

void mjc_buttons_tick_5ms(void)
{
    button_ticks();
}

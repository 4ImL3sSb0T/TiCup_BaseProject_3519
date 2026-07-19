#include <stddef.h>
#include "mjc_core.h"
#include "mjc_define.h"
#include "mjc_render.h"

static mjc_page_t* g_current_page = NULL;

/* NUMBER edit session (buffer only; commit writes value_ptr) */
static uint8_t s_edit_mode = 0;
static mjc_item_t* s_edit_item = NULL;
static float s_edit_value = 0.0f;
static float s_edit_step = 1.0f;

#define MJC_EDIT_STEP_MIN  1e-3f
#define MJC_EDIT_STEP_MAX  1e6f

static void mjc_edit_clear(void)
{
    s_edit_mode = 0;
    s_edit_item = NULL;
    s_edit_value = 0.0f;
    s_edit_step = 1.0f;
}

static float mjc_number_read_float(const mjc_number_config_t* num)
{
    if (num == NULL || num->value_ptr == NULL) {
        return 0.0f;
    }
    switch (num->num_type) {
    case MJC_NUM_INT8:   return (float)(*(int8_t*)num->value_ptr);
    case MJC_NUM_UINT8:  return (float)(*(uint8_t*)num->value_ptr);
    case MJC_NUM_INT16:  return (float)(*(int16_t*)num->value_ptr);
    case MJC_NUM_UINT16: return (float)(*(uint16_t*)num->value_ptr);
    case MJC_NUM_INT32:  return (float)(*(int32_t*)num->value_ptr);
    case MJC_NUM_UINT32: return (float)(*(uint32_t*)num->value_ptr);
    case MJC_NUM_FLOAT:  return *(float*)num->value_ptr;
    default:             return 0.0f;
    }
}

static void mjc_number_write_float(const mjc_number_config_t* num, float v)
{
    if (num == NULL || num->value_ptr == NULL) {
        return;
    }
    switch (num->num_type) {
    case MJC_NUM_INT8:   *(int8_t*)num->value_ptr = (int8_t)v; break;
    case MJC_NUM_UINT8:
        if (v < 0.0f) {
            v = 0.0f;
        }
        *(uint8_t*)num->value_ptr = (uint8_t)v;
        break;
    case MJC_NUM_INT16:  *(int16_t*)num->value_ptr = (int16_t)v; break;
    case MJC_NUM_UINT16:
        if (v < 0.0f) {
            v = 0.0f;
        }
        *(uint16_t*)num->value_ptr = (uint16_t)v;
        break;
    case MJC_NUM_INT32:  *(int32_t*)num->value_ptr = (int32_t)v; break;
    case MJC_NUM_UINT32:
        if (v < 0.0f) {
            v = 0.0f;
        }
        *(uint32_t*)num->value_ptr = (uint32_t)v;
        break;
    case MJC_NUM_FLOAT:  *(float*)num->value_ptr = v; break;
    default: break;
    }
}

static float mjc_number_clamp(const mjc_number_config_t* num, float v)
{
    if (num == NULL) {
        return v;
    }
    /* Only clamp when min < max. min==max==0 (default) means unrestricted. */
    if (num->min < num->max) {
        if (v < num->min) {
            v = num->min;
        }
        if (v > num->max) {
            v = num->max;
        }
    }
    return v;
}

static void mjc_submenu_action(mjc_item_t* item, mjc_event_type_t event, void* user_data)
{
    (void)user_data;
    if (item == NULL) {
        return;
    }

    if (event == MJC_EVENT_TRIGGER) {
        if (item->type == MJC_ITEM_TYPE_SUBMENU && item->data.submenu != NULL) {
            mjc_set_current_page(item->data.submenu);
        }
    }
}

static void mjc_checkbox_action(mjc_item_t* item, mjc_event_type_t event, void* user_data)
{
    (void)user_data;
    if (item == NULL || item->type != MJC_ITEM_TYPE_CHECKBOX || item->data.checkbox == NULL) {
        return;
    }

    if (event == MJC_EVENT_TRIGGER) {
        *(item->data.checkbox) = !(*(item->data.checkbox));
    }
}

mjc_page_t* mjc_get_current_page(void)
{
    return g_current_page;
}

uint8_t mjc_set_current_page(mjc_page_t* page)
{
    if (page == NULL || page->items == NULL || page->count == 0) {
        return 0;
    }

    if (s_edit_mode) {
        mjc_edit_clear();
    }

    if (page->selected_index >= page->count) {
        page->selected_index = 0;
    }

    g_current_page = page;
    return 1;
}

mjc_item_t* mjc_get_selected_item(void)
{
    if (g_current_page == NULL || g_current_page->items == NULL || g_current_page->count == 0) {
        return NULL;
    }

    if (g_current_page->selected_index >= g_current_page->count) {
        g_current_page->selected_index = 0;
    }

    return &g_current_page->items[g_current_page->selected_index];
}

static void mjc_set_page_parent(mjc_page_t* page, mjc_page_t* parent_page)
{
    if (page == NULL) {
        return;
    }
    page->parent_page = parent_page;
    if (page->items == NULL) {
        return;
    }

    for (uint8_t i = 0; i < page->count; i++) {
        mjc_item_t* item = &page->items[i];
        if (item->action == NULL) {
            if (item->type == MJC_ITEM_TYPE_SUBMENU) {
                item->action = mjc_submenu_action;
            } else if (item->type == MJC_ITEM_TYPE_CHECKBOX) {
                item->action = mjc_checkbox_action;
            }
        }
        if (item->type == MJC_ITEM_TYPE_SUBMENU && item->data.submenu != NULL) {
            mjc_set_page_parent(item->data.submenu, page);
        }
    }
}

uint8_t mjc_init(mjc_page_t* root_page)
{
    if (root_page == NULL || root_page->items == NULL || root_page->count == 0) {
        return 0;
    }

    if (!mjc_render_init()) {
        return 0;
    }

    mjc_edit_clear();

    if (root_page->selected_index >= root_page->count) {
        root_page->selected_index = 0;
    }

    g_current_page = root_page;
    mjc_set_page_parent(root_page, NULL);

    return 1;
}

uint8_t mjc_add_submenu(mjc_page_t* parent_page, mjc_item_t* parent_item, mjc_page_t* submenu_page)
{
    if (parent_page == NULL || parent_item == NULL || submenu_page == NULL
        || parent_item->type != MJC_ITEM_TYPE_SUBMENU) {
        return 0;
    }

    mjc_set_page_parent(submenu_page, parent_page);
    parent_item->data.submenu = submenu_page;

    return 1;
}

uint8_t mjc_update(void)
{
    if (g_current_page == NULL || g_current_page->items == NULL || g_current_page->count == 0) {
        return 0;
    }

    mjc_render_page(g_current_page);
    return 1;
}

uint8_t mjc_item_execute(mjc_item_t* item, mjc_event_type_t event)
{
    if (item == NULL) {
        return 0;
    }

    if (item->action != NULL) {
        item->action(item, event, item->user_data);
    }

    return 1;
}

uint8_t mjc_page_up(void)
{
    if (s_edit_mode || g_current_page == NULL || g_current_page->count == 0) {
        return 0;
    }

    g_current_page->selected_index =
        (uint8_t)((g_current_page->selected_index + g_current_page->count - 1U) % g_current_page->count);

    return 1;
}

uint8_t mjc_page_down(void)
{
    if (s_edit_mode || g_current_page == NULL || g_current_page->count == 0) {
        return 0;
    }

    g_current_page->selected_index =
        (uint8_t)((g_current_page->selected_index + 1U) % g_current_page->count);

    return 1;
}

uint8_t mjc_page_back(void)
{
    if (s_edit_mode) {
        return mjc_edit_cancel();
    }
    if (g_current_page == NULL || g_current_page->parent_page == NULL) {
        return 0;
    }

    return mjc_set_current_page(g_current_page->parent_page);
}

/* ---- NUMBER edit session ---- */

uint8_t mjc_is_edit_mode(void)
{
    return s_edit_mode;
}

mjc_item_t* mjc_edit_get_item(void)
{
    return s_edit_mode ? s_edit_item : NULL;
}

float mjc_edit_get_value(void)
{
    return s_edit_value;
}

float mjc_edit_get_step(void)
{
    return s_edit_step;
}

uint8_t mjc_edit_begin(void)
{
    mjc_item_t* item = mjc_get_selected_item();
    if (item == NULL || item->type != MJC_ITEM_TYPE_NUMBER) {
        return 0;
    }
    if (item->data.number.value_ptr == NULL) {
        return 0;
    }

    s_edit_item = item;
    s_edit_value = mjc_number_read_float(&item->data.number);
    s_edit_step = item->data.number.step;
    if (s_edit_step <= 0.0f) {
        s_edit_step = 1.0f;
    }
    if (s_edit_step < MJC_EDIT_STEP_MIN) {
        s_edit_step = MJC_EDIT_STEP_MIN;
    }
    if (s_edit_step > MJC_EDIT_STEP_MAX) {
        s_edit_step = MJC_EDIT_STEP_MAX;
    }
    s_edit_value = mjc_number_clamp(&item->data.number, s_edit_value);
    s_edit_mode = 1;

    (void)mjc_item_execute(item, MJC_EVENT_TRIGGER);
    return 1;
}

uint8_t mjc_edit_commit(void)
{
    if (!s_edit_mode || s_edit_item == NULL) {
        return 0;
    }

    mjc_item_t* item = s_edit_item;
    s_edit_value = mjc_number_clamp(&item->data.number, s_edit_value);
    mjc_number_write_float(&item->data.number, s_edit_value);

    mjc_edit_clear();
    (void)mjc_item_execute(item, MJC_EVENT_CHANGE);
    return 1;
}

uint8_t mjc_edit_cancel(void)
{
    if (!s_edit_mode || s_edit_item == NULL) {
        return 0;
    }

    mjc_item_t* item = s_edit_item;
    mjc_edit_clear();
    (void)mjc_item_execute(item, MJC_EVENT_EXIT);
    return 1;
}

uint8_t mjc_edit_adjust(int8_t dir)
{
    if (!s_edit_mode || s_edit_item == NULL || dir == 0) {
        return 0;
    }

    float delta = s_edit_step * (float)dir;
    s_edit_value = mjc_number_clamp(&s_edit_item->data.number, s_edit_value + delta);
    return 1;
}

uint8_t mjc_edit_scale_step(int8_t dir)
{
    if (!s_edit_mode || dir == 0) {
        return 0;
    }

    if (dir > 0) {
        s_edit_step *= 10.0f;
    } else {
        s_edit_step *= 0.1f;
    }

    if (s_edit_step < MJC_EDIT_STEP_MIN) {
        s_edit_step = MJC_EDIT_STEP_MIN;
    }
    if (s_edit_step > MJC_EDIT_STEP_MAX) {
        s_edit_step = MJC_EDIT_STEP_MAX;
    }
    return 1;
}

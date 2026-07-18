#include <stddef.h>
#include "mjc_core.h"
#include "mjc_define.h"
#include "mjc_render.h"

static mjc_page_t* g_current_page = NULL;

static void mjc_submenu_action(mjc_item_t* item, mjc_event_type_t event, void* user_data) {
    (void)user_data;
    if (item == NULL) {
        return;
    }

    if (event == MJC_EVENT_TRIGGER) {
        if (item->type == MJC_ITEM_TYPE_SUBMENU && item->data.submenu != NULL)
            mjc_set_current_page(item->data.submenu);
    }
}

static void mjc_checkbox_action(mjc_item_t* item, mjc_event_type_t event, void* user_data) {
    (void)user_data;
    if (item == NULL || item->type != MJC_ITEM_TYPE_CHECKBOX || item->data.checkbox == NULL)
        return;

    if (event == MJC_EVENT_TRIGGER)
        *(item->data.checkbox) = !(*(item->data.checkbox));
}

mjc_page_t* mjc_get_current_page(void) {
    return g_current_page;
}

uint8_t mjc_set_current_page(mjc_page_t* page) {
    if (page == NULL || page->items == NULL || page->count == 0)
        return 0;

    if (page->selected_index >= page->count)
        page->selected_index = 0;

    g_current_page = page;
    return 1;
}

mjc_item_t* mjc_get_selected_item(void) {
    if (g_current_page == NULL || g_current_page->items == NULL || g_current_page->count == 0)
        return NULL;

    if (g_current_page->selected_index >= g_current_page->count)
        g_current_page->selected_index = 0;

    return &g_current_page->items[g_current_page->selected_index];
}

static void mjc_set_page_parent(mjc_page_t* page, mjc_page_t* parent_page) {
    if (page == NULL)
        return;
    page->parent_page = parent_page;
    if (page->items == NULL)
        return;

    // 为条目设置默认 action
    for (uint8_t i = 0; i < page->count; i++) {
        mjc_item_t* item = &page->items[i];
        if (item->action == NULL) {
            if (item->type == MJC_ITEM_TYPE_SUBMENU) {
                item->action = mjc_submenu_action;
            } else if (item->type == MJC_ITEM_TYPE_CHECKBOX) {
                item->action = mjc_checkbox_action;
            }
        }
        // 递归处理子菜单
        if (item->type == MJC_ITEM_TYPE_SUBMENU && item->data.submenu != NULL) {
            mjc_set_page_parent(item->data.submenu, page);
        }
    }
}

uint8_t mjc_init(mjc_page_t* root_page) {
    if (root_page == NULL || root_page->items == NULL || root_page->count == 0)
        return 0;

    if (!mjc_render_init())
        return 0;

    if (root_page->selected_index >= root_page->count)
        root_page->selected_index = 0;

    g_current_page = root_page;
    mjc_set_page_parent(root_page, NULL);

    return 1;
}

uint8_t mjc_add_submenu(mjc_page_t* parent_page, mjc_item_t* parent_item, mjc_page_t* submenu_page) {
    if (parent_page == NULL || parent_item == NULL || submenu_page == NULL || parent_item->type != MJC_ITEM_TYPE_SUBMENU)
        return 0;

    mjc_set_page_parent(submenu_page, parent_page);
    parent_item->data.submenu = submenu_page;

    return 1;
}

uint8_t mjc_update(void) {
    if (g_current_page == NULL || g_current_page->items == NULL || g_current_page->count == 0)
        return 0;

    mjc_render_page(g_current_page);
    return 1;
}

uint8_t mjc_item_execute(mjc_item_t* item, mjc_event_type_t event) {
    if (item == NULL)
        return 0;

    if (item->action != NULL)
        item->action(item, event, item->user_data);

    return 1;
}

uint8_t mjc_page_up(void) {
    if (g_current_page == NULL || g_current_page->count == 0)
        return 0;

    g_current_page->selected_index = (g_current_page->selected_index + g_current_page->count - 1) % g_current_page->count;

    return 1;
}
uint8_t mjc_page_down(void) {
    if (g_current_page == NULL || g_current_page->count == 0)
        return 0;

    g_current_page->selected_index = (g_current_page->selected_index + 1) % g_current_page->count;

    return 1;
}
uint8_t mjc_page_back(void) {
    if (g_current_page == NULL || g_current_page->parent_page == NULL)
        return 0;

    return mjc_set_current_page(g_current_page->parent_page);
}
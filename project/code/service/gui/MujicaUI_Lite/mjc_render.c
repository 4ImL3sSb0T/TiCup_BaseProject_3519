/**
 * @file mjc_render.c
 * @brief Basic rendering for a single menu item.
 */

#include "mjc_render.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "mjc_hal.h"
#include "service/sys/sys_log.h"
#include "mjc_config.h"

static bool s_render_inited = false;

typedef struct {
    const mjc_page_t* page;
    uint8_t selected_index;
    uint8_t count;
    uint8_t view_start;     // 当前窗口起始条目索引
    uint8_t view_count;     // 当前窗口可见条目数量
    uint16_t last_used_height; // 上次渲染使用的屏幕高度
    uint32_t hashes[MJC_RENDER_MAX_TRACKED_ITEMS];
    bool valid;
} mjc_render_snapshot_t;

static mjc_render_snapshot_t s_snapshot = {0};

static uint32_t mjc_render_item_hash(const mjc_item_t* item, uint8_t selected) {
    if (item == NULL) {
        return 0U;
    }

    uint32_t h = ((uint32_t)item->type << 24) | ((uint32_t)selected << 16);

    switch (item->type) {
    case MJC_ITEM_TYPE_CHECKBOX:
        if (item->data.checkbox != NULL) {
            h ^= (uint32_t)(*item->data.checkbox & 0xFFU);
        }
        break;
    case MJC_ITEM_TYPE_SUBMENU:
        h ^= (uint32_t)(uintptr_t)item->data.submenu;
        break;
    case MJC_ITEM_TYPE_NUMBER:
        if (item->data.number.value_ptr != NULL) {
            switch (item->data.number.num_type) {
            case MJC_NUM_INT8:
                h ^= (uint32_t)(*(int8_t*)item->data.number.value_ptr);
                break;
            case MJC_NUM_UINT8:
                h ^= (uint32_t)(*(uint8_t*)item->data.number.value_ptr);
                break;
            case MJC_NUM_INT16:
                h ^= (uint32_t)(*(int16_t*)item->data.number.value_ptr);
                break;
            case MJC_NUM_UINT16:
                h ^= (uint32_t)(*(uint16_t*)item->data.number.value_ptr);
                break;
            case MJC_NUM_INT32:
                h ^= (uint32_t)(*(int32_t*)item->data.number.value_ptr);
                break;
            case MJC_NUM_UINT32:
                h ^= (*(uint32_t*)item->data.number.value_ptr);
                break;
            case MJC_NUM_FLOAT: {
                uint32_t bits = 0U;
                memcpy(&bits, item->data.number.value_ptr, sizeof(uint32_t));
                h ^= bits;
                break;
            }
            default:
                break;
            }
        }
        break;
    default:
        break;
    }

    return h;
}

static uint8_t mjc_render_items_per_screen(void) {
    uint16_t screen_h = mjc_hal_screen_height();
    if (screen_h == 0 || MJC_RENDER_FONT_HEIGHT == 0) {
        return 1;
    }
    uint16_t lines = (uint16_t)(screen_h / MJC_RENDER_FONT_HEIGHT);
    if (lines == 0) {
        lines = 1;
    }
    if (lines > UINT8_MAX) {
        lines = UINT8_MAX;
    }
    return (uint8_t)lines;
}

static uint8_t mjc_render_compute_view_start(const mjc_page_t* page, uint8_t items_per_screen) {
    if (page == NULL || items_per_screen == 0) {
        return 0;
    }

    uint8_t start = 0;

    if (s_snapshot.valid && s_snapshot.page == page && s_snapshot.count == page->count) {
        start = s_snapshot.view_start;
    }

    if (page->count <= items_per_screen) {
        return 0;
    }

    if (page->selected_index < start) {
        start = page->selected_index;
    } else if (page->selected_index >= (uint8_t)(start + items_per_screen)) {
        start = (uint8_t)(page->selected_index - items_per_screen + 1);
    }

    if ((uint16_t)start + items_per_screen > page->count) {
        start = (uint8_t)(page->count - items_per_screen);
    }

    return start;
}

static bool mjc_render_build_snapshot(const mjc_page_t* page, uint8_t view_start, uint8_t view_count, mjc_render_snapshot_t* snap) {
    if (page == NULL || page->items == NULL || snap == NULL) {
        return false;
    }

    if (view_count > MJC_RENDER_MAX_TRACKED_ITEMS) {
        snap->valid = false;
        return false;
    }

    snap->page = page;
    snap->selected_index = page->selected_index;
    snap->count = page->count;
    snap->view_start = view_start;
    snap->view_count = view_count;

    uint8_t end = (uint8_t)(view_start + view_count);
    for (uint8_t i = view_start, idx = 0; i < end; i++, idx++) {
        snap->hashes[idx] = mjc_render_item_hash(&page->items[i], (uint8_t)(i == page->selected_index));
    }
    snap->valid = true;
    return true;
}

static bool mjc_render_snapshot_equal(const mjc_render_snapshot_t* a, const mjc_render_snapshot_t* b) {
    if (a == NULL || b == NULL || !a->valid || !b->valid) {
        return false;
    }
    if (a->page != b->page) {
        return false;
    }
    if (a->count != b->count) {
        return false;
    }
    if (a->selected_index != b->selected_index) {
        return false;
    }
    if (a->view_start != b->view_start || a->view_count != b->view_count) {
        return false;
    }
    for (uint8_t i = 0; i < a->view_count; i++) {
        if (a->hashes[i] != b->hashes[i]) {
            return false;
        }
    }
    return true;
}

uint8_t mjc_render_init(void) {
    if (s_render_inited) {
        return 1;
    }

    mjc_hal_config_t cfg = mjc_hal_get_default_config();
    cfg.display_dir = MJC_DIR_PORTRAIT; // 竖屏方向，可按需调整

    if (!mjc_hal_init(&cfg)) {
        sys_log_text(error, "mjc_hal_init failed");
        return 0;
    }

    const mjc_hal_driver_t* drv = mjc_hal_get_driver();
    if (drv == NULL) {
        sys_log_text(error, "mjc_hal_get_driver null");
        return 0;
    }

    if (drv->set_color != NULL) {
        drv->set_color(0xFFFF, 0x0000); // 白前景，黑背景
    }
    if (drv->fill != NULL) {
        drv->fill(0x0000);
    }

    sys_log_text(info, "UI driver ready w=%d h=%d", mjc_hal_screen_width(), mjc_hal_screen_height());
    s_render_inited = true;
    return 1;
}

static void mjc_render_format_number(const mjc_number_config_t* number, char* buf, size_t len) {
    if (buf == NULL || len == 0) {
        return;
    }
    if (number == NULL || number->value_ptr == NULL) {
        (void)snprintf(buf, len, "--");
        return;
    }

    switch (number->num_type) {
    case MJC_NUM_INT8:
        (void)snprintf(buf, len, "%d", *(int8_t*)number->value_ptr);
        break;
    case MJC_NUM_UINT8:
        (void)snprintf(buf, len, "%u", *(uint8_t*)number->value_ptr);
        break;
    case MJC_NUM_INT16:
        (void)snprintf(buf, len, "%d", *(int16_t*)number->value_ptr);
        break;
    case MJC_NUM_UINT16:
        (void)snprintf(buf, len, "%u", *(uint16_t*)number->value_ptr);
        break;
    case MJC_NUM_INT32:
        (void)snprintf(buf, len, "%ld", (long)*(int32_t*)number->value_ptr);
        break;
    case MJC_NUM_UINT32:
        (void)snprintf(buf, len, "%lu", (unsigned long)*(uint32_t*)number->value_ptr);
        break;
    case MJC_NUM_FLOAT:
        (void)snprintf(buf, len, "%.2f", (double)*(float*)number->value_ptr);
        break;
    default:
        (void)snprintf(buf, len, "--");
        break;
    }
}

static void mjc_render_format_value(const mjc_item_t* item, char* buf, size_t len) {
    if (buf == NULL || len == 0) {
        return;
    }

    if (item == NULL) {
        buf[0] = '\0';
        return;
    }

    switch (item->type) {
    case MJC_ITEM_TYPE_LABEL:
        buf[0] = '\0';
        break;
    case MJC_ITEM_TYPE_CHECKBOX:
        if (item->data.checkbox == NULL) {
            (void)snprintf(buf, len, "--");
        } else {
            (void)snprintf(buf, len, "%s", (*item->data.checkbox) ? "On" : "Off");
        }
        break;
    case MJC_ITEM_TYPE_SUBMENU:
        (void)snprintf(buf, len, ">");
        break;
    case MJC_ITEM_TYPE_NUMBER:
        mjc_render_format_number(&item->data.number, buf, len);
        break;
    default:
        buf[0] = '\0';
        break;
    }
}

void mjc_render_item(const mjc_item_t* item, uint16_t y, uint8_t selected) {
    const mjc_hal_driver_t* drv = mjc_hal_get_driver();
    if (item == NULL || drv == NULL || drv->draw_string == NULL) {
        return;
    }

    if (drv->set_font_size != NULL) {
        drv->set_font_size(1); // 8x16 字体
    }

    char value_buf[MJC_RENDER_VALUE_BUF] = {0};
    mjc_render_format_value(item, value_buf, sizeof(value_buf));

    const uint16_t screen_w = mjc_hal_screen_width();
    if (screen_w == 0) {
        return;
    }

    const size_t value_len = strlen(value_buf);
    const uint16_t value_width = (uint16_t)(value_len * MJC_RENDER_FONT_WIDTH);
    uint16_t value_x = MJC_RENDER_PADDING_X;
    if (screen_w > (MJC_RENDER_PADDING_X + value_width)) {
        value_x = (uint16_t)(screen_w - MJC_RENDER_PADDING_X - value_width);
    }

    char name_buf[MJC_RENDER_NAME_BUF] = {0};
    uint16_t max_name_width = 0;
    if (value_x > (MJC_RENDER_PADDING_X + MJC_RENDER_GAP_X)) {
        max_name_width = (uint16_t)(value_x - MJC_RENDER_PADDING_X - MJC_RENDER_GAP_X);
    }
    size_t max_name_chars = max_name_width / MJC_RENDER_FONT_WIDTH;
    if (max_name_chars >= (MJC_RENDER_NAME_BUF - 1)) {
        max_name_chars = MJC_RENDER_NAME_BUF - 1;
    }

    if (item->name != NULL && max_name_chars > 0) {
        size_t copy_len = strlen(item->name);
        if (copy_len > max_name_chars) {
            copy_len = max_name_chars;
        }
        memcpy(name_buf, item->name, copy_len);
        name_buf[copy_len] = '\0';
    }

    const uint16_t pen_color = selected ? MJC_RENDER_COLOR_SELECTED_PEN : MJC_RENDER_COLOR_NORMAL_PEN;
    const uint16_t bg_color = selected ? MJC_RENDER_COLOR_SELECTED_BG : MJC_RENDER_COLOR_NORMAL_BG;

    if (drv->set_color != NULL) {
        drv->set_color(pen_color, bg_color);
    }

    // 填充整行背景，避免残影
    if (drv->fill_rect != NULL) {
        drv->fill_rect(0, y, screen_w, MJC_RENDER_FONT_HEIGHT, bg_color);
    }

    drv->draw_string(MJC_RENDER_PADDING_X, y, name_buf);
    drv->draw_string(value_x, y, value_buf);
}

void mjc_render_page(const mjc_page_t* page) {
    if (page == NULL || page->items == NULL) {
        return;
    }

    const uint8_t items_per_screen = mjc_render_items_per_screen();
    uint8_t view_count = items_per_screen;
    if (view_count == 0) {
        view_count = 1;
    }
    if (view_count > page->count) {
        view_count = page->count;
    }

    uint8_t view_start = mjc_render_compute_view_start(page, view_count);
    uint8_t view_end = (uint8_t)(view_start + view_count);

    mjc_render_snapshot_t next = {0};
    bool trackable = mjc_render_build_snapshot(page, view_start, view_count, &next);
    if (trackable && mjc_render_snapshot_equal(&next, &s_snapshot)) {
        return; // 未变化，跳过重绘
    }

    uint16_t y = 0;
    for (uint8_t i = view_start; i < view_end; i++) {
        mjc_render_item(&page->items[i], y, (i == page->selected_index));
        y = (uint16_t)(y + MJC_RENDER_FONT_HEIGHT);
    }

    // 只在必要时清除多余区域：页面切换或 view_count 缩小
    uint16_t used_h = (uint16_t)(view_count * MJC_RENDER_FONT_HEIGHT);
    bool need_clear = false;
    if (!s_snapshot.valid || s_snapshot.page != page) {
        need_clear = true;  // 页面切换
    } else if (s_snapshot.last_used_height > used_h) {
        need_clear = true;  // 高度缩小，需清除旧内容
    }

    if (need_clear) {
        const mjc_hal_driver_t* drv = mjc_hal_get_driver();
        if (drv != NULL && drv->fill_rect != NULL) {
            uint16_t screen_h = mjc_hal_screen_height();
            uint16_t screen_w = mjc_hal_screen_width();
            if (screen_h > used_h) {
                drv->fill_rect(0, used_h, screen_w, (uint16_t)(screen_h - used_h), MJC_RENDER_COLOR_NORMAL_BG);
            }
        }
    }

    if (trackable) {
        s_snapshot = next;
        s_snapshot.last_used_height = used_h;
    } else {
        s_snapshot.valid = false;
    }
}

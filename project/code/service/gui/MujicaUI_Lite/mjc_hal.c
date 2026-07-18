#include "mjc_hal.h"
#include "zf_common_font.h"
#include "zf_device_ips114.h"
#include "zf_device_ips200.h"

static mjc_hal_driver_t g_ips200_driver;
static mjc_hal_driver_t g_ips114_driver;
static mjc_hal_driver_t* g_active_driver = NULL;

static uint8_t mjc_point_in_bounds(uint16_t x, uint16_t y) {
    return (g_active_driver != NULL) && (x < g_active_driver->width) && (y < g_active_driver->height);
}

static uint8_t mjc_clip_rect(uint16_t x, uint16_t y, uint16_t* w, uint16_t* h) {
    if ((g_active_driver == NULL) || (w == NULL) || (h == NULL)) {
        return 0;
    }

    if (x >= g_active_driver->width || y >= g_active_driver->height) {
        return 0;
    }

    uint32_t max_w = g_active_driver->width - x;
    uint32_t max_h = g_active_driver->height - y;

    if (*w > max_w) {
        *w = (uint16_t)max_w;
    }
    if (*h > max_h) {
        *h = (uint16_t)max_h;
    }

    return (*w > 0 && *h > 0);
}

static void mjc_ips200_apply_dir(mjc_display_dir_t dir) {
    switch (dir) {
    case MJC_DIR_PORTRAIT:
        ips200_set_dir(IPS200_PORTAIT);
        g_ips200_driver.width = 240;
        g_ips200_driver.height = 320;
        break;
    case MJC_DIR_PORTRAIT_180:
        ips200_set_dir(IPS200_PORTAIT_180);
        g_ips200_driver.width = 240;
        g_ips200_driver.height = 320;
        break;
    case MJC_DIR_LANDSCAPE:
        ips200_set_dir(IPS200_CROSSWISE);
        g_ips200_driver.width = 320;
        g_ips200_driver.height = 240;
        break;
    case MJC_DIR_LANDSCAPE_180:
        ips200_set_dir(IPS200_CROSSWISE_180);
        g_ips200_driver.width = 320;
        g_ips200_driver.height = 240;
        break;
    case MJC_DIR_AUTO:
    default:
        ips200_set_dir(IPS200_DEFAULT_DISPLAY_DIR);
        if (IPS200_DEFAULT_DISPLAY_DIR == IPS200_CROSSWISE || IPS200_DEFAULT_DISPLAY_DIR == IPS200_CROSSWISE_180) {
            g_ips200_driver.width = 320;
            g_ips200_driver.height = 240;
        } else {
            g_ips200_driver.width = 240;
            g_ips200_driver.height = 320;
        }
        break;
    }
}

static void mjc_ips114_apply_dir(mjc_display_dir_t dir) {
    switch (dir) {
    case MJC_DIR_PORTRAIT:
        ips114_set_dir(IPS114_PORTAIT);
        g_ips114_driver.width = 240;
        g_ips114_driver.height = 135;
        break;
    case MJC_DIR_PORTRAIT_180:
        ips114_set_dir(IPS114_PORTAIT_180);
        g_ips114_driver.width = 240;
        g_ips114_driver.height = 135;
        break;
    case MJC_DIR_LANDSCAPE:
        ips114_set_dir(IPS114_CROSSWISE);
        g_ips114_driver.width = 135;
        g_ips114_driver.height = 240;
        break;
    case MJC_DIR_LANDSCAPE_180:
        ips114_set_dir(IPS114_CROSSWISE_180);
        g_ips114_driver.width = 135;
        g_ips114_driver.height = 240;
        break;
    case MJC_DIR_AUTO:
    default:
        ips114_set_dir(IPS114_DEFAULT_DISPLAY_DIR);
        if (IPS114_DEFAULT_DISPLAY_DIR == IPS114_CROSSWISE || IPS114_DEFAULT_DISPLAY_DIR == IPS114_CROSSWISE_180) {
            g_ips114_driver.width = 135;
            g_ips114_driver.height = 240;
        } else {
            g_ips114_driver.width = 240;
            g_ips114_driver.height = 135;
        }
        break;
    }
}

static void mjc_ips200_clear(void) {
    ips200_clear();
}

static void mjc_ips200_fill(uint16_t color) {
    ips200_full(color);
}

static void mjc_ips200_set_color(uint16_t pen, uint16_t bg) {
    ips200_set_color(pen, bg);
    g_ips200_driver.pen_color = pen;
    g_ips200_driver.bg_color = bg;
}

static void mjc_ips200_draw_point(uint16_t x, uint16_t y, uint16_t color) {
    if (!mjc_point_in_bounds(x, y)) {
        return;
    }
    ips200_draw_point(x, y, color);
}

static void mjc_ips200_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    if (!mjc_point_in_bounds(x0, y0) || !mjc_point_in_bounds(x1, y1)) {
        return;
    }
    ips200_draw_line(x0, y0, x1, y1, color);
}

static void mjc_ips200_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (!mjc_clip_rect(x, y, &w, &h)) {
        return;
    }
    uint16_t x_end = x + w - 1;
    uint16_t y_end = y + h - 1;
    ips200_draw_line(x, y, x_end, y, color);
    ips200_draw_line(x, y_end, x_end, y_end, color);
    ips200_draw_line(x, y, x, y_end, color);
    ips200_draw_line(x_end, y, x_end, y_end, color);
}

static void mjc_ips200_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (!mjc_clip_rect(x, y, &w, &h)) {
        return;
    }
    uint16_t y_end = y + h;
    uint16_t x_end = x + w - 1;
    for (uint16_t row = y; row < y_end; row++) {
        ips200_draw_line(x, row, x_end, row, color);
    }
}

static void mjc_ips200_draw_char(uint16_t x, uint16_t y, char c) {
    if (!mjc_point_in_bounds(x, y)) {
        return;
    }
    char buf[2] = {c, '\0'};
    ips200_show_string(x, y, buf);
}

static void mjc_ips200_draw_string(uint16_t x, uint16_t y, const char* str) {
    if (!mjc_point_in_bounds(x, y) || str == NULL) {
        return;
    }
    ips200_show_string(x, y, str);
}

static void mjc_ips200_set_font_size(uint8_t size) {
    ips200_font_size_enum font = (size == 0) ? IPS200_6X8_FONT : IPS200_8X16_FONT;
    ips200_set_font(font);
}

static void mjc_ips114_clear(void) {
    ips114_clear();
}

static void mjc_ips114_fill(uint16_t color) {
    ips114_full(color);
}

static void mjc_ips114_set_color(uint16_t pen, uint16_t bg) {
    ips114_set_color(pen, bg);
    g_ips114_driver.pen_color = pen;
    g_ips114_driver.bg_color = bg;
}

static void mjc_ips114_draw_point(uint16_t x, uint16_t y, uint16_t color) {
    if (!mjc_point_in_bounds(x, y)) {
        return;
    }
    ips114_draw_point(x, y, color);
}

static void mjc_ips114_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    if (!mjc_point_in_bounds(x0, y0) || !mjc_point_in_bounds(x1, y1)) {
        return;
    }
    ips114_draw_line(x0, y0, x1, y1, color);
}

static void mjc_ips114_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (!mjc_clip_rect(x, y, &w, &h)) {
        return;
    }
    uint16_t x_end = x + w - 1;
    uint16_t y_end = y + h - 1;
    ips114_draw_line(x, y, x_end, y, color);
    ips114_draw_line(x, y_end, x_end, y_end, color);
    ips114_draw_line(x, y, x, y_end, color);
    ips114_draw_line(x_end, y, x_end, y_end, color);
}

static void mjc_ips114_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (!mjc_clip_rect(x, y, &w, &h)) {
        return;
    }
    uint16_t y_end = y + h;
    uint16_t x_end = x + w - 1;
    for (uint16_t row = y; row < y_end; row++) {
        ips114_draw_line(x, row, x_end, row, color);
    }
}

static void mjc_ips114_draw_char(uint16_t x, uint16_t y, char c) {
    if (!mjc_point_in_bounds(x, y)) {
        return;
    }
    char buf[2] = {c, '\0'};
    ips114_show_string(x, y, buf);
}

static void mjc_ips114_draw_string(uint16_t x, uint16_t y, const char* str) {
    if (!mjc_point_in_bounds(x, y) || str == NULL) {
        return;
    }
    ips114_show_string(x, y, str);
}

static void mjc_ips114_set_font_size(uint8_t size) {
    ips114_font_size_enum font = (size == 0) ? IPS114_6X8_FONT : IPS114_8X16_FONT;
    ips114_set_font(font);
}

static void mjc_bind_ips200(mjc_display_dir_t dir, ips200_type_enum bus) {
    ips200_init(bus);
    mjc_ips200_apply_dir(dir);

    g_ips200_driver.pen_color = IPS200_DEFAULT_PENCOLOR;
    g_ips200_driver.bg_color = IPS200_DEFAULT_BGCOLOR;
    g_ips200_driver.user_data = NULL;

    g_ips200_driver.clear = mjc_ips200_clear;
    g_ips200_driver.fill = mjc_ips200_fill;
    g_ips200_driver.set_color = mjc_ips200_set_color;
    g_ips200_driver.draw_point = mjc_ips200_draw_point;
    g_ips200_driver.draw_line = mjc_ips200_draw_line;
    g_ips200_driver.draw_rect = mjc_ips200_draw_rect;
    g_ips200_driver.fill_rect = mjc_ips200_fill_rect;
    g_ips200_driver.draw_char = mjc_ips200_draw_char;
    g_ips200_driver.draw_string = mjc_ips200_draw_string;
    g_ips200_driver.set_font_size = mjc_ips200_set_font_size;

    ips200_set_color(g_ips200_driver.pen_color, g_ips200_driver.bg_color);
    ips200_set_font(IPS200_DEFAULT_DISPLAY_FONT);
}

static void mjc_bind_ips114(mjc_display_dir_t dir) {
    ips114_init();
    mjc_ips114_apply_dir(dir);

    g_ips114_driver.pen_color = IPS114_DEFAULT_PENCOLOR;
    g_ips114_driver.bg_color = IPS114_DEFAULT_BGCOLOR;
    g_ips114_driver.user_data = NULL;

    g_ips114_driver.clear = mjc_ips114_clear;
    g_ips114_driver.fill = mjc_ips114_fill;
    g_ips114_driver.set_color = mjc_ips114_set_color;
    g_ips114_driver.draw_point = mjc_ips114_draw_point;
    g_ips114_driver.draw_line = mjc_ips114_draw_line;
    g_ips114_driver.draw_rect = mjc_ips114_draw_rect;
    g_ips114_driver.fill_rect = mjc_ips114_fill_rect;
    g_ips114_driver.draw_char = mjc_ips114_draw_char;
    g_ips114_driver.draw_string = mjc_ips114_draw_string;
    g_ips114_driver.set_font_size = mjc_ips114_set_font_size;

    ips114_set_color(g_ips114_driver.pen_color, g_ips114_driver.bg_color);
    ips114_set_font(IPS114_DEFAULT_DISPLAY_FONT);
}

uint8_t mjc_hal_use_driver(mjc_hal_driver_t* driver) {
    if (driver == NULL) {
        return 0;
    }
    g_active_driver = driver;
    return 1;
}

uint8_t mjc_hal_init(const mjc_hal_config_t* config) {
    mjc_hal_config_t cfg = mjc_hal_get_default_config();
    if (config != NULL) {
        cfg = *config;
    }

    switch (cfg.driver_type) {
    case MJC_DRIVER_IPS200:
        mjc_bind_ips200(cfg.display_dir, cfg.ips200_bus);
        g_active_driver = &g_ips200_driver;
        break;
    case MJC_DRIVER_IPS114:
        mjc_bind_ips114(cfg.display_dir);
        g_active_driver = &g_ips114_driver;
        break;
    case MJC_DRIVER_CUSTOM:
        return mjc_hal_use_driver(cfg.custom_driver);
    default:
        return 0;
    }

    return 1;
}

const mjc_hal_driver_t* mjc_hal_get_driver(void) {
    return g_active_driver;
}

uint16_t mjc_hal_screen_width(void) {
    return g_active_driver ? g_active_driver->width : 0;
}

uint16_t mjc_hal_screen_height(void) {
    return g_active_driver ? g_active_driver->height : 0;
}

#ifndef __MJC_HAL_H
#define __MJC_HAL_H

#include <stdint.h>
#include "mjc_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mjc_hal_driver_t {
    uint16_t width;
    uint16_t height;
    uint16_t pen_color;
    uint16_t bg_color;
    void* user_data;

    void (*clear)(void);
    void (*fill)(uint16_t color);
    void (*set_color)(uint16_t pen, uint16_t bg);
    void (*draw_point)(uint16_t x, uint16_t y, uint16_t color);
    void (*draw_line)(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
    void (*draw_rect)(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    void (*fill_rect)(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    void (*draw_char)(uint16_t x, uint16_t y, char c);
    void (*draw_string)(uint16_t x, uint16_t y, const char* str);
    void (*set_font_size)(uint8_t size);
} mjc_hal_driver_t;

uint8_t mjc_hal_init(const mjc_hal_config_t* config);
uint8_t mjc_hal_use_driver(mjc_hal_driver_t* driver);
const mjc_hal_driver_t* mjc_hal_get_driver(void);
uint16_t mjc_hal_screen_width(void);
uint16_t mjc_hal_screen_height(void);

#ifdef __cplusplus
}
#endif

#endif // !__MJC_HAL_H
